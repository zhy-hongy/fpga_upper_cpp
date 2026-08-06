#ifndef PHOTON_CAMERA_H
#define PHOTON_CAMERA_H

#include <cstdint>

// =========================================================================
// 1. DLL 导出/导入宏与调用约定
// =========================================================================
#if defined(_WIN32) || defined(_WIN64)
    #ifdef PHOTONSDK_EXPORTS
        #define CAM_API __declspec(dllexport)
    #else
        #define CAM_API __declspec(dllimport)
    #endif
    #define CAM_CALL __stdcall
#else
    #define CAM_API __attribute__((visibility("default")))
    #define CAM_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 设备句柄 (指向 SDK 内部 CameraDevice 对象，不代表宿主 ProtocolHandler)
typedef void* CameraHandle;

// =========================================================================
// 2. 状态码与通用枚举
// =========================================================================

// 统一错误码。
// 数值约定：
//   - 与 FPGA 协议错误码语义相同的项，使用与 FPGA 完全一致的数值（见各行注释）。
//   - SDK 自身定义的错误码（FPGA 没有对应项）从已有码值之后按正数递增，不使用负数。
//   - CAM_PENDING 不是错误，表示异步命令已提交、结果经回调通知。
enum CamError : int32_t {
    // ── 与 FPGA 协议错误码一一对应（相同语义 = 相同数值）──
    CAM_OK                   = 0,   // 成功（FPGA 协议 0x0000）
    CAM_ERR_CRC_FAILED       = 1,   // CRC 校验失败（FPGA 协议 0x0001）
    CAM_ERR_INVALID_PARAM    = 2,   // 参数错误（FPGA 协议 0x0002）
    CAM_ERR_INVALID_FUNCTION = 6,   // 功能码错误：命令功能码 FPGA 不支持（FPGA 协议 0x0006）
    CAM_ERR_DEVICE_BUSY      = 7,   // 设备忙（FPGA 协议 0x0007）
    CAM_ERR_INVALID_LENGTH   = 8,   // 消息长度错误：命令/应答长度与协议不符（FPGA 协议 0x0008）

    // ── SDK 专用错误码（无 FPGA 对应项，按正数递增）──
    CAM_ERR_UNKNOWN          = 3,   // 未知错误（SDK 专用）
    CAM_ERR_NOT_INITIALIZED  = 4,   // SDK 未初始化：需先调用 Cam_Initialize（SDK 专用）
    CAM_ERR_INVALID_HANDLE   = 5,   // 无效的相机句柄：传入句柄为空或已被关闭（SDK 专用）
    CAM_ERR_NO_DEVICE        = 9,   // 指定 Sensor/设备不存在或离线（SDK 专用）
    CAM_ERR_DISCONNECTED     = 10,  // 网络连接断开（SDK 专用）
    CAM_ERR_TIMEOUT          = 11,  // 通信超时：等待 FPGA ACK 或同步数据超时（SDK 专用）
    CAM_ERR_BUFFER_TOO_SMALL = 12,  // 缓冲区不足：调用方提供的缓冲区小于所需大小（SDK 专用）
    CAM_ERR_NOT_SUPPORTED    = 13,  // 不支持的功能：协议未实现或设备类型不支持（SDK 专用）

    CAM_PENDING              = 100  // 异步命令已提交，结果经回调通知（非阻塞，非错误）
};

// 设备运行状态
enum DeviceRunState : uint32_t {
    STATE_IDLE      = 0,            // 空闲
    STATE_READY     = 1,            // 就绪
    STATE_RUNNING   = 2,            // 采集中
    STATE_STOPPING  = 3,            // 停止中
    STATE_ERROR     = 4             // 错误
};

// 数据类型
enum PixelFormat : uint16_t {
    PIXEL_FMT_MONO8          = 0x01, // 2D 灰度图 (0x0301)
    PIXEL_FMT_CENTERLINE_F32 = 0x10, // 激光中心线 (0x0302)
    PIXEL_FMT_POINTCLOUD_XYZ = 0x20  // 3D 点云 (0x0303)
};

#pragma pack(push, 1)

// =========================================================================
// 3. 结构体定义 (SDK 高层 API 数据抽象层)
// =========================================================================

// 3.1 主控板网络配置
struct EthernetConfig {
    uint32_t deviceIp;              // 设备 IP
    uint32_t pcIp;                  // 本机/PC IP
    uint32_t subnetMask;            // 子网掩码
    uint32_t gateway;               // 默认网关
    uint8_t  mac[6];                // MAC 地址
};

