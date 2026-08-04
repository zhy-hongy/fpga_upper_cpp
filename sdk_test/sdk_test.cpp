#include "sdk_test.h"

#include <QByteArray>

#include "sdk_test/mock/mock_protocol_handler.h"

SDKTest::SDKTest(ProtocolHandler* protocol, const Config& config,
                 bool mockMode, QObject* parent)
    : QThread(parent), m_protocol(protocol), m_config(config), m_mock_mode(mockMode) {
    if (m_mock_mode) {
        // Mock 模式：创建独立 MockProtocolHandler。构造在宿主主线程（on_sdk_test 内），
        // 基类不启动 → 无 socket；生命周期归 SDKTest，结束时 deleteLater 到主线程。
        m_mock = new MockProtocolHandler(config);
    }
}

SDKTest::~SDKTest() {
    // 线程已由 finished → deleteLater 清理；此处只做对象析构。
    // Mock 生命周期：投递到其所属线程（宿主主线程）事件循环释放。
    if (m_mock) {
        m_mock->deleteLater();
        m_mock = nullptr;
    }
}

void SDKTest::request_stop() {
    m_stop.store(true);
}

void SDKTest::log_line(const QString& line) {
    emit log(line);
}

void CAM_CALL SDKTest::on_data(const FrameInfo* info, const void* data, void* user) {
    Q_UNUSED(data);
    auto* self = static_cast<SDKTest*>(user);
    if (self) self->on_frame_received(info);
}

void SDKTest::on_frame_received(const FrameInfo* info) {
    if (!info) return;
    ++m_frame_count;
    // 运行在 SDK CallbackWorker 线程：只发信号，不操作 UI。
    const quintptr tid = reinterpret_cast<quintptr>(QThread::currentThreadId());
    if (m_mock_mode) {
        // Mock 模式：验证 DataHandler 运行线程 ≠ SDKTest 线程，且帧内容在变化。
        log_line(QString("[SDK CALLBACK] th=0x%1  frameId=%2  sensor=%3  %4x%5  size=%6  (回调线程≠测试线程: %7)")
                     .arg(tid, 0, 16)
                     .arg(info->frameId)
                     .arg(info->sensorId)
                     .arg(info->width)
                     .arg(info->height)
                     .arg(info->dataSize)
                     .arg(tid != m_test_thread_id ? "是" : "否！"));
        if (m_frame_count.load() == 1) {
            log_line(QString("[MOCK FRAME] 首帧示例: frameId=%1 sensor=%2 %3x%4 size=%5")
                         .arg(info->frameId).arg(info->sensorId)
                         .arg(info->width).arg(info->height).arg(info->dataSize));
        }
    } else {
        log_line(QString("   [CBK] th=0x%1  frameId=%2  sensor=%3  %4x%5  ts=%6  size=%7")
                     .arg(tid, 0, 16)
                     .arg(info->frameId)
                     .arg(info->sensorId)
                     .arg(info->width)
                     .arg(info->height)
                     .arg(info->timestamp)
                     .arg(info->dataSize));
    }
}

