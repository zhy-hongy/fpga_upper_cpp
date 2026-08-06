#include "camera_device.h"

#include <cstring>

#include <QCoreApplication>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>

#include "sdk/src/frame_packet.h"
#include "sdk/src/sdk_protocol_thread.h"
#include "sdk_test/mock/mock_protocol_handler.h"

namespace {

// ── SDK 全局状态（借用协议指针 + 已打开设备表 + 独立模式内部通信）────────────────
// 约束：宿主模式只保存宿主 ProtocolHandler*（borrowed, never owned）；
//       独立模式（Cam_OpenNetworkDevice/Mock）由 SDK 拥有内部 SdkProtocolThread。
// 两种模式互斥：任意时刻至多存在【一套】UDPWorker + UDPDataThread。
QMutex               g_mutex;        // 保护下面全局状态
bool                 g_initialized = false;
ProtocolHandler*     g_protocol = nullptr;      // borrowed, never owned（宿主注入）
QMap<CameraHandle, CameraDevice*> g_devices;    // 句柄 -> SDK 内部设备

// 独立 SDK 模式内部通信宿主线程（owned）。非空 ⇔ 独立设备已打开。
SdkProtocolThread*   g_internalThread = nullptr;

// 独立模式（第三方非 Qt 进程）自建的 QCoreApplication。
// 原因：ProtocolAdapter 的控制投递用 QMetaObject::invokeMethod(..., QueuedConnection)，
// 事件投递依赖 QCoreApplication::sendEvent 路由——进程内没有 Qt App 实例时
// QMetaCallEvent 永不投递，SdkProtocolThread 的 exec() 事件循环不会处理它，
// 导致异步/同步控制命令全部超时。宿主（fpga_upper.exe 已是 Qt App）不触发。
QCoreApplication*    g_app = nullptr;            // owned（仅当本 DLL 自建时非空）
int                  s_appArgc = 1;
char                 s_appName[] = "PhotonCameraSDK";
char*                s_appArgv[] = { s_appName, nullptr };

const uint32_t kMaxGatewayCount = 1;  // 当前接入方式：单 FPGA 网关

CameraDevice* device_for(CameraHandle handle) {
    return g_devices.value(handle, nullptr);
}

// 独立模式端口映射（与宿主一致）：controlPort=本地控制绑定，dataPort=本地数据绑定
// + FPGA 目标端口；命令发往 target_host:dataPort。
Config make_standalone_config(const QString& ip, uint16_t controlPort, uint16_t dataPort) {
    Config c;
    c.target_host = ip;
    c.target_port = dataPort;
    c.control_port = controlPort;
    c.data_port = dataPort;
    c.host_ip = "0.0.0.0";
    return c;
}

// 通用内部打开逻辑（mockMode=true 时创建 MockProtocolHandler，无 UDP；否则真实通信）。
// 调用方必须已持有 g_mutex。
CamError open_internal(const char* fpgaIp, uint16_t controlPort, uint16_t dataPort,
                       CameraHandle* pHandle, bool mockMode) {
    if (!g_initialized) return CAM_ERR_NOT_INITIALIZED;
    if (g_protocol) return CAM_ERR_DEVICE_BUSY;          // 宿主模式已注入
    if (!g_devices.isEmpty()) return CAM_ERR_DEVICE_BUSY; // 单设备限制
    if (g_internalThread) return CAM_ERR_DEVICE_BUSY;     // 独立设备已打开

    SdkProtocolThread* th = new SdkProtocolThread(
        make_standalone_config(QString::fromUtf8(fpgaIp), controlPort, dataPort),
        mockMode);
    th->start();
    ProtocolHandler* internal = th->wait_ready();
    if (!internal) {
        // 内部 handler 创建失败（超时等）：回收线程后返回错误。
        th->request_stop();
        delete th;
        return CAM_ERR_UNKNOWN;
    }

    CameraDevice* dev = new CameraDevice(internal, 0);
    dev->set_ip(QString::fromUtf8(fpgaIp));
    dev->open();
    *pHandle = dev;
    g_devices.insert(dev, dev);
    g_internalThread = th;
    return CAM_OK;
}

// 停止并销毁独立模式内部通信线程（不持 g_mutex 调用；内部会 wait()）。
void stop_internal_thread() {
    SdkProtocolThread* th = nullptr;
    {
        QMutexLocker locker(&g_mutex);
        th = g_internalThread;
        g_internalThread = nullptr;
    }
    if (th) {
        th->request_stop();
        delete th;
    }
}

}  // namespace