// 3.2 主控板信息 (仅 FPGA 主控硬件信息)
struct DeviceInformation {
    uint32_t       deviceSN;        // 主控硬件 SN
    char           fpgaVersion[8];  // FPGA 固件版本
    EthernetConfig network;         // 网络参数
    uint32_t       status;          // FPGA 基础状态
};

// 3.3 Sensor 硬件信息 (只读)
struct SensorInformation {
    uint16_t sensorId;              // Sensor 位置编号 (1~4)
    uint32_t sensorSN;              // Sensor 硬件 SN
    char     firmwareVersion[8];    // Sensor 固件版本
    char     model[8];              // 相机型号 (如 "CAM-3D01")
};

// 3.4 Sensor 核心基础配置
struct SensorConfig {
    // 曝光与增益
    uint32_t exposureTime;          // 曝光时间 (us)
    uint32_t gain;                  // 曝光增益
    uint32_t exposureDelay;         // 曝光延迟

    // 图像 ROI
    uint16_t roiStart;              // ROI 起始列
    uint16_t roiWidth;              // ROI 宽度

    // 物理与测量参数
    float    pixelPitch;            // X 轴像素尺寸
    uint16_t zMin;                  // Z 轴测量范围 Min
    uint16_t zMax;                  // Z 轴测量范围 Max

    // 采样
    uint32_t sampleRate;            // FPS / LineRate
};

// 3.5 触发与控制配置
struct TriggerConfig {
    uint16_t triggerMode;           // 1:连续, 2:编码器, 3:外部IO
    uint16_t encoderType;           // 1:差分, 2:单端
    uint16_t encoderInputMode;      // 1:4倍频, 2:2倍频, 3:1倍频
    uint32_t encoderStep;           // 触发间隔脉冲数
    uint16_t externalEnable;        // 外部触发使能 (0:关, 1:开)
    uint16_t reserved;              // 协议预留填充
};

// 3.6 批处理配置
struct BatchConfig {
    uint16_t length;                // 批处理行数
};

// 3.7 Sensor 专属标定数据块 (128 字节二进制块)
struct CalibrationData {
    uint8_t  data[128];             // 128 字节标定矩阵二进制块
};

// 3.8 设备搜索列表
struct DeviceList {
    uint32_t count;                 // 搜索到的 FPGA 网关数量
    DeviceInformation devices[16];  // 主控板列表
};

// 3.9 多通道异步数据回调头信息 (包含 sensorId 标识数据源通道)
struct FrameInfo {
    uint32_t    frameId;            // 全局帧号
    uint16_t    sensorId;           // 数据来源 Sensor 编号 (1~4)
    uint32_t    batchId;            // 当前批次编号
    uint32_t    lineIndex;          // 批次内行号 (0 ~ N-1)
    uint64_t    encoder;            // 触发瞬间的编码器绝对脉冲值
    uint64_t    timestamp;          // 时间戳 (us)
    uint16_t    width;              // 列数 / 点数
    uint16_t    height;             // 行数
    uint32_t    dataSize;           // 载荷大小 (Bytes)
    PixelFormat dataType;           // 数据类型 (图像/中心线/点云)
};

#pragma pack(pop)

// 通用数据回调函数指针
//   pFrameInfo: 帧头信息（含 sensorId、尺寸、时间戳等），SDK 回调期间有效
//   pData:      帧载荷数据指针（dataSize 字节），SDK 回调期间有效
//   pUserData:  调用方透传指针（Cam_RegisterDataHandler 传入）
typedef void(CAM_CALL *DataHandler)(
    const FrameInfo* pFrameInfo,
    const void* pData,
    void* pUserData
);

// 异步控制命令结果回调（全异步非阻塞 API 使用）
//   result:     CAM_OK（ACK 正常返回）/ CAM_ERR_TIMEOUT（超时）/ CAM_ERR_UNKNOWN（投递失败等）
//   commandId:  本次命令唯一 ID（由 SDK 分配，与调用方无关，仅用于日志/关联）
//   pUserData:  调用方透传指针
// 注意：回调在 SDK 内部 CommandManager 线程执行，绝不运行在 UDP / UI / 调用线程。
typedef void(CAM_CALL *CommandResultCallback)(
    CamError result,
    uint16_t commandId,
    void* pUserData
);

// =========================================================================
// 4. SDK API 导出函数
// =========================================================================

// ── 4.1 环境与网关发现 ──────────────────────────────────────────────

/**
 * @brief 初始化 SDK。
 *
 * 必须在调用任何其他 API 之前调用一次。内部会创建全局运行环境
 * （如 Qt 核心环境），并注册内部数据类型。
 *
 * @return CamError
 *   - CAM_OK: 初始化成功（重复调用也返回 CAM_OK，幂等）
 */