void SDKTest::run() {
    m_test_thread_id = reinterpret_cast<quintptr>(QThread::currentThreadId());
    log_line(QString("════════ SDKTest 启动（测试线程 th=0x%1，模式=%2）════════")
                 .arg(m_test_thread_id, 0, 16)
                 .arg(m_mock_mode ? "MOCK" : "REAL"));

    // 1) SDK 初始化
    CamError e = Cam_Initialize();
    log_line(QString("1. Cam_Initialize()                      -> %1").arg(int(e)));
    if (e != CAM_OK) { log_line("初始化失败，测试中止"); return; }

    // 2) 连接 ProtocolHandler：Mock 模式用 MockProtocolHandler，真实模式用宿主已有
    ProtocolHandler* attach = m_mock_mode ? static_cast<ProtocolHandler*>(m_mock) : m_protocol;
    log_line(QString("2. Cam_AttachProtocolHandler(0x%1)  [%2]")
                 .arg(reinterpret_cast<quintptr>(attach), 0, 16)
                 .arg(m_mock_mode ? "MOCK" : "REAL"));
    e = Cam_AttachProtocolHandler(attach);
    log_line(QString("   Cam_AttachProtocolHandler()           -> %1").arg(int(e)));
    if (e != CAM_OK) { log_line("Attach 失败，测试中止"); Cam_Finalize(); return; }

    // 3) 打开设备
    CameraHandle handle = nullptr;
    e = Cam_OpenDevice(0, &handle);
    log_line(QString("3. Cam_OpenDevice(0)                     -> %1   handle=0x%2")
                 .arg(int(e))
                 .arg(reinterpret_cast<quintptr>(handle), 0, 16));
    if (e != CAM_OK) { log_line("打开设备失败，测试中止"); Cam_Finalize(); return; }
    m_handle = handle;

    // 4) 获取设备信息
    DeviceInformation info;
    e = Cam_GetDeviceInformation(handle, &info);
    log_line(QString("4. Cam_GetDeviceInformation()            -> %1").arg(int(e)));
    if (e == CAM_OK) {
        log_line(QString("     device status = %1").arg(info.status));
        const QString ver = QString::fromLatin1(info.fpgaVersion);
        if (m_mock_mode)
            log_line(QString("     fpgaVersion   = \"%1\"  (SDK 无版本命令；Mock 期望 = \"%2\")")
                         .arg(ver, m_mock->mock_fpga_version()));
        else
            log_line(QString("     fpgaVersion   = \"%1\"  (当前协议无读取命令时为空)").arg(ver));
    }

    // 5) 注册数据回调
    e = Cam_RegisterDataHandler(handle, &SDKTest::on_data, this);
    log_line(QString("5. Cam_RegisterDataHandler()             -> %1").arg(int(e)));
    if (e != CAM_OK) {
        log_line("注册回调失败，测试中止");
        Cam_CloseDevice(handle);
        Cam_Finalize();
        return;
    }

    // 6) 控制命令 + 采集
    if (m_mock_mode) {
        // Mock 模式：基类传输未启动 → 控制命令预期 CAM_ERR_TIMEOUT（不触碰 UDP）。
        log_line("[MOCK] 控制命令走真实(未启动)传输，预期 CAM_ERR_TIMEOUT");
        SensorConfig cfg{};
        cfg.exposureTime = 2000; cfg.gain = 3; cfg.exposureDelay = 100; cfg.sampleRate = 3000;
        e = Cam_SetSensorConfig(handle, 1, &cfg);
        log_line(QString("6a. Cam_SetSensorConfig(exp=2000,gain=3) -> %1").arg(int(e)));

        SensorConfig got{};
        e = Cam_GetSensorConfig(handle, 1, &got);
        log_line(QString("6b. Cam_GetSensorConfig()                -> %1").arg(int(e)));

        log_line("6c. Cam_StartCapture(handle, 0)  广播 1~4（MOCK 预期 TIMEOUT）...");
        e = Cam_StartCapture(handle, 0);
        log_line(QString("    Cam_StartCapture()                  -> %1").arg(int(e)));
        log_line("[MOCK] 显式启动 Mock 帧生成（40ms/帧，sensor 1~4 循环）");
        m_mock->start_mock_frames();
    } else {
        // 真实模式：sensorId=0 广播 1~4；FPGA 未连接时各等 ACK 约 4×4s
        log_line("6. Cam_StartCapture(handle, 0)  广播 1~4（FPGA 未连接时约等 16s）...");
        e = Cam_StartCapture(handle, 0);
        log_line(QString("   Cam_StartCapture()                    -> %1").arg(int(e)));
    }

    // 7) 收集回调约 2 秒
    log_line("等待帧回调（2 秒）...");
    for (int i = 0; i < 20; ++i) {
        QThread::msleep(100);
        if (m_stop.load()) break;
    }
    log_line(QString("   [TEST] 累计回调帧数 = %1").arg(m_frame_count.load()));

    // 8) 同步取帧：周期调用（本测试线程即"定时器线程"）
    log_line("7. Cam_GetFrameSync() 周期取帧（间隔 500ms × 5 次）");
    QByteArray buf(4 * 1024 * 1024, '\0');
    for (int i = 0; i < 5; ++i) {
        if (m_stop.load()) break;
        FrameInfo finfo;
        e = Cam_GetFrameSync(handle, 1, &finfo, buf.data(),
                             static_cast<uint32_t>(buf.size()), 1000);
        if (e == CAM_OK) {
            log_line(QString("   [SYNC] th=0x%1  frameId=%2  %3x%4  size=%5")
                         .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
                         .arg(finfo.frameId)
                         .arg(finfo.width)
                         .arg(finfo.height)
                         .arg(finfo.dataSize));
        } else {
            log_line(QString("   [SYNC] 取帧失败 -> %1").arg(int(e)));
        }
        QThread::msleep(500);
    }

    // 9) 停止流程
    if (m_mock_mode) {
        log_line("8. Cam_StopCapture(0)  广播 1~4（MOCK 预期 TIMEOUT）...");
        e = Cam_StopCapture(handle, 0);
        log_line(QString("   Cam_StopCapture()                    -> %1").arg(int(e)));
        log_line(QString("[MOCK] 停止 Mock 帧生成（已生成 %1 帧）").arg(m_mock->generated_frames()));
        m_mock->stop_mock_frames();
    } else {
        e = Cam_StopCapture(handle, 0);
        log_line(QString("8. Cam_StopCapture(0)                    -> %1").arg(int(e)));
    }
    e = Cam_UnregisterDataHandler(handle);
    log_line(QString("   Cam_UnregisterDataHandler()           -> %1").arg(int(e)));
    e = Cam_CloseDevice(handle);
    log_line(QString("   Cam_CloseDevice()                     -> %1").arg(int(e)));
    m_handle = nullptr;
    e = Cam_Finalize();
    log_line(QString("   Cam_Finalize()                        -> %1").arg(int(e)));

    log_line("════════ SDKTest 结束（CallbackWorker 已随设备关闭退出，无线程泄漏）════════");
}