CameraDevice::CameraDevice(ProtocolHandler* protocol, uint32_t index)
    : m_protocol(protocol),
      m_adapter(protocol),   // 借用指针的适配层，不创建任何通信对象
      m_index(index),
      m_dispatcher(new DataDispatcher()),
      m_worker(new CallbackWorker(m_dispatcher)),
      m_cmdMgr(new CommandManager()) {   // CameraDevice 非 QObject，parent=nullptr；dtor 手动 delete
    m_adapter.set_command_manager(m_cmdMgr);
    if (m_protocol) {
        // 直连（sender 线程执行）投递到 SDK 队列；enqueue 线程安全。
        // 不修改 FramePacket 来源字段，不重新组帧/解析。
        m_frameConn = QObject::connect(
            m_protocol, &ProtocolHandler::frame_ready,
            [this](const FramePacket& f) { m_dispatcher->enqueue(f); });
    }
    // 启动 SDK 线程：数据回调 worker + 异步控制命令管理
    m_worker->start();
    m_cmdMgr->start();
}

CameraDevice::~CameraDevice() {
    // 先停数据链路，再断开信号，最后销毁线程与调度器。
    stop_data_path();
    if (m_frameConn) {
        QObject::disconnect(m_frameConn);
        m_frameConn = QMetaObject::Connection();
    }
    // 停异步命令管理线程：若从命令线程自身关闭设备则不能自等待（不支持，文档注明）。
    if (m_cmdMgr) {
        m_cmdMgr->request_stop();
        if (QThread::currentThread() != m_cmdMgr) m_cmdMgr->wait();
        delete m_cmdMgr;
        m_cmdMgr = nullptr;
    }
    delete m_worker;       // 线程已停止（非回调线程自关闭场景）
    delete m_dispatcher;
}

void CameraDevice::stop_data_path() {
    if (!m_worker) return;
    // 1) 先摘回调：停止过程中不再发起新回调（进行中的回调自然结束）。
    m_worker->clear_handler();
    // 2) 请求停止 run() 循环。
    m_worker->request_stop();
    // 3) 等待线程退出。若从回调线程自身关闭设备则不能自等待（不支持，文档注明）。
    if (QThread::currentThread() != m_worker) {
        m_worker->wait();
    }
}

// =========================================================================
// 4.1 环境与网关发现
// =========================================================================

CAM_API CamError CAM_CALL Cam_Initialize() {
    QMutexLocker locker(&g_mutex);
    if (g_initialized) return CAM_OK;
    // FramePacket 跨线程 QueuedConnection 依赖 metatype 注册（幂等，可重复调用）
    qRegisterMetaType<FramePacket>("FramePacket");
    // 独立 SDK 模式：进程内无 Qt App 时自建 QCoreApplication（否则 QueuedConnection 事件
    // 无法投递，异步/同步控制全部超时）。宿主（fpga_upper.exe 已是 Qt App）不触发。
    if (!QCoreApplication::instance()) {
        g_app = new QCoreApplication(s_appArgc, s_appArgv);
    }
    g_initialized = true;
    return CAM_OK;
}

CAM_API CamError CAM_CALL Cam_Finalize() {
    QList<CameraDevice*> devs;
    {
        QMutexLocker locker(&g_mutex);
        for (auto it = g_devices.begin(); it != g_devices.end(); ++it)
            devs.append(it.value());
        g_devices.clear();
        g_protocol = nullptr;
        g_initialized = false;
    }
    // 独立模式：先停内部通信线程（UDP 停止、不再产生帧），避免与设备析构竞态。
    // 宿主模式该函数为空操作（g_internalThread == nullptr）。
    stop_internal_thread();
    // 不持 g_mutex 停止数据链路并销毁：避免回调线程 re-enter SDK 造成死锁。
    // 只销毁 SDK 内部 CameraDevice；借用的 ProtocolHandler 由宿主管理，不释放。
    for (CameraDevice* d : devs) {
        d->stop_data_path();
        delete d;
    }
    // 自建的 QCoreApplication 在 Finalize 释放（此时 SDK 内所有 Qt 线程已停）；
    // 宿主自建的（fpga_upper 的 QApplication）不动。
    if (g_app) {
        delete g_app;
        g_app = nullptr;
    }
    return CAM_OK;
}

CAM_API const char* CAM_CALL Cam_GetSdkVersion() {
    static const char kVersion[] = "1.0.0";
    return kVersion;
}