CAM_API CamError CAM_CALL Cam_Initialize();

/**
 * @brief 释放 SDK 全部资源。
 *
 * 关闭全部已打开设备、停止独立 SDK 模式的内部通信线程、销毁全局运行环境。
 * 建议在进程退出前调用；未初始化时调用返回 CAM_OK（幂等）。
 *
 * @return CamError
 *   - CAM_OK: 释放成功
 */
CAM_API CamError CAM_CALL Cam_Finalize();

/**
 * @brief 获取 SDK 版本字符串。
 *
 * 返回的字符串由 SDK 内部静态存储，调用方无需（也不应）释放。
 *
 * @return const char*: 版本号字符串（如 "1.0.0"）
 */
CAM_API const char* CAM_CALL Cam_GetSdkVersion();

/**
 * @brief 枚举当前可发现的 FPGA 网关（主控板）。
 *
 * 宿主注入模式：上报已注入的 1 台网关（0 台时返回成功但 count=0）。
 * 独立 SDK 模式：若已打开独立设备，上报 1 台。
 *
 * @param pList      [out] 设备列表（count + 主控信息数组），内存由调用方分配
 * @param timeoutMs  枚举等待时间（毫秒），建议 1000
 * @return CamError
 *   - CAM_OK: 枚举成功（pList->count 为实际数量，可为 0）
 *   - CAM_ERR_NOT_INITIALIZED: 未调用 Cam_Initialize
 *   - CAM_ERR_INVALID_PARAM: pList 为空
 */
CAM_API CamError CAM_CALL Cam_EnumDevices(DeviceList* pList, uint32_t timeoutMs);

// ── 4.2 宿主集成与网关连接 ─────────────────────────────────────────

/**
 * @brief 宿主注入模式：将宿主已创建的通信处理器借给 SDK。
 *
 * 宿主 App 初始化完成后，将已有 ProtocolHandler* 指针传入 SDK。
 * SDK 只保存借用指针：不负责创建、不负责释放，生命周期由宿主管理。
 *
 * @param protocolHandler 宿主 ProtocolHandler* 指针（以 void* 传入）
 * @return CamError
 *   - CAM_OK: 注入成功
 *   - CAM_ERR_INVALID_PARAM: protocolHandler 为空
 *   - CAM_ERR_NOT_INITIALIZED: 未调用 Cam_Initialize
 *   - CAM_ERR_DEVICE_BUSY: 已有独立 SDK 设备打开（互斥），或已注入过
 */
CAM_API CamError CAM_CALL Cam_AttachProtocolHandler(void* protocolHandler);

// ── 4.3 网关连接与主控信息（宿主注入模式）─────────────────────────

/**
 * @brief 宿主模式：按序号打开设备。
 *
 * 系统仅支持 1 台网关，index 固定传 0。
 *
 * @param index   设备序号（0）
 * @param pHandle [out] 相机句柄，成功后由 SDK 写入
 * @return CamError
 *   - CAM_OK: 打开成功
 *   - CAM_ERR_NOT_INITIALIZED: 未初始化
 *   - CAM_ERR_NO_DEVICE: 未注入 ProtocolHandler，或 index 越界
 *   - CAM_ERR_DEVICE_BUSY: 已有一台设备打开（单设备限制）
 *   - CAM_ERR_INVALID_PARAM: pHandle 为空
 */
CAM_API CamError CAM_CALL Cam_OpenDevice(uint32_t index, CameraHandle* pHandle);

/**
 * @brief 宿主模式：按 IP 打开设备。
 *
 * @param ipAddress 目标网关 IP 字符串（如 "192.168.10.10"）
 * @param pHandle   [out] 相机句柄，成功后由 SDK 写入
 * @return CamError
 *   - CAM_OK: 打开成功
 *   - CAM_ERR_NOT_INITIALIZED: 未初始化
 *   - CAM_ERR_NO_DEVICE: 未注入 ProtocolHandler
 *   - CAM_ERR_DEVICE_BUSY: 已有一台设备打开（单设备限制）
 *   - CAM_ERR_INVALID_PARAM: ipAddress 或 pHandle 为空
 */
CAM_API CamError CAM_CALL Cam_OpenDeviceByIP(const char* ipAddress, CameraHandle* pHandle);

/**
 * @brief 关闭并释放设备。
 *
 * 若为独立 SDK 模式设备，会先停止内部通信线程（停止收帧），再销毁设备对象。
 * 注意：不能在 SDK 数据回调线程 / 异步命令回调线程内调用本函数。
 *
 * @param handle 相机句柄
 * @return CamError
 *   - CAM_OK: 关闭成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 */
CAM_API CamError CAM_CALL Cam_CloseDevice(CameraHandle handle);

