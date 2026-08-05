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

enum CamError : int32_t {
    CAM_OK                   = 0,   // 操作成功
    CAM_ERR_UNKNOWN          = -1,  // 未知错误
    CAM_ERR_NOT_INITIALIZED  = -2,  // SDK 未初始化
    CAM_ERR_INVALID_HANDLE   = -3,  // 无效的相机句柄
    CAM_ERR_INVALID_PARAM    = -4,  // 参数错误或指针为空
    CAM_ERR_NO_DEVICE        = -5,  // 指定 Sensor/设备不存在或离线
    CAM_ERR_DISCONNECTED     = -6,  // 网络连接断开
    CAM_ERR_TIMEOUT          = -7,  // 通信超时
    CAM_ERR_BUFFER_TOO_SMALL = -8,  // 缓冲区不足
    CAM_ERR_CRC_FAILED       = -9,  // EEPROM/帧 CRC 校验失败
    CAM_ERR_DEVICE_BUSY      = -10, // 设备忙
    CAM_ERR_NOT_SUPPORTED    = -11, // 不支持的功能
    CAM_PENDING              = 100  // 异步命令已提交，结果经回调通知（非阻塞）
};

enum DeviceRunState : uint32_t {
    STATE_IDLE      = 0,            // 空闲
    STATE_READY     = 1,            // 就绪
    STATE_RUNNING   = 2,            // 采集中
    STATE_STOPPING  = 3,            // 停止中
    STATE_ERROR     = 4             // 错误
};

enum PixelFormat : uint16_t {
    PIXEL_FMT_MONO8          = 0x01, // 2D 灰度图 (0x0301)
    PIXEL_FMT_CENTERLINE_F32 = 0x10, // 激光中心线 (0x0302)
    PIXEL_FMT_POINTCLOUD_XYZ = 0x20  // 3D 点云 (0x0303)
};

#pragma pack(push, 1)

// =========================================================================
// 3. 结构体定义 (SDK 高层 API 数据抽象层)
// =========================================================================

// 3.1 主控板网络配置 (主控 EEPROM 0x0010, 22 Bytes)
struct EthernetConfig {
    uint32_t deviceIp;              // 设备 IP
    uint32_t pcIp;                  // 本机/PC IP
    uint32_t subnetMask;            // 子网掩码
    uint32_t gateway;               // 默认网关
    uint8_t  mac[6];                // MAC 地址
};

// 3.2 主控板信息 (仅保留 FPGA 主控硬件信息)
struct DeviceInformation {
    uint32_t       deviceSN;        // 主控硬件 SN (0x0000)
    char           fpgaVersion[8];  // FPGA 固件版本 (0x0004)
    EthernetConfig network;         // 网络参数 (0x0010)
    uint32_t       status;          // FPGA 基础状态
};

// 3.3 Sensor 硬件信息 (Sensor EEPROM 0x0000 ~ 0x0015, 只读)
struct SensorInformation {
    uint16_t sensorId;              // Sensor 位置编号 (1~4)
    uint32_t sensorSN;              // Sensor 硬件 SN
    char     firmwareVersion[8];    // Sensor 固件版本
    char     model[8];              // 相机型号 (如 "CAM-3D01")
};

// 3.4 Sensor 核心基础配置 (对应 Sensor EEPROM 0x0096 ~ 0x00B1 数据映射，共 28 Bytes)
struct SensorConfig {
    // 曝光与增益
    uint32_t exposureTime;          // 曝光时间 (us) [0x0096]
    uint32_t gain;                  // 曝光增益 [0x009A]
    uint32_t exposureDelay;         // 曝光延迟 [0x009E]

    // 图像 ROI
    uint16_t roiStart;              // ROI 起始列 [0x00A2 高16位]
    uint16_t roiWidth;              // ROI 宽度 [0x00A2 低16位]

    // 物理与测量参数
    float    pixelPitch;            // X 轴像素尺寸 [0x00A6]
    uint16_t zMin;                  // Z 轴测量范围 Min [0x00AA 高16位]
    uint16_t zMax;                  // Z 轴测量范围 Max [0x00AA 低16位]

