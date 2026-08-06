#ifndef PHOTON_CAMERA_H
#define PHOTON_CAMERA_H

#include <cstdint>

// =========================================================================
// 1. DLL 导出/导入宏
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

// 设备句柄。由 Cam_OpenNetworkDevice 等函数创建，Cam_CloseDevice 释放。
// 句柄仅代表已打开的设备，不代表底层的网络连接。
typedef void* CameraHandle;

// =========================================================================
// 2. 错误码与枚举
// =========================================================================

// 所有 API 的返回值类型。
// CAM_PENDING 不是错误，表示异步命令已提交，结果将通过回调通知。
enum CamError : int32_t {
    CAM_OK                   = 0,   // 成功
    CAM_ERR_CRC_FAILED       = 1,   // 通信校验失败
    CAM_ERR_INVALID_PARAM    = 2,   // 参数无效（空指针、越界等）
    CAM_ERR_UNKNOWN          = 3,   // 未知错误
    CAM_ERR_NOT_INITIALIZED  = 4,   // 未初始化，请先调用 Cam_Initialize
    CAM_ERR_INVALID_HANDLE   = 5,   // 设备句柄无效（已关闭或为空）
    CAM_ERR_INVALID_FUNCTION = 6,   // 设备不支持该功能码
    CAM_ERR_DEVICE_BUSY      = 7,   // 设备忙（已有设备打开，或模式冲突）
    CAM_ERR_INVALID_LENGTH   = 8,   // 数据长度不匹配
    CAM_ERR_NO_DEVICE        = 9,   // 设备不存在或离线
    CAM_ERR_DISCONNECTED     = 10,  // 网络连接断开
    CAM_ERR_TIMEOUT          = 11,  // 通信超时
    CAM_ERR_BUFFER_TOO_SMALL = 12,  // 缓冲区不足
    CAM_ERR_NOT_SUPPORTED    = 13,  // 当前版本不支持该功能

    CAM_PENDING              = 100  // 异步命令已提交，等待回调结果
};

// 设备运行状态 
enum DeviceRunState : uint32_t {
    STATE_IDLE      = 0,            // 空闲（未打开设备）
    STATE_READY     = 1,            // 就绪（设备已打开，未采集）
    STATE_RUNNING   = 2,            // 采集中
    STATE_STOPPING  = 3,            // 停止中
    STATE_ERROR     = 4             // 错误
};

// 帧数据类型
enum PixelFormat : uint16_t {
    PIXEL_FMT_MONO8          = 0x01, // 2D 灰度图
    PIXEL_FMT_CENTERLINE_F32 = 0x10, // 激光中心线
    PIXEL_FMT_POINTCLOUD_XYZ = 0x20  // 3D 点云
};

#pragma pack(push, 1)

// =========================================================================
// 3. 数据结构
// =========================================================================

// 网络配置
struct EthernetConfig {
    uint32_t deviceIp;              // 设备 IP
    uint32_t pcIp;                  // 本机 IP
    uint32_t subnetMask;            // 子网掩码
    uint32_t gateway;               // 默认网关
    uint8_t  mac[6];                // MAC 地址
};

// 设备（主控板）信息
struct DeviceInformation {
    uint32_t       deviceSN;        // 设备序列号
    char           fpgaVersion[8];  // 固件版本
    EthernetConfig network;         // 网络参数
    uint32_t       status;          // 状态（1=在线）
};

// Sensor 信息（只读）
// 注意：当前版本暂不支持读取 Sensor 硬件信息，调用将返回 CAM_ERR_NOT_SUPPORTED。
struct SensorInformation {
    uint16_t sensorId;              // Sensor 编号 (1~4)
    uint32_t sensorSN;              // Sensor 序列号
    char     firmwareVersion[8];    // Sensor 固件版本
    char     model[8];              // 相机型号
};