/**
 * @brief 获取主控板信息（SN、固件版本、网络配置、状态）。
 *
 * @param handle 相机句柄
 * @param pInfo  [out] 主控板信息，内存由调用方分配
 * @return CamError
 *   - CAM_OK: 获取成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pInfo 为空
 */
CAM_API CamError CAM_CALL Cam_GetDeviceInformation(CameraHandle handle, DeviceInformation* pInfo);

/**
 * @brief 获取设备运行状态。
 *
 * @param handle 相机句柄
 * @param pState [out] 运行状态（见 DeviceRunState）
 * @return CamError
 *   - CAM_OK: 获取成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pState 为空
 */
CAM_API CamError CAM_CALL Cam_GetRunState(CameraHandle handle, DeviceRunState* pState);

// ── 4.3b 独立 SDK 模式（无宿主注入，SDK 内部自建唯一一套通信）───────

/**
 * @brief 独立 SDK 模式：直接连接 FPGA，不依赖上位机。
 *
 * 第三方直接调用 PhotonCameraSDK.dll 连接 FPGA，无需 fpga_upper.exe、
 * 无需调用 Cam_AttachProtocolHandler()：
 *   - SDK 内部创建唯一的通信处理器 ProtocolHandler
 *     （一个 UDPWorker + 一个 UDPDataThread），端口映射与宿主一致：
 *     控制 socket 绑定 controlPort，数据 socket 绑定 dataPort，
 *     控制命令发往 fpgaIp:dataPort。
 *   - 控制/数据流与宿主模式完全一致：异步 ACK（CommandManager）
 *     与数据帧（DataDispatcher → CallbackWorker → DataHandler）。
 *   - 与宿主注入模式互斥：已有宿主 ProtocolHandler（Cam_AttachProtocolHandler）
 *     或已有打开设备时返回 CAM_ERR_DEVICE_BUSY。
 *
 * @param fpgaIp      FPGA 设备 IP 字符串（如 "192.168.10.10"）
 * @param controlPort 本地控制 UDP 绑定端口（默认 5000）
 * @param dataPort    本地数据 UDP 绑定端口 + FPGA 目标端口（默认 60000）
 * @param pHandle     [out] 相机句柄，成功后由 SDK 写入
 * @return CamError
 *   - CAM_OK: 打开成功
 *   - CAM_ERR_NOT_INITIALIZED: 未初始化
 *   - CAM_ERR_INVALID_PARAM: fpgaIp 为空、controlPort/dataPort 为 0，或 pHandle 为空
 *   - CAM_ERR_DEVICE_BUSY: 已有宿主注入或已打开设备（互斥）
 *   - CAM_ERR_UNKNOWN: 内部通信线程启动失败
 */
CAM_API CamError CAM_CALL Cam_OpenNetworkDevice(const char* fpgaIp, uint16_t controlPort,
                                                uint16_t dataPort, CameraHandle* pHandle);

// ── 4.3c 测试专用 Mock 设备（TEST-ONLY）─────────────────────────────

/**
 * @brief [TEST-ONLY] 打开测试专用 Mock 设备（无 FPGA 环境验证用）。
 *
 * 与 Cam_OpenNetworkDevice 同签名，但 SDK 内部创建 MockProtocolHandler：
 * 不绑定任何 UDP socket、不产生任何网络收发；控制命令立即回调成功 ACK，
 * 调用 Cam_StartCapture 后按固定参数（1280×1024、sensor=1）生成模拟帧。
 * 仅供 sdk_standalone_test --mock 等测试使用。
 *
 * @param fpgaIp      任意占位 IP（Mock 模式不实际连接）
 * @param controlPort 占位控制端口（Mock 模式不绑定）
 * @param dataPort    占位数据端口（Mock 模式不绑定）
 * @param pHandle     [out] 相机句柄，成功后由 SDK 写入
 * @return CamError
 *   - CAM_OK: 打开成功
 *   - CAM_ERR_INVALID_PARAM: pHandle 为空
 *   - CAM_ERR_DEVICE_BUSY: 已有设备打开（互斥）
 *   - CAM_ERR_UNKNOWN: 内部线程启动失败
 */
CAM_API CamError CAM_CALL Cam_OpenNetworkDeviceMock(const char* fpgaIp, uint16_t controlPort,
                                                    uint16_t dataPort, CameraHandle* pHandle);

// ── 4.3d 测试专用：查询 Mock 设备累计"已发送"命令条数（TEST-ONLY）──