    // 采样
    uint32_t sampleRate;            // FPS / LineRate [0x00AE]
};

// 3.5 触发与控制配置 (对应 Sensor EEPROM 0x00B2 ~ 0x00BE，共 14 Bytes)
struct TriggerConfig {
    uint16_t triggerMode;           // 1:连续, 2:编码器, 3:外部IO [0x00B2]
    uint16_t encoderType;           // 1:差分, 2:单端 [0x00B4]
    uint16_t encoderInputMode;      // 1:4倍频, 2:2倍频, 3:1倍频 [0x00B6]
    uint32_t encoderStep;           // 触发间隔脉冲数 [0x00B8]
    uint16_t externalEnable;        // 外部触发使能 (0:关, 1:开) [0x00BE]
    uint16_t reserved;              // 协议预留填充 [对齐 14 Bytes]
};

// 3.6 批处理配置 (对应 Sensor EEPROM 0x00BC, 2 Bytes)
struct BatchConfig {
    uint16_t length;                // 批处理行数 [0x00BC]
};

// 3.7 Sensor 专属标定数据块 (对应 Sensor EEPROM 0x0016 ~ 0x0095, 128 Bytes)
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

// 4.1 环境与网关发现
CAM_API CamError CAM_CALL Cam_Initialize();
CAM_API CamError CAM_CALL Cam_Finalize();
CAM_API const char* CAM_CALL Cam_GetSdkVersion();
CAM_API CamError CAM_CALL Cam_EnumDevices(DeviceList* pList, uint32_t timeoutMs);

// 4.2 宿主集成与网关连接
// 宿主 App 初始化完成后，将已有 ProtocolHandler* 传入 SDK。
// SDK 只保存借用指针：不负责创建、不负责释放，生命周期由宿主管理。
CAM_API CamError CAM_CALL Cam_AttachProtocolHandler(void* protocolHandler);

// 4.3 网关连接与主控信息
CAM_API CamError CAM_CALL Cam_OpenDevice(uint32_t index, CameraHandle* pHandle);
CAM_API CamError CAM_CALL Cam_OpenDeviceByIP(const char* ipAddress, CameraHandle* pHandle);
CAM_API CamError CAM_CALL Cam_CloseDevice(CameraHandle handle);
CAM_API CamError CAM_CALL Cam_GetDeviceInformation(CameraHandle handle, DeviceInformation* pInfo);
CAM_API CamError CAM_CALL Cam_GetRunState(CameraHandle handle, DeviceRunState* pState);

// 4.4 Sensor 独立节点查询 (sensorId 范围 1~4)
CAM_API CamError CAM_CALL Cam_GetSensorCount(CameraHandle handle, uint32_t* pCount);
CAM_API CamError CAM_CALL Cam_GetSensorInformation(CameraHandle handle, uint16_t sensorId, SensorInformation* pInfo);

// 4.5 Sensor 参数读写 (读写 Sensor EEPROM 0x0096 ~ 0x00B1)
CAM_API CamError CAM_CALL Cam_SetSensorConfig(CameraHandle handle, uint16_t sensorId, const SensorConfig* pConfig);
CAM_API CamError CAM_CALL Cam_GetSensorConfig(CameraHandle handle, uint16_t sensorId, SensorConfig* pConfig);

// 4.6 Trigger & Batch 控制 (读写 Sensor EEPROM 0x00B2 ~ 0x00BE)
CAM_API CamError CAM_CALL Cam_SetTriggerConfig(CameraHandle handle, uint16_t sensorId, const TriggerConfig* pConfig);
CAM_API CamError CAM_CALL Cam_GetTriggerConfig(CameraHandle handle, uint16_t sensorId, TriggerConfig* pConfig);
CAM_API CamError CAM_CALL Cam_SetBatchConfig(CameraHandle handle, uint16_t sensorId, const BatchConfig* pConfig);
CAM_API CamError CAM_CALL Cam_GetBatchConfig(CameraHandle handle, uint16_t sensorId, BatchConfig* pConfig);