CAM_API CamError CAM_CALL Cam_EnumDevices(DeviceList* pList, uint32_t /*timeoutMs*/) {
    if (!pList) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    if (!g_initialized) return CAM_ERR_NOT_INITIALIZED;

    pList->count = 0;
    // 当前接入方式：宿主传入单个 ProtocolHandler（即单 FPGA 网关），
    // 或独立 SDK 模式（Cam_OpenNetworkDevice/Mock）内部通信已打开。
    if (g_protocol || g_internalThread) {
        pList->count = 1;
        DeviceInformation& info = pList->devices[0];
        std::memset(&info, 0, sizeof(info));
        info.status = 1;  // 网关在线（基础状态）
    }
    return CAM_OK;
}

// =========================================================================
// 4.2 宿主集成
// =========================================================================

CAM_API CamError CAM_CALL Cam_AttachProtocolHandler(void* protocolHandler) {
    QMutexLocker locker(&g_mutex);
    if (!protocolHandler) return CAM_ERR_INVALID_PARAM;
    if (g_internalThread) return CAM_ERR_DEVICE_BUSY;  // 独立 SDK 模式已打开，互斥
    g_protocol = static_cast<ProtocolHandler*>(protocolHandler);  // borrowed
    return CAM_OK;
}

// =========================================================================
// 4.3 网关连接与主控信息
// =========================================================================

CAM_API CamError CAM_CALL Cam_OpenDevice(uint32_t index, CameraHandle* pHandle) {
    if (!pHandle) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    if (!g_initialized) return CAM_ERR_NOT_INITIALIZED;
    if (!g_protocol) return CAM_ERR_NO_DEVICE;
    if (index >= kMaxGatewayCount) return CAM_ERR_NO_DEVICE;  // 仅 1 个网关
    if (!g_devices.isEmpty()) return CAM_ERR_DEVICE_BUSY;     // 单设备已打开

    CameraDevice* dev = new CameraDevice(g_protocol, index);
    dev->open();
    *pHandle = dev;
    g_devices.insert(dev, dev);
    return CAM_OK;
}

CAM_API CamError CAM_CALL Cam_OpenDeviceByIP(const char* ipAddress, CameraHandle* pHandle) {
    if (!pHandle) return CAM_ERR_INVALID_PARAM;
    if (!ipAddress || !*ipAddress) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    if (!g_initialized) return CAM_ERR_NOT_INITIALIZED;
    if (!g_protocol) return CAM_ERR_NO_DEVICE;
    if (!g_devices.isEmpty()) return CAM_ERR_DEVICE_BUSY;

    // 当前协议无按 IP 建网能力：复用宿主通信链路，仅记录目标 IP。
    CameraDevice* dev = new CameraDevice(g_protocol, 0);
    dev->set_ip(QString::fromUtf8(ipAddress));
    dev->open();
    *pHandle = dev;
    g_devices.insert(dev, dev);
    return CAM_OK;
}

// =========================================================================
// 4.3b 独立 SDK 模式（无宿主注入，SDK 内部自建唯一一套通信）
// =========================================================================

CAM_API CamError CAM_CALL Cam_OpenNetworkDevice(const char* fpgaIp, uint16_t controlPort,
                                                uint16_t dataPort, CameraHandle* pHandle) {
    if (!pHandle) return CAM_ERR_INVALID_PARAM;
    if (!fpgaIp || !*fpgaIp) return CAM_ERR_INVALID_PARAM;
    if (controlPort == 0 || dataPort == 0) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    return open_internal(fpgaIp, controlPort, dataPort, pHandle, /*mockMode=*/false);
}

// =========================================================================
// 4.3c 测试专用 Mock 设备（TEST-ONLY，无 FPGA 环境，仅 sdk_standalone_test --mock 使用）
// =========================================================================

CAM_API CamError CAM_CALL Cam_OpenNetworkDeviceMock(const char* fpgaIp, uint16_t controlPort,
                                                    uint16_t dataPort, CameraHandle* pHandle) {
    if (!pHandle) return CAM_ERR_INVALID_PARAM;
    if (!fpgaIp || !*fpgaIp) return CAM_ERR_INVALID_PARAM;
    if (controlPort == 0 || dataPort == 0) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    return open_internal(fpgaIp, controlPort, dataPort, pHandle, /*mockMode=*/true);
}