/**
 * @brief [TEST-ONLY] 查询 Mock 设备累计记录的"已发送"协议命令条数。
 *
 * 用于验证"未实现协议返回 CAM_ERR_NOT_SUPPORTED 且不发送任何 UDP 命令"。
 * 非 Mock 设备返回 CAM_ERR_NOT_SUPPORTED。
 *
 * @param handle 相机句柄（必须为 Mock 设备）
 * @param pCount [out] 累计已发送命令条数
 * @return CamError
 *   - CAM_OK: 获取成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pCount 为空
 *   - CAM_ERR_NOT_SUPPORTED: 非 Mock 设备
 */
CAM_API CamError CAM_CALL Cam_GetMockSentCommandCount(CameraHandle handle, uint32_t* pCount);

// ── 4.4 Sensor 独立节点查询 (sensorId 范围 1~4) ────────────────────

/**
 * @brief 获取在线 Sensor 数量。
 *
 * @param handle 相机句柄
 * @param pCount [out] 在线 Sensor 数量
 * @return CamError
 *   - CAM_OK: 获取成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pCount 为空
 */
CAM_API CamError CAM_CALL Cam_GetSensorCount(CameraHandle handle, uint32_t* pCount);

/**
 * @brief 获取 Sensor 硬件信息（SN、固件版本、型号）。
 *
 * @param handle   相机句柄
 * @param sensorId Sensor 编号（1~4）
 * @param pInfo    [out] Sensor 硬件信息，内存由调用方分配
 * @return CamError
 *   - CAM_OK: 获取成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pInfo 为空或 sensorId 超出 1~4
 *   - CAM_ERR_NOT_SUPPORTED: 协议未实现
 */
CAM_API CamError CAM_CALL Cam_GetSensorInformation(CameraHandle handle, uint16_t sensorId, SensorInformation* pInfo);

// ── 4.5 Sensor 参数读写 ────────────────────────────────────────────

/**
 * @brief 写 Sensor 基础配置（曝光/增益/ROI/采样率等）。
 *
 * 注意：ROI（roiStart/roiWidth）、物理测量参数（pixelPitch/zMin/zMax）
 * 协议未实现，设置这些字段会返回 CAM_ERR_NOT_SUPPORTED 且不发送任何命令。
 *
 * @param handle   相机句柄
 * @param sensorId Sensor 编号（1~4）
 * @param pConfig  待写入的配置，内存由调用方分配
 * @return CamError
 *   - CAM_OK: 设置成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pConfig 为空
 *   - CAM_ERR_NOT_SUPPORTED: 含未实现的 ROI/物理测量字段
 */
CAM_API CamError CAM_CALL Cam_SetSensorConfig(CameraHandle handle, uint16_t sensorId, const SensorConfig* pConfig);

/**
 * @brief 读 Sensor 基础配置。
 *
 * @param handle   相机句柄
 * @param sensorId Sensor 编号（1~4）
 * @param pConfig  [out] 读回的配置，内存由调用方分配
 * @return CamError
 *   - CAM_OK: 读取成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pConfig 为空
 */
CAM_API CamError CAM_CALL Cam_GetSensorConfig(CameraHandle handle, uint16_t sensorId, SensorConfig* pConfig);

// ── 4.6 Trigger & Batch 控制 ────────────────────────────────────────

/**
 * @brief 写触发与控制配置（触发模式、编码器参数、外部触发使能）。
 *
 * @param handle   相机句柄
 * @param sensorId Sensor 编号（1~4）
 * @param pConfig  待写入的触发配置
 * @return CamError
 *   - CAM_OK: 设置成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pConfig 为空
 */
CAM_API CamError CAM_CALL Cam_SetTriggerConfig(CameraHandle handle, uint16_t sensorId, const TriggerConfig* pConfig);

/**
 * @brief 读触发与控制配置。
 *
 * @param handle   相机句柄
 * @param sensorId Sensor 编号（1~4）
 * @param pConfig  [out] 读回的触发配置
 * @return CamError
 *   - CAM_OK: 读取成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pConfig 为空
 */
CAM_API CamError CAM_CALL Cam_GetTriggerConfig(CameraHandle handle, uint16_t sensorId, TriggerConfig* pConfig);

/**
 * @brief 写批处理配置（批处理行数）。
 *
 * @param handle   相机句柄
 * @param sensorId Sensor 编号（1~4）
 * @param pConfig  待写入的批处理配置
 * @return CamError
 *   - CAM_OK: 设置成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pConfig 为空
 */
CAM_API CamError CAM_CALL Cam_SetBatchConfig(CameraHandle handle, uint16_t sensorId, const BatchConfig* pConfig);

/**
 * @brief 读批处理配置。
 *
 * @param handle   相机句柄
 * @param sensorId Sensor 编号（1~4）
 * @param pConfig  [out] 读回的批处理配置
 * @return CamError
 *   - CAM_OK: 读取成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pConfig 为空
 */