// Sensor 配置
// 注意：ROI 字段 (roiStart, roiWidth) 和物理测量字段 (pixelPitch, zMin, zMax)
// 当前版本暂不支持，设置这些字段为非零值将返回 CAM_ERR_NOT_SUPPORTED。
// 支持的字段：exposureTime, gain, exposureDelay, sampleRate。
struct SensorConfig {
    uint32_t exposureTime;          // 曝光时间 (微秒)
    uint32_t gain;                  // 增益
    uint32_t exposureDelay;         // 曝光延迟 (微秒)

    uint16_t roiStart;              // ROI 起始列（暂不支持，请置 0）
    uint16_t roiWidth;              // ROI 宽度（暂不支持，请置 0）

    float    pixelPitch;            // X 轴像素尺寸（暂不支持，请置 0）
    uint16_t zMin;                  // Z 轴测量范围最小值（暂不支持，请置 0）
    uint16_t zMax;                  // Z 轴测量范围最大值（暂不支持，请置 0）

    uint32_t sampleRate;            // 采样率 (Hz)
};

// 触发配置
struct TriggerConfig {
    uint16_t triggerMode;           // 触发模式: 1=连续, 2=编码器, 3=外部IO
    uint16_t encoderType;           // 编码器类型: 1=差分, 2=单端
    uint16_t encoderInputMode;      // 编码器输入模式: 1=4倍频, 2=2倍频, 3=1倍频
    uint32_t encoderStep;           // 触发间隔脉冲数
    uint16_t externalEnable;        // 外部触发使能: 0=关闭, 1=开启
    uint16_t reserved;              // 保留字段
};

// 批处理配置
struct BatchConfig {
    uint16_t length;                // 批处理行数
};

// 标定数据（128 字节）
// 注意：当前版本暂不支持标定数据读写，调用将返回 CAM_ERR_NOT_SUPPORTED。
struct CalibrationData {
    uint8_t  data[128];
};

// 设备搜索列表
struct DeviceList {
    uint32_t count;                 // 搜索到的设备数量
    DeviceInformation devices[16];  // 设备列表（最多 16 台）
};

// 帧头信息（回调或同步取帧时返回）
struct FrameInfo {
    uint32_t    frameId;            // 帧序号
    uint16_t    sensorId;           // 数据来源 Sensor (1~4)
    uint32_t    batchId;            // 批次编号
    uint32_t    lineIndex;          // 批次内行号 (0 ~ N-1)
    uint64_t    encoder;            // 编码器脉冲值
    uint64_t    timestamp;          // 时间戳 (微秒)
    uint16_t    width;              // 图像宽度 / 点数
    uint16_t    height;             // 图像高度 / 行数
    uint32_t    dataSize;           // 数据大小 (字节)
    PixelFormat dataType;           // 数据类型
};

#pragma pack(pop)

// =========================================================================
// 4. 回调函数类型
// =========================================================================

// 数据帧回调。注册后，每当收到一帧数据 SDK 就会调用此函数。
//   pFrameInfo : 帧头信息（仅回调期间有效，如需保存请自行拷贝）
//   pData      : 帧数据指针（仅回调期间有效，dataSize 字节）
//   pUserData  : 注册时传入的透传指针
// 注意：回调在 SDK 内部线程执行，不要在回调中调用 Cam_CloseDevice 或 Cam_Finalize。
typedef void(CAM_CALL *DataHandler)(
    const FrameInfo* pFrameInfo,
    const void* pData,
    void* pUserData
);

// 异步命令结果回调。异步 API 调用后立即返回 CAM_PENDING，结果通过此回调通知。
//   result    : CAM_OK=成功, CAM_ERR_TIMEOUT=超时, 其他=失败
//   commandId : 命令编号（SDK 内部分配，可用于日志关联）
//   pUserData : 调用时传入的透传指针
// 注意：回调在 SDK 内部线程执行，不要在回调中调用 Cam_CloseDevice 或 Cam_Finalize。
typedef void(CAM_CALL *CommandResultCallback)(
    CamError result,
    uint16_t commandId,
    void* pUserData
);

// =========================================================================
// 5. API 函数
// =========================================================================

// ── 5.1 生命周期 ──────────────────────────────────────────────────────