// 4.7 标定矩阵独立读写 (读写 Sensor EEPROM 0x0016 ~ 0x0095)
CAM_API CamError CAM_CALL Cam_WriteCalibration(CameraHandle handle, uint16_t sensorId, const CalibrationData* pCalibData);
CAM_API CamError CAM_CALL Cam_ReadCalibration(CameraHandle handle, uint16_t sensorId, CalibrationData* pCalibData);

// 4.8 采集控制 (sensorId=1~4 操控单通道；sensorId=0 表示对所有在线 Sensor 广播)
CAM_API CamError CAM_CALL Cam_StartCapture(CameraHandle handle, uint16_t sensorId);
CAM_API CamError CAM_CALL Cam_StopCapture(CameraHandle handle, uint16_t sensorId);
CAM_API CamError CAM_CALL Cam_StartBatch(CameraHandle handle, uint16_t sensorId, uint32_t batchId);
CAM_API CamError CAM_CALL Cam_StopBatch(CameraHandle handle, uint16_t sensorId);
CAM_API CamError CAM_CALL Cam_ResetBuffer(CameraHandle handle, uint16_t sensorId);

// 4.9 多通道异步数据回调与同步接收
CAM_API CamError CAM_CALL Cam_RegisterDataHandler(CameraHandle handle, DataHandler handler, void* pUserData);
CAM_API CamError CAM_CALL Cam_UnregisterDataHandler(CameraHandle handle);
CAM_API CamError CAM_CALL Cam_GetFrameSync(CameraHandle handle, uint16_t sensorId, FrameInfo* pFrameInfo, void* pDataBuf, uint32_t bufSize, uint32_t timeoutMs);

// 4.10 异步非阻塞控制 API（立即返回 CAM_PENDING，结果经 CommandResultCallback 通知）
// 说明：
//   - 调用后不等待 FPGA ACK，立即返回 CAM_PENDING；ACK 或超时后回调通知。
//   - timeoutMs 为本次命令超时（ms），超时后回调 result=CAM_ERR_TIMEOUT。
//   - Get*Async：调用方传入输出指针，SDK 在 ACK 到达、回调触发前填充其值；
//     调用方需保证该指针在回调前有效。
CAM_API CamError CAM_CALL Cam_StartCaptureAsync(CameraHandle handle, uint16_t sensorId, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);
CAM_API CamError CAM_CALL Cam_StopCaptureAsync(CameraHandle handle, uint16_t sensorId, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);
CAM_API CamError CAM_CALL Cam_SetSensorConfigAsync(CameraHandle handle, uint16_t sensorId, const SensorConfig* pConfig, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);
CAM_API CamError CAM_CALL Cam_GetSensorConfigAsync(CameraHandle handle, uint16_t sensorId, SensorConfig* pConfig, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);
CAM_API CamError CAM_CALL Cam_SetTriggerConfigAsync(CameraHandle handle, uint16_t sensorId, const TriggerConfig* pConfig, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);
CAM_API CamError CAM_CALL Cam_GetTriggerConfigAsync(CameraHandle handle, uint16_t sensorId, TriggerConfig* pConfig, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);
CAM_API CamError CAM_CALL Cam_SetBatchConfigAsync(CameraHandle handle, uint16_t sensorId, const BatchConfig* pConfig, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);
CAM_API CamError CAM_CALL Cam_GetBatchConfigAsync(CameraHandle handle, uint16_t sensorId, BatchConfig* pConfig, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);
CAM_API CamError CAM_CALL Cam_StartBatchAsync(CameraHandle handle, uint16_t sensorId, uint32_t batchId, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);
CAM_API CamError CAM_CALL Cam_StopBatchAsync(CameraHandle handle, uint16_t sensorId, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);
CAM_API CamError CAM_CALL Cam_ResetBufferAsync(CameraHandle handle, uint16_t sensorId, uint32_t timeoutMs, CommandResultCallback callback, void* pUserData);

#ifdef __cplusplus
}
#endif

#endif // PHOTON_CAMERA_H