CAM_API CamError CAM_CALL Cam_GetMockSentCommandCount(CameraHandle handle, uint32_t* pCount) {
    if (!pCount) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    auto* mock = dynamic_cast<MockProtocolHandler*>(dev->protocol());
    if (!mock) return CAM_ERR_NOT_SUPPORTED;   // 非 Mock 设备
    *pCount = static_cast<uint32_t>(mock->sent_command_count());
    return CAM_OK;
}

CAM_API CamError CAM_CALL Cam_CloseDevice(CameraHandle handle) {
    CameraDevice* dev = nullptr;
    bool standalone = false;
    {
        QMutexLocker locker(&g_mutex);
        dev = device_for(handle);
        if (!dev) return CAM_ERR_INVALID_HANDLE;
        g_devices.remove(handle);
        standalone = (g_internalThread != nullptr);  // 独立模式：关闭的是内部通信设备
    }
    // 独立模式：先停内部通信线程（UDP 停止、不再产生帧），避免与设备析构竞态。
    // 不持 g_mutex 停止数据链路并销毁：避免回调线程 re-enter SDK 造成死锁。
    if (standalone) stop_internal_thread();
    dev->stop_data_path();
    delete dev;
    return CAM_OK;
}

CAM_API CamError CAM_CALL Cam_GetDeviceInformation(CameraHandle handle, DeviceInformation* pInfo) {
    if (!pInfo) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;

    // 当前协议无独立主控信息查询命令：返回零填充 + 在线状态。
    std::memset(pInfo, 0, sizeof(*pInfo));
    pInfo->status = 1;
    return CAM_OK;
}

CAM_API CamError CAM_CALL Cam_GetRunState(CameraHandle handle, DeviceRunState* pState) {
    if (!pState) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;

    *pState = dev->is_open() ? STATE_READY : STATE_IDLE;
    return CAM_OK;
}

// =========================================================================
// 4.4 Sensor 独立节点查询
// =========================================================================

CAM_API CamError CAM_CALL Cam_GetSensorCount(CameraHandle handle, uint32_t* pCount) {
    if (!pCount) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;

    // 架构固定 4 个传感器槽位（sensorId 1~4）。非协议读取，不伪造数据。
    *pCount = 4;
    return CAM_OK;
}

CAM_API CamError CAM_CALL Cam_GetSensorInformation(CameraHandle handle, uint16_t sensorId,
                                                   SensorInformation* pInfo) {
    if (!pInfo) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (sensorId < 1 || sensorId > 4) return CAM_ERR_INVALID_PARAM;

    // 当前协议无 Sensor EEPROM (0x0000~0x0015) 读取命令 → 不支持。
    return CAM_ERR_NOT_SUPPORTED;
}

// =========================================================================
// 4.5 Sensor 参数读写 (转发 ProtocolAdapter → ProtocolHandler 现有命令)
// =========================================================================

CAM_API CamError CAM_CALL Cam_SetSensorConfig(CameraHandle handle, uint16_t sensorId,
                                              const SensorConfig* pConfig) {
    if (!pConfig) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    return dev->adapter()->set_sensor_config(sensorId, *pConfig);
}

CAM_API CamError CAM_CALL Cam_GetSensorConfig(CameraHandle handle, uint16_t sensorId,
                                              SensorConfig* pConfig) {
    if (!pConfig) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    return dev->adapter()->get_sensor_config(sensorId, *pConfig);
}

// =========================================================================
// 4.6 Trigger & Batch 控制
// =========================================================================

CAM_API CamError CAM_CALL Cam_SetTriggerConfig(CameraHandle handle, uint16_t sensorId,
                                               const TriggerConfig* pConfig) {
    if (!pConfig) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    return dev->adapter()->set_trigger_config(sensorId, *pConfig);
}

CAM_API CamError CAM_CALL Cam_GetTriggerConfig(CameraHandle handle, uint16_t sensorId,
                                               TriggerConfig* pConfig) {
    if (!pConfig) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    return dev->adapter()->get_trigger_config(sensorId, *pConfig);
}

CAM_API CamError CAM_CALL Cam_SetBatchConfig(CameraHandle handle, uint16_t sensorId,
                                             const BatchConfig* pConfig) {
    if (!pConfig) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    return dev->adapter()->set_batch_config(sensorId, *pConfig);
}

CAM_API CamError CAM_CALL Cam_GetBatchConfig(CameraHandle handle, uint16_t sensorId,
                                             BatchConfig* pConfig) {
    if (!pConfig) return CAM_ERR_INVALID_PARAM;
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    return dev->adapter()->get_batch_config(sensorId, *pConfig);
}