// 初始化 SDK。必须在调用任何其他 API 之前调用。
// 可重复调用（幂等），返回 CAM_OK。
CAM_API CamError CAM_CALL Cam_Initialize();

// 释放 SDK 所有资源。关闭所有已打开的设备，释放内部线程和内存。
// 建议在进程退出前调用。可重复调用（幂等）。
CAM_API CamError CAM_CALL Cam_Finalize();

// 获取 SDK 版本号，如 "1.0.0"。返回的字符串由 SDK 内部持有，调用方无需释放。
CAM_API const char* CAM_CALL Cam_GetSdkVersion();

// ── 5.2 设备发现 ──────────────────────────────────────────────────────

// 枚举当前网络中可发现的设备。
//   pList     : [out] 设备列表，内存由调用方分配
//   timeoutMs : 搜索超时时间（毫秒），建议 1000
CAM_API CamError CAM_CALL Cam_EnumDevices(DeviceList* pList, uint32_t timeoutMs);

// ── 5.3 打开 / 关闭设备 ────────────────────────────────────────────────

// 打开网络设备（直连 FPGA，无需上位机）。这是第三方用户最常用的入口。
// SDK 内部会创建网络连接，后续所有控制命令和数据帧都通过此连接传输。
//   fpgaIp      : 设备 IP 地址，如 "192.168.10.10"
//   controlPort : 控制命令端口（通常 5000）
//   dataPort    : 数据端口（通常 60000）
//   pHandle     : [out] 设备句柄，成功后由 SDK 写入
// 注意：同一时刻只能打开一台设备。如需切换，请先关闭当前设备。
CAM_API CamError CAM_CALL Cam_OpenNetworkDevice(const char* fpgaIp, uint16_t controlPort,
                                                uint16_t dataPort, CameraHandle* pHandle);

// 关闭设备。释放设备相关的所有资源。
// 注意：不能在数据回调或异步命令回调中调用此函数，否则会死锁。
CAM_API CamError CAM_CALL Cam_CloseDevice(CameraHandle handle);

// ── 5.4 设备信息 ──────────────────────────────────────────────────────

// 获取设备（主控板）信息。
CAM_API CamError CAM_CALL Cam_GetDeviceInformation(CameraHandle handle, DeviceInformation* pInfo);

// 获取设备当前运行状态。
CAM_API CamError CAM_CALL Cam_GetRunState(CameraHandle handle, DeviceRunState* pState);

// ── 5.5 Sensor 信息 ───────────────────────────────────────────────────

// 获取在线 Sensor 数量。当前架构固定为 4 个 Sensor 槽位。
CAM_API CamError CAM_CALL Cam_GetSensorCount(CameraHandle handle, uint32_t* pCount);

// 获取 Sensor 硬件信息（序列号、固件版本、型号）。
// 注意：当前版本暂不支持，始终返回 CAM_ERR_NOT_SUPPORTED。
//   sensorId : Sensor 编号 (1~4)
CAM_API CamError CAM_CALL Cam_GetSensorInformation(CameraHandle handle, uint16_t sensorId, SensorInformation* pInfo);

// ── 5.6 Sensor 配置 ───────────────────────────────────────────────────

// 写入 Sensor 配置（曝光、增益、采样率等）。
// 一次调用会依次设置 exposureTime、gain、exposureDelay、sampleRate。
// 注意：ROI 和物理测量字段当前不支持，如非零则返回 CAM_ERR_NOT_SUPPORTED。
//   sensorId : Sensor 编号 (1~4)
//   pConfig  : 要写入的配置（调用方分配内存）
CAM_API CamError CAM_CALL Cam_SetSensorConfig(CameraHandle handle, uint16_t sensorId, const SensorConfig* pConfig);

// 读取 Sensor 当前配置。
// 读取的字段：exposureTime、gain、exposureDelay、sampleRate。
// ROI 和物理测量字段保持调用方传入的初值不变。
//   sensorId : Sensor 编号 (1~4)
//   pConfig  : [out] 读回的配置（调用方分配内存）
CAM_API CamError CAM_CALL Cam_GetSensorConfig(CameraHandle handle, uint16_t sensorId, SensorConfig* pConfig);