CAM_API CamError CAM_CALL Cam_GetBatchConfig(CameraHandle handle, uint16_t sensorId, BatchConfig* pConfig);

// ── 4.7 标定矩阵独立读写 ───────────────────────────────────────────

/**
 * @brief 写 Sensor 标定矩阵（128 字节二进制块）。
 *
 * 协议未实现，一律返回 CAM_ERR_NOT_SUPPORTED 且不发送任何命令。
 *
 * @param handle     相机句柄
 * @param sensorId   Sensor 编号（1~4）
 * @param pCalibData 待写入的标定数据
 * @return CamError
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pCalibData 为空
 *   - CAM_ERR_NOT_SUPPORTED: 协议未实现（恒返回）
 */
CAM_API CamError CAM_CALL Cam_WriteCalibration(CameraHandle handle, uint16_t sensorId, const CalibrationData* pCalibData);

/**
 * @brief 读 Sensor 标定矩阵（128 字节二进制块）。
 *
 * 协议未实现，一律返回 CAM_ERR_NOT_SUPPORTED 且不发送任何命令。
 *
 * @param handle     相机句柄
 * @param sensorId   Sensor 编号（1~4）
 * @param pCalibData [out] 读回的标定数据
 * @return CamError
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pCalibData 为空
 *   - CAM_ERR_NOT_SUPPORTED: 协议未实现（恒返回）
 */
CAM_API CamError CAM_CALL Cam_ReadCalibration(CameraHandle handle, uint16_t sensorId, CalibrationData* pCalibData);

// ── 4.8 采集控制 (sensorId=1~4 操控单通道；sensorId=0 表示对所有在线 Sensor 广播) ──

/**
 * @brief 开始采集指定 Sensor（或广播全部）。
 *
 * @param handle   相机句柄
 * @param sensorId Sensor 编号（1~4），0 表示对所有在线 Sensor 广播
 * @return CamError
 *   - CAM_OK: 命令已发出
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 */
CAM_API CamError CAM_CALL Cam_StartCapture(CameraHandle handle, uint16_t sensorId);

/**
 * @brief 停止采集指定 Sensor（或广播全部）。
 *
 * @param handle   相机句柄
 * @param sensorId Sensor 编号（1~4），0 表示对所有在线 Sensor 广播
 * @return CamError
 *   - CAM_OK: 命令已发出
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 */
CAM_API CamError CAM_CALL Cam_StopCapture(CameraHandle handle, uint16_t sensorId);

/**
 * @brief 开始批处理采集。
 *
 * @param handle   相机句柄
 * @param sensorId Sensor 编号（1~4），0 表示对所有在线 Sensor 广播
 * @param batchId  本批次编号（由调用方指定）
 * @return CamError
 *   - CAM_OK: 命令已发出
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 */
CAM_API CamError CAM_CALL Cam_StartBatch(CameraHandle handle, uint16_t sensorId, uint32_t batchId);

/**
 * @brief 停止批处理采集。
 *
 * @param handle   相机句柄
 * @param sensorId Sensor 编号（1~4），0 表示对所有在线 Sensor 广播
 * @return CamError
 *   - CAM_OK: 命令已发出
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 */
CAM_API CamError CAM_CALL Cam_StopBatch(CameraHandle handle, uint16_t sensorId);

/**
 * @brief 复位指定 Sensor 的接收缓冲区。
 *
 * @param handle   相机句柄
 * @param sensorId Sensor 编号（1~4），0 表示对所有在线 Sensor 广播
 * @return CamError
 *   - CAM_OK: 命令已发出
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 */
CAM_API CamError CAM_CALL Cam_ResetBuffer(CameraHandle handle, uint16_t sensorId);

// ── 4.9 多通道异步数据回调与同步接收 ───────────────────────────────

/**
 * @brief 注册数据回调。
 *
 * 注册后，SDK 在收到数据帧时在内部回调线程调用 handler。
 * 重复注册会覆盖上一次的 handler 与 pUserData。
 *
 * @param handle    相机句柄
 * @param handler   数据回调函数指针（传 NULL 等价于注销）
 * @param pUserData 调用方透传指针，回调时原样回传（可为 NULL）
 * @return CamError
 *   - CAM_OK: 注册成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 */
CAM_API CamError CAM_CALL Cam_RegisterDataHandler(CameraHandle handle, DataHandler handler, void* pUserData);

/**
 * @brief 注销数据回调（不再接收数据帧）。
 *
 * @param handle 相机句柄
 * @return CamError
 *   - CAM_OK: 注销成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 */