// =========================================================================
// 4.7 标定矩阵读写（当前协议无对应命令 → 不支持）
// =========================================================================

CAM_API CamError CAM_CALL Cam_WriteCalibration(CameraHandle /*handle*/, uint16_t /*sensorId*/,
                                               const CalibrationData* /*pCalibData*/) {
    // 协议无标定矩阵写命令 (EEPROM 0x0016~0x0095) → 不支持。
    return CAM_ERR_NOT_SUPPORTED;
}

CAM_API CamError CAM_CALL Cam_ReadCalibration(CameraHandle /*handle*/, uint16_t /*sensorId*/,
                                              CalibrationData* /*pCalibData*/) {
    // 协议无标定矩阵读命令 → 不支持。
    return CAM_ERR_NOT_SUPPORTED;
}

// =========================================================================
// 4.8 采集控制 (sensorId=0 由 ProtocolAdapter 广播到 1~4)
// =========================================================================

CAM_API CamError CAM_CALL Cam_StartCapture(CameraHandle handle, uint16_t sensorId) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    return dev->adapter()->start_capture(sensorId);
}

CAM_API CamError CAM_CALL Cam_StopCapture(CameraHandle handle, uint16_t sensorId) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    return dev->adapter()->stop_capture(sensorId);
}

CAM_API CamError CAM_CALL Cam_StartBatch(CameraHandle handle, uint16_t sensorId, uint32_t batchId) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    return dev->adapter()->start_batch(sensorId, batchId);
}

CAM_API CamError CAM_CALL Cam_StopBatch(CameraHandle handle, uint16_t sensorId) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    return dev->adapter()->stop_batch(sensorId);
}

CAM_API CamError CAM_CALL Cam_ResetBuffer(CameraHandle handle, uint16_t sensorId) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    return dev->adapter()->reset_buffer(sensorId);
}

// =========================================================================
// 4.9 多通道异步数据回调与同步接收
//    链路：UDPDataThread → ProtocolHandler::frame_ready → DataDispatcher
//          → CallbackWorker（SDK 唯一新增线程）→ DataHandler
// =========================================================================

CAM_API CamError CAM_CALL Cam_RegisterDataHandler(CameraHandle handle, DataHandler handler,
                                                  void* pUserData) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (!handler) return CAM_ERR_INVALID_PARAM;
    if (!dev->worker()) return CAM_ERR_NOT_INITIALIZED;
    dev->worker()->set_handler(handler, pUserData);
    return CAM_OK;
}

CAM_API CamError CAM_CALL Cam_UnregisterDataHandler(CameraHandle handle) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (dev->worker()) dev->worker()->clear_handler();
    return CAM_OK;
}

CAM_API CamError CAM_CALL Cam_GetFrameSync(CameraHandle handle, uint16_t sensorId,
                                           FrameInfo* pFrameInfo, void* pDataBuf,
                                           uint32_t bufSize, uint32_t timeoutMs) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (!pFrameInfo || !pDataBuf || bufSize == 0) return CAM_ERR_INVALID_PARAM;
    if (sensorId < 1 || sensorId > 4) return CAM_ERR_INVALID_PARAM;
    if (!dev->dispatcher()) return CAM_ERR_NOT_INITIALIZED;

    FramePacket pkt;
    if (!dev->dispatcher()->wait_latest(sensorId, &pkt, static_cast<int>(timeoutMs)))
        return CAM_ERR_TIMEOUT;
    if (static_cast<uint32_t>(pkt.data.size()) > bufSize)
        return CAM_ERR_BUFFER_TOO_SMALL;

    std::memcpy(pDataBuf, pkt.data.constData(), static_cast<size_t>(pkt.data.size()));
    frame_to_info(pkt, *pFrameInfo);
    return CAM_OK;
}

// =========================================================================
// 4.10 异步非阻塞控制 API（立即返回 CAM_PENDING，结果经 CommandResultCallback 通知）
//     callback 在 SDK CommandManager 线程执行，绝不运行在 UDP / UI 线程。
// =========================================================================

CAM_API CamError CAM_CALL Cam_StartCaptureAsync(CameraHandle handle, uint16_t sensorId,
                                                uint32_t timeoutMs, CommandResultCallback callback,
                                                void* pUserData) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (!callback) return CAM_ERR_INVALID_PARAM;
    return dev->adapter()->start_capture_async(sensorId, timeoutMs, callback, pUserData);
}