// ── 5.7 触发配置 ──────────────────────────────────────────────────────

// 写入触发配置（触发模式、编码器参数、外部触发）。
// 一次调用会依次设置所有触发相关参数。
//   sensorId : Sensor 编号 (1~4)
CAM_API CamError CAM_CALL Cam_SetTriggerConfig(CameraHandle handle, uint16_t sensorId, const TriggerConfig* pConfig);

// 读取触发配置。
//   sensorId : Sensor 编号 (1~4)
CAM_API CamError CAM_CALL Cam_GetTriggerConfig(CameraHandle handle, uint16_t sensorId, TriggerConfig* pConfig);

// ── 5.8 批处理配置 ────────────────────────────────────────────────────

// 写入批处理配置（批处理行数）。
//   sensorId : Sensor 编号 (1~4)
CAM_API CamError CAM_CALL Cam_SetBatchConfig(CameraHandle handle, uint16_t sensorId, const BatchConfig* pConfig);

// 读取批处理配置。
//   sensorId : Sensor 编号 (1~4)
CAM_API CamError CAM_CALL Cam_GetBatchConfig(CameraHandle handle, uint16_t sensorId, BatchConfig* pConfig);

// ── 5.9 标定数据 ──────────────────────────────────────────────────────

// 写入标定数据（128 字节）。
// 注意：当前版本暂不支持，始终返回 CAM_ERR_NOT_SUPPORTED。
//   sensorId : Sensor 编号 (1~4)
CAM_API CamError CAM_CALL Cam_WriteCalibration(CameraHandle handle, uint16_t sensorId, const CalibrationData* pCalibData);

// 读取标定数据（128 字节）。
// 注意：当前版本暂不支持，始终返回 CAM_ERR_NOT_SUPPORTED。
//   sensorId : Sensor 编号 (1~4)
CAM_API CamError CAM_CALL Cam_ReadCalibration(CameraHandle handle, uint16_t sensorId, CalibrationData* pCalibData);

// ── 5.10 采集控制 ─────────────────────────────────────────────────────

// sensorId 说明：
//   1~4 : 控制指定 Sensor
//   0   : 对所有在线 Sensor 广播（同时控制 1~4 号 Sensor）

// 开始采集。
CAM_API CamError CAM_CALL Cam_StartCapture(CameraHandle handle, uint16_t sensorId);

// 停止采集。
CAM_API CamError CAM_CALL Cam_StopCapture(CameraHandle handle, uint16_t sensorId);

// 开始批处理采集。
//   batchId : 批次编号，由调用方指定
CAM_API CamError CAM_CALL Cam_StartBatch(CameraHandle handle, uint16_t sensorId, uint32_t batchId);

// 停止批处理采集。
CAM_API CamError CAM_CALL Cam_StopBatch(CameraHandle handle, uint16_t sensorId);

// 复位 Sensor 接收缓冲区。
CAM_API CamError CAM_CALL Cam_ResetBuffer(CameraHandle handle, uint16_t sensorId);

// ── 5.11 数据接收 ─────────────────────────────────────────────────────

// 注册数据帧回调。注册后，每当收到数据帧就会调用 handler。
// 重复注册会覆盖之前的 handler 和 pUserData。
//   handler   : 回调函数指针，传 NULL 则注销
//   pUserData : 透传指针，回调时原样返回（可为 NULL）
CAM_API CamError CAM_CALL Cam_RegisterDataHandler(CameraHandle handle, DataHandler handler, void* pUserData);

// 注销数据帧回调。
CAM_API CamError CAM_CALL Cam_UnregisterDataHandler(CameraHandle handle);