CAM_API CamError CAM_CALL Cam_UnregisterDataHandler(CameraHandle handle);

/**
 * @brief 同步阻塞读取一帧数据。
 *
 * 将指定 Sensor 最新一帧数据拷贝到调用方缓冲区并返回帧头信息。
 *
 * @param handle    相机句柄
 * @param sensorId  Sensor 编号（1~4）
 * @param pFrameInfo [out] 帧头信息，内存由调用方分配
 * @param pDataBuf  [out] 数据缓冲区，内存由调用方分配
 * @param bufSize   缓冲区大小（Bytes），须 ≥ 实际帧大小
 * @param timeoutMs 等待超时（毫秒）
 * @return CamError
 *   - CAM_OK: 获取成功
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pFrameInfo/pDataBuf 为空，bufSize 为 0，或 sensorId 超出 1~4
 *   - CAM_ERR_TIMEOUT: 超时未取到帧
 *   - CAM_ERR_BUFFER_TOO_SMALL: 缓冲区小于帧大小
 */
CAM_API CamError CAM_CALL Cam_GetFrameSync(CameraHandle handle, uint16_t sensorId, FrameInfo* pFrameInfo, void* pDataBuf, uint32_t bufSize, uint32_t timeoutMs);

// ── 4.10 异步非阻塞控制 API（立即返回 CAM_PENDING，结果经 CommandResultCallback 通知）──
// 说明：
//   - 调用后不等待 FPGA ACK，立即返回 CAM_PENDING；ACK 或超时后回调通知。
//   - timeoutMs 为本次命令超时（毫秒），超时后回调 result=CAM_ERR_TIMEOUT。
//   - Get*Async：调用方传入输出指针，SDK 在 ACK 到达、回调触发前填充其值；
//     调用方需保证该指针在回调前有效。
//   - 回调在 SDK 内部 CommandManager 线程执行，不要在回调内调用 CloseDevice/Finalize。

/**
 * @brief [异步] 开始采集。
 *
 * @param handle    相机句柄
 * @param sensorId  Sensor 编号（1~4），0 表示广播全部
 * @param timeoutMs 命令超时（毫秒）
 * @param callback  结果回调（ACK 成功→CAM_OK，超时→CAM_ERR_TIMEOUT）
 * @param pUserData 调用方透传指针，回调时原样回传（可为 NULL）
 * @return CamError
 *   - CAM_PENDING: 命令已提交，结果经回调通知
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: callback 为空
 */
CAM_API CamError CAM_CALL Cam_StartCaptureAsync(CameraHandle handle, uint16_t sensorId, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);

/**
 * @brief [异步] 停止采集。
 *
 * @param handle    相机句柄
 * @param sensorId  Sensor 编号（1~4），0 表示广播全部
 * @param timeoutMs 命令超时（毫秒）
 * @param callback  结果回调
 * @param pUserData 调用方透传指针（可为 NULL）
 * @return CamError
 *   - CAM_PENDING: 命令已提交，结果经回调通知
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: callback 为空
 */
CAM_API CamError CAM_CALL Cam_StopCaptureAsync(CameraHandle handle, uint16_t sensorId, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);

/**
 * @brief [异步] 写 Sensor 基础配置。
 *
 * @param handle    相机句柄
 * @param sensorId  Sensor 编号（1~4）
 * @param pConfig   待写入的配置（SDK 在提交时读取内容）
 * @param timeoutMs 命令超时（毫秒）
 * @param callback  结果回调
 * @param pUserData 调用方透传指针（可为 NULL）
 * @return CamError
 *   - CAM_PENDING: 命令已提交，结果经回调通知
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pConfig 或 callback 为空
 *   - CAM_ERR_NOT_SUPPORTED: 含未实现的 ROI/物理测量字段（不发送命令）
 */
CAM_API CamError CAM_CALL Cam_SetSensorConfigAsync(CameraHandle handle, uint16_t sensorId, const SensorConfig* pConfig, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);

/**
 * @brief [异步] 读 Sensor 基础配置。
 *
 * @param handle    相机句柄
 * @param sensorId  Sensor 编号（1~4）
 * @param pConfig   [out] 读回配置存放区（须在回调前保持有效）
 * @param timeoutMs 命令超时（毫秒）
 * @param callback  结果回调
 * @param pUserData 调用方透传指针（可为 NULL）
 * @return CamError
 *   - CAM_PENDING: 命令已提交，结果经回调通知
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pConfig 或 callback 为空
 */
CAM_API CamError CAM_CALL Cam_GetSensorConfigAsync(CameraHandle handle, uint16_t sensorId, SensorConfig* pConfig, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);