CAM_API CamError CAM_CALL Cam_StopCaptureAsync(CameraHandle handle, uint16_t sensorId,
                                               uint32_t timeoutMs, CommandResultCallback callback,
                                               void* pUserData) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (!callback) return CAM_ERR_INVALID_PARAM;
    return dev->adapter()->stop_capture_async(sensorId, timeoutMs, callback, pUserData);
}

CAM_API CamError CAM_CALL Cam_SetSensorConfigAsync(CameraHandle handle, uint16_t sensorId,
                                                   const SensorConfig* pConfig, uint32_t timeoutMs,
                                                   CommandResultCallback callback, void* pUserData) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (!pConfig || !callback) return CAM_ERR_INVALID_PARAM;
    return dev->adapter()->set_sensor_config_async(sensorId, *pConfig, timeoutMs, callback, pUserData);
}

CAM_API CamError CAM_CALL Cam_GetSensorConfigAsync(CameraHandle handle, uint16_t sensorId,
                                                   SensorConfig* pConfig, uint32_t timeoutMs,
                                                   CommandResultCallback callback, void* pUserData) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (!pConfig || !callback) return CAM_ERR_INVALID_PARAM;
    return dev->adapter()->get_sensor_config_async(sensorId, pConfig, timeoutMs, callback, pUserData);
}

CAM_API CamError CAM_CALL Cam_SetTriggerConfigAsync(CameraHandle handle, uint16_t sensorId,
                                                    const TriggerConfig* pConfig, uint32_t timeoutMs,
                                                    CommandResultCallback callback, void* pUserData) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (!pConfig || !callback) return CAM_ERR_INVALID_PARAM;
    return dev->adapter()->set_trigger_config_async(sensorId, *pConfig, timeoutMs, callback, pUserData);
}

CAM_API CamError CAM_CALL Cam_GetTriggerConfigAsync(CameraHandle handle, uint16_t sensorId,
                                                    TriggerConfig* pConfig, uint32_t timeoutMs,
                                                    CommandResultCallback callback, void* pUserData) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (!pConfig || !callback) return CAM_ERR_INVALID_PARAM;
    return dev->adapter()->get_trigger_config_async(sensorId, pConfig, timeoutMs, callback, pUserData);
}

CAM_API CamError CAM_CALL Cam_SetBatchConfigAsync(CameraHandle handle, uint16_t sensorId,
                                                  const BatchConfig* pConfig, uint32_t timeoutMs,
                                                  CommandResultCallback callback, void* pUserData) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (!pConfig || !callback) return CAM_ERR_INVALID_PARAM;
    return dev->adapter()->set_batch_config_async(sensorId, *pConfig, timeoutMs, callback, pUserData);
}

CAM_API CamError CAM_CALL Cam_GetBatchConfigAsync(CameraHandle handle, uint16_t sensorId,
                                                  BatchConfig* pConfig, uint32_t timeoutMs,
                                                  CommandResultCallback callback, void* pUserData) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (!pConfig || !callback) return CAM_ERR_INVALID_PARAM;
    return dev->adapter()->get_batch_config_async(sensorId, pConfig, timeoutMs, callback, pUserData);
}

CAM_API CamError CAM_CALL Cam_StartBatchAsync(CameraHandle handle, uint16_t sensorId, uint32_t batchId,
                                              uint32_t timeoutMs, CommandResultCallback callback,
                                              void* pUserData) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (!callback) return CAM_ERR_INVALID_PARAM;
    return dev->adapter()->start_batch_async(sensorId, batchId, timeoutMs, callback, pUserData);
}

CAM_API CamError CAM_CALL Cam_StopBatchAsync(CameraHandle handle, uint16_t sensorId,
                                             uint32_t timeoutMs, CommandResultCallback callback,
                                             void* pUserData) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (!callback) return CAM_ERR_INVALID_PARAM;
    return dev->adapter()->stop_batch_async(sensorId, timeoutMs, callback, pUserData);
}

CAM_API CamError CAM_CALL Cam_ResetBufferAsync(CameraHandle handle, uint16_t sensorId,
                                               uint32_t timeoutMs, CommandResultCallback callback,
                                               void* pUserData) {
    QMutexLocker locker(&g_mutex);
    CameraDevice* dev = device_for(handle);
    if (!dev) return CAM_ERR_INVALID_HANDLE;
    if (!callback) return CAM_ERR_INVALID_PARAM;
    return dev->adapter()->reset_buffer_async(sensorId, timeoutMs, callback, pUserData);
}