// 同步阻塞读取一帧数据。等待指定 Sensor 的最新一帧，拷贝到调用方缓冲区。
// 与 Cam_RegisterDataHandler 可同时使用，互不冲突。
//   sensorId   : Sensor 编号 (1~4)
//   pFrameInfo : [out] 帧头信息
//   pDataBuf   : [out] 数据缓冲区（调用方分配）
//   bufSize    : 缓冲区大小（字节），须 ≥ 实际帧大小
//   timeoutMs  : 等待超时（毫秒），超时返回 CAM_ERR_TIMEOUT
CAM_API CamError CAM_CALL Cam_GetFrameSync(CameraHandle handle, uint16_t sensorId,
                                           FrameInfo* pFrameInfo, void* pDataBuf,
                                           uint32_t bufSize, uint32_t timeoutMs);

// ── 5.12 异步 API（非阻塞）─────────────────────────────────────────────

// 以下所有 Async 函数：
//   - 调用后立即返回 CAM_PENDING（不等待设备响应），结果通过 callback 通知。
//   - timeoutMs 指定本次命令的超时时间（毫秒），超时后回调收到 CAM_ERR_TIMEOUT。
//   - Get*Async 的 [out] 参数（如 pConfig）在回调触发前由 SDK 填充，调用方须保证
//     该指针在回调触发前一直有效。
//   - 回调在 SDK 内部线程执行，不要在回调中调用 Cam_CloseDevice 或 Cam_Finalize。

// [异步] 开始采集
CAM_API CamError CAM_CALL Cam_StartCaptureAsync(CameraHandle handle, uint16_t sensorId,
                                                uint32_t timeoutMs, CommandResultCallback callback,
                                                void* pUserData);

// [异步] 停止采集
CAM_API CamError CAM_CALL Cam_StopCaptureAsync(CameraHandle handle, uint16_t sensorId,
                                               uint32_t timeoutMs, CommandResultCallback callback,
                                               void* pUserData);

// [异步] 写入 Sensor 配置
CAM_API CamError CAM_CALL Cam_SetSensorConfigAsync(CameraHandle handle, uint16_t sensorId,
                                                   const SensorConfig* pConfig, uint32_t timeoutMs,
                                                   CommandResultCallback callback, void* pUserData);

// [异步] 读取 Sensor 配置
CAM_API CamError CAM_CALL Cam_GetSensorConfigAsync(CameraHandle handle, uint16_t sensorId,
                                                   SensorConfig* pConfig, uint32_t timeoutMs,
                                                   CommandResultCallback callback, void* pUserData);

// [异步] 写入触发配置
CAM_API CamError CAM_CALL Cam_SetTriggerConfigAsync(CameraHandle handle, uint16_t sensorId,
                                                    const TriggerConfig* pConfig, uint32_t timeoutMs,
                                                    CommandResultCallback callback, void* pUserData);

// [异步] 读取触发配置
CAM_API CamError CAM_CALL Cam_GetTriggerConfigAsync(CameraHandle handle, uint16_t sensorId,
                                                    TriggerConfig* pConfig, uint32_t timeoutMs,
                                                    CommandResultCallback callback, void* pUserData);

// [异步] 写入批处理配置
CAM_API CamError CAM_CALL Cam_SetBatchConfigAsync(CameraHandle handle, uint16_t sensorId,
                                                  const BatchConfig* pConfig, uint32_t timeoutMs,
                                                  CommandResultCallback callback, void* pUserData);

// [异步] 读取批处理配置
CAM_API CamError CAM_CALL Cam_GetBatchConfigAsync(CameraHandle handle, uint16_t sensorId,
                                                  BatchConfig* pConfig, uint32_t timeoutMs,
                                                  CommandResultCallback callback, void* pUserData);

// [异步] 开始批处理采集
CAM_API CamError CAM_CALL Cam_StartBatchAsync(CameraHandle handle, uint16_t sensorId, uint32_t batchId,
                                              uint32_t timeoutMs, CommandResultCallback callback,
                                              void* pUserData);

// [异步] 停止批处理采集
CAM_API CamError CAM_CALL Cam_StopBatchAsync(CameraHandle handle, uint16_t sensorId,
                                             uint32_t timeoutMs, CommandResultCallback callback,
                                             void* pUserData);