/**
 * @brief [异步] 写触发与控制配置。
 *
 * @param handle    相机句柄
 * @param sensorId  Sensor 编号（1~4）
 * @param pConfig   待写入的触发配置
 * @param timeoutMs 命令超时（毫秒）
 * @param callback  结果回调
 * @param pUserData 调用方透传指针（可为 NULL）
 * @return CamError
 *   - CAM_PENDING: 命令已提交，结果经回调通知
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pConfig 或 callback 为空
 */
CAM_API CamError CAM_CALL Cam_SetTriggerConfigAsync(CameraHandle handle, uint16_t sensorId, const TriggerConfig* pConfig, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);

/**
 * @brief [异步] 读触发与控制配置。
 *
 * @param handle    相机句柄
 * @param sensorId  Sensor 编号（1~4）
 * @param pConfig   [out] 读回配置存放区（须在回调前保持有效）
 * @param timeoutMs 命令超时（毫秒）
 * @param callback  结果回调
 * @param pUserData 调用方透传指针（可为 NULL）
 * @return CamError
 *   - CAM_PENDING: 命令已提交，结果经回调通知
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pConfig 或 callback 为空
 */
CAM_API CamError CAM_CALL Cam_GetTriggerConfigAsync(CameraHandle handle, uint16_t sensorId, TriggerConfig* pConfig, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);

/**
 * @brief [异步] 写批处理配置。
 *
 * @param handle    相机句柄
 * @param sensorId  Sensor 编号（1~4）
 * @param pConfig   待写入的批处理配置
 * @param timeoutMs 命令超时（毫秒）
 * @param callback  结果回调
 * @param pUserData 调用方透传指针（可为 NULL）
 * @return CamError
 *   - CAM_PENDING: 命令已提交，结果经回调通知
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pConfig 或 callback 为空
 */
CAM_API CamError CAM_CALL Cam_SetBatchConfigAsync(CameraHandle handle, uint16_t sensorId, const BatchConfig* pConfig, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);

/**
 * @brief [异步] 读批处理配置。
 *
 * @param handle    相机句柄
 * @param sensorId  Sensor 编号（1~4）
 * @param pConfig   [out] 读回配置存放区（须在回调前保持有效）
 * @param timeoutMs 命令超时（毫秒）
 * @param callback  结果回调
 * @param pUserData 调用方透传指针（可为 NULL）
 * @return CamError
 *   - CAM_PENDING: 命令已提交，结果经回调通知
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: pConfig 或 callback 为空
 */
CAM_API CamError CAM_CALL Cam_GetBatchConfigAsync(CameraHandle handle, uint16_t sensorId, BatchConfig* pConfig, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);

/**
 * @brief [异步] 开始批处理采集。
 *
 * @param handle    相机句柄
 * @param sensorId  Sensor 编号（1~4），0 表示广播全部
 * @param batchId   本批次编号（由调用方指定）
 * @param timeoutMs 命令超时（毫秒）
 * @param callback  结果回调
 * @param pUserData 调用方透传指针（可为 NULL）
 * @return CamError
 *   - CAM_PENDING: 命令已提交，结果经回调通知
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: callback 为空
 */
CAM_API CamError CAM_CALL Cam_StartBatchAsync(CameraHandle handle, uint16_t sensorId, uint32_t batchId, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);

/**
 * @brief [异步] 停止批处理采集。
 *
 * @param handle    相机句柄
 * @param sensorId  Sensor 编号（1~4），0 表示广播全部
 * @param timeoutMs 命令超时（毫秒）
 * @param callback  结果回调
 * @param pUserData 调用方透传指针（可为 NULL）
 * @return CamError
 *   - CAM_PENDING: 命令已提交，结果经回调通知
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: callback 为空
 */
CAM_API CamError CAM_CALL Cam_StopBatchAsync(CameraHandle handle, uint16_t sensorId, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);

/**
 * @brief [异步] 复位指定 Sensor 接收缓冲区。
 *
 * @param handle    相机句柄
 * @param sensorId  Sensor 编号（1~4），0 表示广播全部
 * @param timeoutMs 命令超时（毫秒）
 * @param callback  结果回调
 * @param pUserData 调用方透传指针（可为 NULL）
 * @return CamError
 *   - CAM_PENDING: 命令已提交，结果经回调通知
 *   - CAM_ERR_INVALID_HANDLE: 句柄无效
 *   - CAM_ERR_INVALID_PARAM: callback 为空
 */
CAM_API CamError CAM_CALL Cam_ResetBufferAsync(CameraHandle handle, uint16_t sensorId, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);

#ifdef __cplusplus
}
#endif

#endif // PHOTON_CAMERA_H