// [异步] 复位缓冲区
CAM_API CamError CAM_CALL Cam_ResetBufferAsync(CameraHandle handle, uint16_t sensorId,
                                               uint32_t timeoutMs, CommandResultCallback callback,
                                               void* pUserData);

// =========================================================================
// 6. 典型调用流程
// =========================================================================
//
// 最简示例（直连 FPGA）：
//
//   Cam_Initialize();
//
//   CameraHandle handle = nullptr;
//   Cam_OpenNetworkDevice("192.168.10.10", 5000, 60000, &handle);
//
//   // 注册数据回调
//   Cam_RegisterDataHandler(handle, MyDataHandler, nullptr);
//
//   // 配置 Sensor 1
//   SensorConfig cfg = {};
//   cfg.exposureTime = 1000;
//   cfg.gain         = 200;
//   cfg.sampleRate   = 100;
//   Cam_SetSensorConfig(handle, 1, &cfg);
//
//   // 开始采集
//   Cam_StartCapture(handle, 1);
//
//   // ... 数据通过 MyDataHandler 回调到达 ...
//
//   // 停止并清理
//   Cam_StopCapture(handle, 1);
//   Cam_CloseDevice(handle);
//   Cam_Finalize();
//
// 异步示例：
//
//   Cam_StartCaptureAsync(handle, 1, 3000,
//       [](CamError r, uint16_t id, void* ud) {
//           if (r == CAM_OK) { /* 开始成功 */ }
//       }, nullptr);
//   // 立即返回 CAM_PENDING，不阻塞

// =========================================================================
// 7. 注意事项
// =========================================================================
//
// 1. 初始化：所有 API 调用前必须先调 Cam_Initialize()。
// 2. 单设备：同一时刻只能打开一台设备。
// 3. 回调限制：不要在 DataHandler 或 CommandResultCallback 中调用
//    Cam_CloseDevice 或 Cam_Finalize，否则会死锁。
// 4. 数据生命周期：回调中的 pFrameInfo 和 pData 仅在回调期间有效，
//    如需保存请自行拷贝。
// 5. 资源释放：先 Cam_CloseDevice 关闭所有设备，再 Cam_Finalize 释放 SDK。
// 6. 暂不支持的功能：ROI 设置、像素尺寸、Z 范围、标定读写、Sensor 信息读取。
//    这些 API 调用会返回 CAM_ERR_NOT_SUPPORTED。
// 7. sensorId 范围：1~4 对应 4 个 Sensor 槽位，0 表示广播所有 Sensor。
// 8. 同步 API 的线程：同步 API（如 Cam_SetSensorConfig）会阻塞等待设备响应，
//    建议在工作线程中调用，避免阻塞 UI 线程。

// =========================================================================
// 8. 内部/测试 API（仅供上位机或测试使用，第三方用户无需关注）
// =========================================================================

// 打开 Mock 设备（无需真实 FPGA，SDK 内部生成模拟数据）。
CAM_API CamError CAM_CALL Cam_OpenNetworkDeviceMock(const char* fpgaIp, uint16_t controlPort,
                                                    uint16_t dataPort, CameraHandle* pHandle);

// 获取 Mock 模式下已发送的命令计数（用于验证未实现协议不发送 UDP 命令）。
CAM_API CamError CAM_CALL Cam_GetMockSentCommandCount(CameraHandle handle, uint32_t* pCount);

// 注入宿主通信层（上位机内部使用，第三方无需调用）。
CAM_API CamError CAM_CALL Cam_AttachProtocolHandler(void* protocolHandler);

// 通过注入的宿主通信层打开设备（按索引）。
CAM_API CamError CAM_CALL Cam_OpenDevice(uint32_t index, CameraHandle* pHandle);

// 通过注入的宿主通信层打开设备（按 IP 地址）。
CAM_API CamError CAM_CALL Cam_OpenDeviceByIP(const char* ipAddress, CameraHandle* pHandle);

#ifdef __cplusplus
}
#endif

#endif // PHOTON_CAMERA_H