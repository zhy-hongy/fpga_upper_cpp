# CameraSDK 通信库 — 参考手册

**CameraSDK**（FPGA 相机通信库）

V1.0.0

2026.08

---

## 目录

1. [运行环境](#1-运行环境)
2. [文件构成](#2-文件构成)
3. [嵌入方法](#3-嵌入方法)
4. [变量类型](#4-变量类型)
5. [结构体定义](#5-结构体定义)
6. [函数](#6-函数)
7. [软件开发接口函数调用顺序](#7-软件开发接口函数调用顺序)

---

## 1 运行环境

| 项目 | 要求 |
|---|---|
| 操作系统 | Windows10 32/64 位（Professional/Enterprise）<br>Windows11 32/64 位（Professional/Enterprise） |
| CPU | Core i3 2.3GHz 或更快 |
| 内存 | 8GB 以上 |
| 硬盘 | 10GB 以上 |
| 接口 | Ethernet 1000BASE-T |
| 显卡 | 2G 显存 |

### 1.1 运行环境

以下内容是在使用 CameraSDK 通信库执行应用程序时的基本环境。

**1.1.1 Microsoft C Runtime Library**

这是执行 DLL 所必需的运行环境。

**1.1.2 Microsoft .NET Framework**

这是执行示例程序（C# 示例）所必需的运行环境。本库当前提供 C/C++ 头文件接口。

---

## 2 文件构成

| 文件 | 说明 |
|---|---|
| `CameraSDK.dll` | DLL 本体（依赖 Qt6Core / Qt6Network 动态库，部署时一并拷贝） |
| `CameraSDK.lib` | `CameraSDK.dll` 的引入库 |
| `photon_camera.h` | 定义通信接口的头文件 |

---

## 3 嵌入方法

### 3.1 C++ 引入动态链接库

**运行时依赖 DLL**

- `CameraSDK.dll`
- `Qt6Cored.dll`、`Qt6Networkd.dll`（及 Qt 运行库相关依赖，由 CMake 部署步骤自动拷贝）

**链接**

- 暗链接：请链接 `CameraSDK.lib`。

**包含文件**

- 请务必将下列头文件放置在源文件中：

```
#include "photon_camera.h"
```

---

## 4 变量类型

| 类型 | 位数 |
|---|---|
| `unsigned char` (`uint8_t`) | 无符号的 8bit 整数 |
| `unsigned short` (`uint16_t`) | 无符号的 16bit 整数 |
| `unsigned int` / `unsigned long` (`uint32_t`) | 无符号的 32bit 整数 |
| `int` (`int32_t`) | 有符号的 32bit 整数 |
| `long long` (`int64_t`) | 有符号的 64bit 整数 |
| `float` | 32bit 浮点数 |
| `double` | 64bit 浮点数 |

---

## 5 结构体定义

### 5.1 EthernetConfig — 主控板网络配置

| 名称 | 定义 |
|---|---|
| 网络配置结构体 | `EthernetConfig` |

```cpp
struct EthernetConfig {
    uint32_t deviceIp;              // 设备 IP
    uint32_t pcIp;                  // 本机/PC IP
    uint32_t subnetMask;            // 子网掩码
    uint32_t gateway;               // 默认网关
    uint8_t  mac[6];                // MAC 地址
};
```

| 描述 | 备注 |
|---|---|
| 主控板网络参数 | - |

### 5.2 DeviceInformation — 主控板信息

| 名称 | 定义 |
|---|---|
| 主控板信息结构体 | `DeviceInformation` |

```cpp
struct DeviceInformation {
    uint32_t       deviceSN;        // 主控硬件 SN
    char           fpgaVersion[8];  // FPGA 固件版本
    EthernetConfig network;         // 网络参数
    uint32_t       status;          // FPGA 基础状态
};
```

| 描述 | 备注 |
|---|---|
| 仅 FPGA 主控硬件信息 | 当前协议无独立主控信息查询命令，`Cam_GetDeviceInformation` 返回零填充 + `status=1`（在线） |

### 5.3 SensorInformation — Sensor 硬件信息

| 名称 | 定义 |
|---|---|
| Sensor 硬件信息结构体 | `SensorInformation` |

```cpp
struct SensorInformation {
    uint16_t sensorId;              // Sensor 位置编号 (1~4)
    uint32_t sensorSN;              // Sensor 硬件 SN
    char     firmwareVersion[8];    // Sensor 固件版本
    char     model[8];              // 相机型号 (如 "CAM-3D01")
};
```

| 描述 | 备注 |
|---|---|
| Sensor 只读硬件信息 | 当前协议无 Sensor 信息读取命令，`Cam_GetSensorInformation` 返回 `CAM_ERR_NOT_SUPPORTED` |

### 5.4 SensorConfig — Sensor 核心基础配置

| 名称 | 定义 |
|---|---|
| Sensor 核心基础配置结构体 | `SensorConfig` |

```cpp
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
```

| 描述 | 备注 |
|---|---|
| 曝光/增益/ROI/物理测量/采样参数 | ROI 与物理测量字段（`roiStart/roiWidth/pixelPitch/zMin/zMax`）协议未实现，非零时 `Cam_SetSensorConfig` 返回 `CAM_ERR_NOT_SUPPORTED` 且不发送任何命令 |

### 5.5 TriggerConfig — 触发与控制配置

| 名称 | 定义 |
|---|---|
| 触发与控制配置结构体 | `TriggerConfig` |

```cpp
struct TriggerConfig {
    uint16_t triggerMode;           // 1:连续, 2:编码器, 3:外部IO
    uint16_t encoderType;           // 1:差分, 2:单端
    uint16_t encoderInputMode;      // 1:4倍频, 2:2倍频, 3:1倍频
    uint32_t encoderStep;           // 触发间隔脉冲数
    uint16_t externalEnable;        // 外部触发使能 (0:关, 1:开)
    uint16_t reserved;              // 协议预留填充
};
```

| 描述 | 备注 |
|---|---|
| 触发模式、编码器参数、外部触发使能 | - |

### 5.6 BatchConfig — 批处理配置

| 名称 | 定义 |
|---|---|
| 批处理配置结构体 | `BatchConfig` |

```cpp
struct BatchConfig {
    uint16_t length;                // 批处理行数
};
```

| 描述 | 备注 |
|---|---|
| 批处理行数 | - |

### 5.7 CalibrationData — Sensor 标定数据块

| 名称 | 定义 |
|---|---|
| 标定数据块结构体 | `CalibrationData` |

```cpp
struct CalibrationData {
    uint8_t  data[128];             // 128 字节标定矩阵二进制块
};
```

| 描述 | 备注 |
|---|---|
| 128 字节标定矩阵二进制块 | 当前协议无标定读写命令，相关 API 返回 `CAM_ERR_NOT_SUPPORTED` 且不发送任何命令 |

### 5.8 设备搜索列表

| 名称 | 定义 |
|---|---|
| 设备信息结构体 | `DeviceInformation` |

`Cam_EnumDevices` 由调用方分配 `DeviceInformation` 数组并传入容量，SDK 写入实际发现的设备信息：

```cpp
DeviceInformation infos[8];
uint32_t count = 0;
Cam_EnumDevices(infos, 8, &count, 1000);
```

| 描述 | 备注 |
|---|---|
| `Cam_EnumDevices` 输出 | 当前接入方式固定为单 FPGA 网关，count 通常为 0 或 1 |
| 容量不足 | `outCount` 始终返回实际总数，调用方可据此重新分配数组 |

### 5.9 FrameInfo — 数据回调帧头信息

| 名称 | 定义 |
|---|---|
| 帧头信息结构体 | `FrameInfo` |

```cpp
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
```

| 描述 | 备注 |
|---|---|
| 数据回调与同步取帧的帧头信息 | 与 `DataHandler` 回调、`Cam_GetFrameSync` 配合使用 |

### 5.10 通用枚举定义

**5.10.1 DeviceRunState — 设备运行状态**

```cpp
enum DeviceRunState : uint32_t {
    STATE_IDLE      = 0,            // 空闲
    STATE_READY     = 1,            // 就绪
    STATE_RUNNING   = 2,            // 采集中
    STATE_STOPPING  = 3,            // 停止中
    STATE_ERROR     = 4             // 错误
};
```

**5.10.2 PixelFormat — 数据类型**

```cpp
enum PixelFormat : uint16_t {
    PIXEL_FMT_MONO8          = 0x01, // 2D 灰度图 (0x0301)
    PIXEL_FMT_CENTERLINE_F32 = 0x10, // 激光中心线 (0x0302)
    PIXEL_FMT_POINTCLOUD_XYZ = 0x20  // 3D 点云 (0x0303)
};
```

**5.10.3 回调函数指针**

```cpp
typedef void(CAM_CALL *DataHandler)(
    const FrameInfo* pFrameInfo,    // 帧头信息（回调期间有效）
    const void* pData,              // 帧载荷数据（dataSize 字节，回调期间有效）
    void* pUserData                 // 调用方透传指针
);

typedef void(CAM_CALL *CommandResultCallback)(
    CamError result,                // CAM_OK（ACK 正常）/ CAM_ERR_TIMEOUT（超时）等
    uint16_t commandId,             // 本次命令唯一 ID（由 SDK 分配）
    void* pUserData                 // 调用方透传指针
);
```

> **注意**：`CommandResultCallback` 在 SDK 内部 CommandManager 线程执行，绝不运行在 UDP / UI / 调用线程；回调内不要调用 `Cam_CloseDevice` / `Cam_Finalize`。

### 5.11 返回码定义

| 名称 | 数值 | 说明 |
|---|---|---|
| `CAM_OK` | 0 | 成功（FPGA 协议 0x0000） |
| `CAM_ERR_CRC_FAILED` | 1 | CRC 校验失败（FPGA 协议 0x0001） |
| `CAM_ERR_INVALID_PARAM` | 2 | 参数错误（FPGA 协议 0x0002） |
| `CAM_ERR_INVALID_FUNCTION` | 6 | 功能码错误：命令功能码 FPGA 不支持（FPGA 协议 0x0006） |
| `CAM_ERR_DEVICE_BUSY` | 7 | 设备忙（FPGA 协议 0x0007） |
| `CAM_ERR_INVALID_LENGTH` | 8 | 消息长度错误：命令/应答长度与协议不符（FPGA 协议 0x0008） |
| `CAM_ERR_UNKNOWN` | 3 | 未知错误（SDK 专用） |
| `CAM_ERR_NOT_INITIALIZED` | 4 | SDK 未初始化：需先调用 `Cam_Initialize`（SDK 专用） |
| `CAM_ERR_INVALID_HANDLE` | 5 | 无效的相机句柄：传入句柄为空或已被关闭（SDK 专用） |
| `CAM_ERR_NO_DEVICE` | 9 | 指定 Sensor/设备不存在或离线（SDK 专用） |
| `CAM_ERR_DISCONNECTED` | 10 | 网络连接断开（SDK 专用） |
| `CAM_ERR_TIMEOUT` | 11 | 通信超时：等待 FPGA ACK 或同步数据超时（SDK 专用） |
| `CAM_ERR_BUFFER_TOO_SMALL` | 12 | 缓冲区不足：调用方缓冲区小于所需大小（SDK 专用） |
| `CAM_ERR_NOT_SUPPORTED` | 13 | 不支持的功能：协议未实现或设备类型不支持（SDK 专用） |
| `CAM_PENDING` | 100 | 异步命令已提交，结果经回调通知（非阻塞，非错误） |

> 返回的是 `int32_t` 整数，可以转为 `CamError` 枚举。与 FPGA 协议错误码语义相同的项使用完全一致的数值；SDK 自身错误码在已有码值之上按正数递增，不使用负数。

---

## 6 函数

### 6.1 函数一览

#### 6.1.1 环境与初始化

| 函数名 | 概要 |
|---|---|
| `Cam_Initialize` | 初始化 SDK |
| `Cam_Finalize` | 释放 SDK 全部资源 |
| `Cam_GetSdkVersion` | 获取 SDK 版本号 |
| `Cam_EnumDevices` | 枚举在线 FPGA 网关 |

#### 6.1.2 宿主集成

| 函数名 | 概要 |
|---|---|
| `Cam_AttachProtocolHandler` | 宿主注入通信处理器（借用指针） |

#### 6.1.3 网关连接与主控信息（宿主模式）

| 函数名 | 概要 |
|---|---|
| `Cam_OpenDevice` | 按序号打开设备 |
| `Cam_OpenDeviceByIP` | 按 IP 打开设备 |
| `Cam_CloseDevice` | 关闭设备 |
| `Cam_GetDeviceInformation` | 获取主控板信息 |
| `Cam_GetRunState` | 获取设备运行状态 |

#### 6.1.4 独立 SDK 模式（网络直连）

| 函数名 | 概要 |
|---|---|
| `Cam_OpenNetworkDevice` | 独立模式直连 FPGA（SDK 内部自建唯一一套通信） |

#### 6.1.5 测试专用 Mock（TEST-ONLY）

| 函数名 | 概要 |
|---|---|
| `Cam_OpenNetworkDeviceMock` | 打开 Mock 设备（无 FPGA 验证用，不绑定 UDP） |
| `Cam_GetMockSentCommandCount` | 查询 Mock 设备累计"已发送"命令条数 |

#### 6.1.6 Sensor 节点查询

| 函数名 | 概要 |
|---|---|
| `Cam_GetSensorCount` | 获取在线 Sensor 数量 |
| `Cam_GetSensorInformation` | 获取 Sensor 硬件信息 |

#### 6.1.7 Sensor 参数读写

| 函数名 | 概要 |
|---|---|
| `Cam_SetSensorConfig` | 写 Sensor 基础配置 |
| `Cam_GetSensorConfig` | 读 Sensor 基础配置 |

#### 6.1.8 Trigger & Batch 控制

| 函数名 | 概要 |
|---|---|
| `Cam_SetTriggerConfig` | 写触发与控制配置 |
| `Cam_GetTriggerConfig` | 读触发与控制配置 |
| `Cam_SetBatchConfig` | 写批处理配置 |
| `Cam_GetBatchConfig` | 读批处理配置 |

#### 6.1.9 标定矩阵读写

| 函数名 | 概要 |
|---|---|
| `Cam_WriteCalibration` | 写标定矩阵（未实现 → 不支持） |
| `Cam_ReadCalibration` | 读标定矩阵（未实现 → 不支持） |

#### 6.1.10 采集控制

| 函数名 | 概要 |
|---|---|
| `Cam_StartCapture` | 开始采集 |
| `Cam_StopCapture` | 停止采集 |
| `Cam_StartBatch` | 开始批处理采集 |
| `Cam_StopBatch` | 停止批处理采集 |
| `Cam_ResetBuffer` | 复位接收缓冲区 |

#### 6.1.11 数据回调与同步接收

| 函数名 | 概要 |
|---|---|
| `Cam_RegisterDataHandler` | 注册数据回调 |
| `Cam_UnregisterDataHandler` | 注销数据回调 |
| `Cam_GetFrameSync` | 同步阻塞读取一帧 |

#### 6.1.12 异步非阻塞控制（全异步，立即返回 `CAM_PENDING`）

| 函数名 | 概要 |
|---|---|
| `Cam_StartCaptureAsync` | 异步开始采集 |
| `Cam_StopCaptureAsync` | 异步停止采集 |
| `Cam_SetSensorConfigAsync` | 异步写 Sensor 配置 |
| `Cam_GetSensorConfigAsync` | 异步读 Sensor 配置 |
| `Cam_SetTriggerConfigAsync` | 异步写触发配置 |
| `Cam_GetTriggerConfigAsync` | 异步读触发配置 |
| `Cam_SetBatchConfigAsync` | 异步写批处理配置 |
| `Cam_GetBatchConfigAsync` | 异步读批处理配置 |
| `Cam_StartBatchAsync` | 异步开始批处理 |
| `Cam_StopBatchAsync` | 异步停止批处理 |
| `Cam_ResetBufferAsync` | 异步复位缓冲区 |

---

### 6.2 函数参考

> 通用约定：除非特别注明，`sensorId` 取值范围 1~4（Sensor 位置编号）；`sensorId=0` 表示对所有在线 Sensor 广播。返回值均参考第 5.11 节 `CamError` 定义。

#### 6.2.1 Cam_Initialize

**格式**

```cpp
CamError CAM_CALL Cam_Initialize();
```

**参数**

- （无）

**返回值**

- `CAM_OK`：初始化成功（重复调用返回 `CAM_OK`，幂等）

**说明**

查找相机前需要初始化 SDK。内部创建全局运行环境（独立进程自动创建 Qt 核心环境），并注册内部数据类型。

#### 6.2.2 Cam_Finalize

**格式**

```cpp
CamError CAM_CALL Cam_Finalize();
```

**参数**

- （无）

**返回值**

- `CAM_OK`：释放成功

**说明**

释放 SDK 全部资源：关闭全部已打开设备、停止独立 SDK 模式的内部通信线程、销毁全局运行环境。建议在进程退出前调用；未初始化时调用返回 `CAM_OK`（幂等）。

#### 6.2.3 Cam_GetSdkVersion

**格式**

```cpp
const char* CAM_CALL Cam_GetSdkVersion();
```

**参数**

- （无）

**返回值**

- 返回 C 语言字符串（SDK 内部静态存储，无需释放）

**说明**

获取 SDK 版本号（如 `"1.0.0"`）。

#### 6.2.4 Cam_EnumDevices

**格式**

```cpp
CamError CAM_CALL Cam_EnumDevices(DeviceInformation* outArray, uint32_t capacity,
                                   uint32_t* outCount, uint32_t timeoutMs);
```

**参数**

- `outArray`：[out] 调用方分配的设备信息数组，可为 `NULL`（仅查询数量）
- `capacity`：`outArray` 数组容量（元素个数）
- `outCount`：[out] 实际发现的设备数量（可能大于 `capacity`）
- `timeoutMs`：枚举等待时间（毫秒），建议 1000

**返回值**

- `CAM_OK`：枚举成功（`outCount` 为实际数量，可为 0）
- `CAM_ERR_NOT_INITIALIZED`：未调用 `Cam_Initialize`
- `CAM_ERR_INVALID_PARAM`：`outCount` 为空

**说明**

枚举当前可发现的 FPGA 网关。宿主注入模式上报已注入的 1 台网关；独立 SDK 模式打开时上报 1 台。当前接入方式固定为单 FPGA 网关。调用方可先传 `NULL`/0 查询总数，再按需分配数组。

#### 6.2.5 Cam_AttachProtocolHandler

**格式**

```cpp
CamError CAM_CALL Cam_AttachProtocolHandler(void* protocolHandler);
```

**参数**

- `protocolHandler`：宿主 `ProtocolHandler*` 指针（以 `void*` 传入）

**返回值**

- `CAM_OK`：注入成功
- `CAM_ERR_INVALID_PARAM`：`protocolHandler` 为空
- `CAM_ERR_NOT_INITIALIZED`：未调用 `Cam_Initialize`
- `CAM_ERR_DEVICE_BUSY`：已有独立 SDK 设备打开（互斥），或已注入过

**说明**

宿主注入模式。宿主 App 初始化完成后，将已有 `ProtocolHandler*` 指针借给 SDK。SDK 只保存借用指针：不负责创建、不负责释放，生命周期由宿主管理。

#### 6.2.6 Cam_OpenDevice

**格式**

```cpp
CamError CAM_CALL Cam_OpenDevice(uint32_t index, CameraHandle* pHandle);
```

**参数**

- `index`：设备序号（当前仅 1 台网关，固定传 0）
- `pHandle`：[out] 相机句柄，成功后由 SDK 写入

**返回值**

- `CAM_OK`：打开成功
- `CAM_ERR_NOT_INITIALIZED`：未初始化
- `CAM_ERR_NO_DEVICE`：未注入 ProtocolHandler，或 index 越界
- `CAM_ERR_DEVICE_BUSY`：已有一台设备打开（单设备限制）
- `CAM_ERR_INVALID_PARAM`：`pHandle` 为空

**说明**

宿主模式按序号打开设备。需先调用 `Cam_AttachProtocolHandler` 注入。

#### 6.2.7 Cam_OpenDeviceByIP

**格式**

```cpp
CamError CAM_CALL Cam_OpenDeviceByIP(const char* ipAddress, CameraHandle* pHandle);
```

**参数**

- `ipAddress`：目标网关 IP 字符串（如 `"192.168.10.10"`）
- `pHandle`：[out] 相机句柄，成功后由 SDK 写入

**返回值**

- `CAM_OK`：打开成功
- `CAM_ERR_NOT_INITIALIZED`：未初始化
- `CAM_ERR_NO_DEVICE`：未注入 ProtocolHandler
- `CAM_ERR_DEVICE_BUSY`：已有一台设备打开（单设备限制）
- `CAM_ERR_INVALID_PARAM`：`ipAddress` 或 `pHandle` 为空

**说明**

宿主模式按 IP 打开设备。当前协议无独立建网能力：复用宿主通信链路，仅记录目标 IP。

#### 6.2.8 Cam_CloseDevice

**格式**

```cpp
CamError CAM_CALL Cam_CloseDevice(CameraHandle handle);
```

**参数**

- `handle`：相机句柄

**返回值**

- `CAM_OK`：关闭成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效

**说明**

关闭并释放设备。若为独立 SDK 模式设备，会先停止内部通信线程（UDP 停止、不再产生帧），再销毁设备对象。**注意：不能在 SDK 数据回调线程 / 异步命令回调线程内调用本函数。**

#### 6.2.9 Cam_GetDeviceInformation

**格式**

```cpp
CamError CAM_CALL Cam_GetDeviceInformation(CameraHandle handle, DeviceInformation* pInfo);
```

**参数**

- `handle`：相机句柄
- `pInfo`：[out] 主控板信息，内存由调用方分配

**返回值**

- `CAM_OK`：获取成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pInfo` 为空

**说明**

获取主控板信息（SN、固件版本、网络配置、状态）。当前协议无独立主控信息查询命令：返回零填充 + `status=1`（在线）。

#### 6.2.10 Cam_GetRunState

**格式**

```cpp
CamError CAM_CALL Cam_GetRunState(CameraHandle handle, DeviceRunState* pState);
```

**参数**

- `handle`：相机句柄
- `pState`：[out] 运行状态（见第 5.10.1 节 `DeviceRunState`）

**返回值**

- `CAM_OK`：获取成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pState` 为空

**说明**

获取设备运行状态。设备打开后返回 `STATE_READY`，未打开返回 `STATE_IDLE`。

#### 6.2.11 Cam_OpenNetworkDevice

**格式**

```cpp
CamError CAM_CALL Cam_OpenNetworkDevice(const char* fpgaIp, uint16_t controlPort,
                                        uint16_t dataPort, CameraHandle* pHandle);
```

**参数**

- `fpgaIp`：FPGA 设备 IP 字符串（如 `"192.168.10.10"`）
- `controlPort`：本地控制 UDP 绑定端口（默认 5000）
- `dataPort`：本地数据 UDP 绑定端口 + FPGA 目标端口（默认 60000）
- `pHandle`：[out] 相机句柄，成功后由 SDK 写入

**返回值**

- `CAM_OK`：打开成功
- `CAM_ERR_NOT_INITIALIZED`：未初始化
- `CAM_ERR_INVALID_PARAM`：`fpgaIp` 为空、`controlPort`/`dataPort` 为 0，或 `pHandle` 为空
- `CAM_ERR_DEVICE_BUSY`：已有宿主注入或已打开设备（互斥）
- `CAM_ERR_UNKNOWN`：内部通信线程启动失败

**说明**

独立 SDK 模式：第三方直接调用 DLL 连接 FPGA，不依赖上位机、不调用 `Cam_AttachProtocolHandler`。SDK 内部创建唯一的通信处理器（一个 UDPWorker + 一个 UDPDataThread），端口映射与宿主一致：控制 socket 绑定 `controlPort`，数据 socket 绑定 `dataPort`，控制命令发往 `fpgaIp:dataPort`。控制/数据流与宿主模式完全一致（异步 ACK + 数据帧回调）。与宿主注入模式互斥。

#### 6.2.12 Cam_OpenNetworkDeviceMock

**格式**

```cpp
CamError CAM_CALL Cam_OpenNetworkDeviceMock(const char* fpgaIp, uint16_t controlPort,
                                            uint16_t dataPort, CameraHandle* pHandle);
```

**参数**

- `fpgaIp`：任意占位 IP（Mock 模式不实际连接）
- `controlPort`：占位控制端口（Mock 模式不绑定）
- `dataPort`：占位数据端口（Mock 模式不绑定）
- `pHandle`：[out] 相机句柄，成功后由 SDK 写入

**返回值**

- `CAM_OK`：打开成功
- `CAM_ERR_INVALID_PARAM`：`pHandle` 为空
- `CAM_ERR_DEVICE_BUSY`：已有设备打开（互斥）
- `CAM_ERR_UNKNOWN`：内部线程启动失败

**说明**

[TEST-ONLY] 打开测试专用 Mock 设备（无 FPGA 环境验证用）。与 `Cam_OpenNetworkDevice` 同签名，但 SDK 内部创建 MockProtocolHandler：不绑定任何 UDP socket、不产生任何网络收发；控制命令立即回调成功 ACK，调用 `Cam_StartCapture` 后按固定参数（1280×1024、sensor=1）生成模拟帧。

#### 6.2.13 Cam_GetMockSentCommandCount

**格式**

```cpp
CamError CAM_CALL Cam_GetMockSentCommandCount(CameraHandle handle, uint32_t* pCount);
```

**参数**

- `handle`：相机句柄（必须为 Mock 设备）
- `pCount`：[out] 累计已发送命令条数

**返回值**

- `CAM_OK`：获取成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pCount` 为空
- `CAM_ERR_NOT_SUPPORTED`：非 Mock 设备

**说明**

[TEST-ONLY] 查询 Mock 设备累计记录的"已发送"协议命令条数，用于验证"未实现协议返回 `CAM_ERR_NOT_SUPPORTED` 且不发送任何 UDP 命令"。

#### 6.2.14 Cam_GetSensorCount

**格式**

```cpp
CamError CAM_CALL Cam_GetSensorCount(CameraHandle handle, uint32_t* pCount);
```

**参数**

- `handle`：相机句柄
- `pCount`：[out] 在线 Sensor 数量

**返回值**

- `CAM_OK`：获取成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pCount` 为空

**说明**

获取在线 Sensor 数量。架构固定 4 个传感器槽位（sensorId 1~4），返回 `*pCount = 4`。

#### 6.2.15 Cam_GetSensorInformation

**格式**

```cpp
CamError CAM_CALL Cam_GetSensorInformation(CameraHandle handle, uint16_t sensorId,
                                           SensorInformation* pInfo);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pInfo`：[out] Sensor 硬件信息，内存由调用方分配

**返回值**

- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pInfo` 为空或 `sensorId` 超出 1~4
- `CAM_ERR_NOT_SUPPORTED`：协议未实现（恒返回）

**说明**

获取 Sensor 硬件信息。当前协议无 Sensor 信息读取命令，一律返回 `CAM_ERR_NOT_SUPPORTED` 且不发送任何命令。

#### 6.2.16 Cam_SetSensorConfig

**格式**

```cpp
CamError CAM_CALL Cam_SetSensorConfig(CameraHandle handle, uint16_t sensorId,
                                      const SensorConfig* pConfig);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pConfig`：待写入的配置，内存由调用方分配

**返回值**

- `CAM_OK`：设置成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pConfig` 为空
- `CAM_ERR_NOT_SUPPORTED`：含未实现的 ROI/物理测量字段

**说明**

写 Sensor 基础配置（曝光/增益/ROI/采样率等）。依次发送曝光时间、增益、曝光延迟、采样率命令。**注意：ROI（`roiStart/roiWidth`）、物理测量参数（`pixelPitch/zMin/zMax`）协议未实现，设置这些字段返回 `CAM_ERR_NOT_SUPPORTED` 且不发送任何命令。**

#### 6.2.17 Cam_GetSensorConfig

**格式**

```cpp
CamError CAM_CALL Cam_GetSensorConfig(CameraHandle handle, uint16_t sensorId,
                                      SensorConfig* pConfig);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pConfig`：[out] 读回的配置，内存由调用方分配

**返回值**

- `CAM_OK`：读取成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pConfig` 为空

**说明**

读 Sensor 基础配置（曝光时间、增益、曝光延迟、采样率）。ROI 与物理测量字段保持调用方传入的初值，不填充。

#### 6.2.18 Cam_SetTriggerConfig

**格式**

```cpp
CamError CAM_CALL Cam_SetTriggerConfig(CameraHandle handle, uint16_t sensorId,
                                       const TriggerConfig* pConfig);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pConfig`：待写入的触发配置

**返回值**

- `CAM_OK`：设置成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pConfig` 为空

**说明**

写触发与控制配置（触发模式、编码器类型/模式/间隔、外部触发使能），依次发送 5 条命令。

#### 6.2.19 Cam_GetTriggerConfig

**格式**

```cpp
CamError CAM_CALL Cam_GetTriggerConfig(CameraHandle handle, uint16_t sensorId,
                                       TriggerConfig* pConfig);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pConfig`：[out] 读回的触发配置

**返回值**

- `CAM_OK`：读取成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pConfig` 为空

**说明**

读触发与控制配置（触发模式、编码器类型/模式/间隔、外部触发使能），依次发送 5 条命令。

#### 6.2.20 Cam_SetBatchConfig

**格式**

```cpp
CamError CAM_CALL Cam_SetBatchConfig(CameraHandle handle, uint16_t sensorId,
                                     const BatchConfig* pConfig);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pConfig`：待写入的批处理配置

**返回值**

- `CAM_OK`：设置成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pConfig` 为空

**说明**

写批处理配置（批处理行数）。

#### 6.2.21 Cam_GetBatchConfig

**格式**

```cpp
CamError CAM_CALL Cam_GetBatchConfig(CameraHandle handle, uint16_t sensorId,
                                     BatchConfig* pConfig);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pConfig`：[out] 读回的批处理配置

**返回值**

- `CAM_OK`：读取成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pConfig` 为空

**说明**

读批处理配置（批处理行数）。

#### 6.2.22 Cam_WriteCalibration

**格式**

```cpp
CamError CAM_CALL Cam_WriteCalibration(CameraHandle handle, uint16_t sensorId,
                                       const CalibrationData* pCalibData);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pCalibData`：待写入的标定数据

**返回值**

- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pCalibData` 为空
- `CAM_ERR_NOT_SUPPORTED`：协议未实现（恒返回）

**说明**

写 Sensor 标定矩阵（128 字节二进制块）。协议未实现，一律返回 `CAM_ERR_NOT_SUPPORTED` 且不发送任何命令。

#### 6.2.23 Cam_ReadCalibration

**格式**

```cpp
CamError CAM_CALL Cam_ReadCalibration(CameraHandle handle, uint16_t sensorId,
                                      CalibrationData* pCalibData);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pCalibData`：[out] 读回的标定数据

**返回值**

- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pCalibData` 为空
- `CAM_ERR_NOT_SUPPORTED`：协议未实现（恒返回）

**说明**

读 Sensor 标定矩阵。协议未实现，一律返回 `CAM_ERR_NOT_SUPPORTED` 且不发送任何命令。

#### 6.2.24 Cam_StartCapture

**格式**

```cpp
CamError CAM_CALL Cam_StartCapture(CameraHandle handle, uint16_t sensorId);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4），0 表示对所有在线 Sensor 广播

**返回值**

- `CAM_OK`：命令已发出
- `CAM_ERR_INVALID_HANDLE`：句柄无效

**说明**

开始采集。`sensorId=0` 时对 1~4 全部 Sensor 广播。

#### 6.2.25 Cam_StopCapture

**格式**

```cpp
CamError CAM_CALL Cam_StopCapture(CameraHandle handle, uint16_t sensorId);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4），0 表示对所有在线 Sensor 广播

**返回值**

- `CAM_OK`：命令已发出
- `CAM_ERR_INVALID_HANDLE`：句柄无效

**说明**

停止采集。

#### 6.2.26 Cam_StartBatch

**格式**

```cpp
CamError CAM_CALL Cam_StartBatch(CameraHandle handle, uint16_t sensorId, uint32_t batchId);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4），0 表示对所有在线 Sensor 广播
- `batchId`：本批次编号（由调用方指定）

**返回值**

- `CAM_OK`：命令已发出
- `CAM_ERR_INVALID_HANDLE`：句柄无效

**说明**

开始批处理采集。

#### 6.2.27 Cam_StopBatch

**格式**

```cpp
CamError CAM_CALL Cam_StopBatch(CameraHandle handle, uint16_t sensorId);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4），0 表示对所有在线 Sensor 广播

**返回值**

- `CAM_OK`：命令已发出
- `CAM_ERR_INVALID_HANDLE`：句柄无效

**说明**

停止批处理采集。

#### 6.2.28 Cam_ResetBuffer

**格式**

```cpp
CamError CAM_CALL Cam_ResetBuffer(CameraHandle handle, uint16_t sensorId);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4），0 表示对所有在线 Sensor 广播

**返回值**

- `CAM_OK`：命令已发出
- `CAM_ERR_INVALID_HANDLE`：句柄无效

**说明**

复位指定 Sensor 的接收缓冲区。

#### 6.2.29 Cam_RegisterDataHandler

**格式**

```cpp
CamError CAM_CALL Cam_RegisterDataHandler(CameraHandle handle, DataHandler handler,
                                          void* pUserData);
```

**参数**

- `handle`：相机句柄
- `handler`：数据回调函数指针（传 `NULL` 等价于注销）
- `pUserData`：调用方透传指针，回调时原样回传（可为 `NULL`）

**返回值**

- `CAM_OK`：注册成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`handler` 为空
- `CAM_ERR_NOT_INITIALIZED`：内部数据链路未就绪

**说明**

注册数据回调。注册后，SDK 在收到数据帧时在内部回调线程调用 `handler`。重复注册会覆盖上一次的 `handler` 与 `pUserData`。数据链路：UDPDataThread → FramePacket → DataDispatcher → CallbackWorker → DataHandler。

#### 6.2.30 Cam_UnregisterDataHandler

**格式**

```cpp
CamError CAM_CALL Cam_UnregisterDataHandler(CameraHandle handle);
```

**参数**

- `handle`：相机句柄

**返回值**

- `CAM_OK`：注销成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效

**说明**

注销数据回调（不再接收数据帧）。

#### 6.2.31 Cam_GetFrameSync

**格式**

```cpp
CamError CAM_CALL Cam_GetFrameSync(CameraHandle handle, uint16_t sensorId,
                                   FrameInfo* pFrameInfo, void* pDataBuf,
                                   uint32_t bufSize, uint32_t timeoutMs);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pFrameInfo`：[out] 帧头信息，内存由调用方分配
- `pDataBuf`：[out] 数据缓冲区，内存由调用方分配
- `bufSize`：缓冲区大小（Bytes），须 ≥ 实际帧大小
- `timeoutMs`：等待超时（毫秒）

**返回值**

- `CAM_OK`：获取成功
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pFrameInfo`/`pDataBuf` 为空、`bufSize` 为 0，或 `sensorId` 超出 1~4
- `CAM_ERR_TIMEOUT`：超时未取到帧
- `CAM_ERR_BUFFER_TOO_SMALL`：缓冲区小于帧大小

**说明**

同步阻塞读取一帧数据。将指定 Sensor 最新一帧数据拷贝到调用方缓冲区并返回帧头信息。

#### 6.2.32 Cam_StartCaptureAsync

**格式**

```cpp
CamError CAM_CALL Cam_StartCaptureAsync(CameraHandle handle, uint16_t sensorId,
                                        uint32_t timeoutMs, CommandResultCallback callback,
                                        void* pUserData);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4），0 表示广播全部
- `timeoutMs`：命令超时（毫秒）
- `callback`：结果回调（ACK 成功→`CAM_OK`，超时→`CAM_ERR_TIMEOUT`）
- `pUserData`：调用方透传指针，回调时原样回传（可为 `NULL`）

**返回值**

- `CAM_PENDING`：命令已提交，结果经回调通知
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`callback` 为空

**说明**

异步非阻塞开始采集。调用后不等待 FPGA ACK，立即返回 `CAM_PENDING`；ACK 或超时后回调通知。**注意：回调在 SDK 内部 CommandManager 线程执行，不要在回调内调用 `Cam_CloseDevice`/`Cam_Finalize`。**

#### 6.2.33 Cam_StopCaptureAsync

**格式**

```cpp
CamError CAM_CALL Cam_StopCaptureAsync(CameraHandle handle, uint16_t sensorId,
                                       uint32_t timeoutMs, CommandResultCallback callback,
                                       void* pUserData);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4），0 表示广播全部
- `timeoutMs`：命令超时（毫秒）
- `callback`：结果回调
- `pUserData`：调用方透传指针（可为 `NULL`）

**返回值**

- `CAM_PENDING`：命令已提交，结果经回调通知
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`callback` 为空

**说明**

异步非阻塞停止采集。

#### 6.2.34 Cam_SetSensorConfigAsync

**格式**

```cpp
CamError CAM_CALL Cam_SetSensorConfigAsync(CameraHandle handle, uint16_t sensorId,
                                           const SensorConfig* pConfig, uint32_t timeoutMs,
                                           CommandResultCallback callback, void* pUserData);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pConfig`：待写入的配置（SDK 在提交时读取内容）
- `timeoutMs`：命令超时（毫秒）
- `callback`：结果回调
- `pUserData`：调用方透传指针（可为 `NULL`）

**返回值**

- `CAM_PENDING`：命令已提交，结果经回调通知
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pConfig` 或 `callback` 为空
- `CAM_ERR_NOT_SUPPORTED`：含未实现的 ROI/物理测量字段（不发送命令）

**说明**

异步非阻塞写 Sensor 基础配置（曝光时间、增益、曝光延迟、采样率）。

#### 6.2.35 Cam_GetSensorConfigAsync

**格式**

```cpp
CamError CAM_CALL Cam_GetSensorConfigAsync(CameraHandle handle, uint16_t sensorId,
                                           SensorConfig* pConfig, uint32_t timeoutMs,
                                           CommandResultCallback callback, void* pUserData);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pConfig`：[out] 读回配置存放区（须在回调前保持有效）
- `timeoutMs`：命令超时（毫秒）
- `callback`：结果回调
- `pUserData`：调用方透传指针（可为 `NULL`）

**返回值**

- `CAM_PENDING`：命令已提交，结果经回调通知
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pConfig` 或 `callback` 为空

**说明**

异步非阻塞读 Sensor 基础配置。SDK 在 ACK 到达、回调触发前填充 `*pConfig`；调用方需保证该指针在回调前有效。

#### 6.2.36 Cam_SetTriggerConfigAsync

**格式**

```cpp
CamError CAM_CALL Cam_SetTriggerConfigAsync(CameraHandle handle, uint16_t sensorId,
                                            const TriggerConfig* pConfig, uint32_t timeoutMs,
                                            CommandResultCallback callback, void* pUserData);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pConfig`：待写入的触发配置
- `timeoutMs`：命令超时（毫秒）
- `callback`：结果回调
- `pUserData`：调用方透传指针（可为 `NULL`）

**返回值**

- `CAM_PENDING`：命令已提交，结果经回调通知
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pConfig` 或 `callback` 为空

**说明**

异步非阻塞写触发与控制配置（触发模式、编码器类型/模式/间隔、外部触发使能）。

#### 6.2.37 Cam_GetTriggerConfigAsync

**格式**

```cpp
CamError CAM_CALL Cam_GetTriggerConfigAsync(CameraHandle handle, uint16_t sensorId,
                                            TriggerConfig* pConfig, uint32_t timeoutMs,
                                            CommandResultCallback callback, void* pUserData);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pConfig`：[out] 读回配置存放区（须在回调前保持有效）
- `timeoutMs`：命令超时（毫秒）
- `callback`：结果回调
- `pUserData`：调用方透传指针（可为 `NULL`）

**返回值**

- `CAM_PENDING`：命令已提交，结果经回调通知
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pConfig` 或 `callback` 为空

**说明**

异步非阻塞读触发与控制配置。

#### 6.2.38 Cam_SetBatchConfigAsync

**格式**

```cpp
CamError CAM_CALL Cam_SetBatchConfigAsync(CameraHandle handle, uint16_t sensorId,
                                          const BatchConfig* pConfig, uint32_t timeoutMs,
                                          CommandResultCallback callback, void* pUserData);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pConfig`：待写入的批处理配置
- `timeoutMs`：命令超时（毫秒）
- `callback`：结果回调
- `pUserData`：调用方透传指针（可为 `NULL`）

**返回值**

- `CAM_PENDING`：命令已提交，结果经回调通知
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pConfig` 或 `callback` 为空

**说明**

异步非阻塞写批处理配置（批处理行数）。

#### 6.2.39 Cam_GetBatchConfigAsync

**格式**

```cpp
CamError CAM_CALL Cam_GetBatchConfigAsync(CameraHandle handle, uint16_t sensorId,
                                          BatchConfig* pConfig, uint32_t timeoutMs,
                                          CommandResultCallback callback, void* pUserData);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4）
- `pConfig`：[out] 读回配置存放区（须在回调前保持有效）
- `timeoutMs`：命令超时（毫秒）
- `callback`：结果回调
- `pUserData`：调用方透传指针（可为 `NULL`）

**返回值**

- `CAM_PENDING`：命令已提交，结果经回调通知
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`pConfig` 或 `callback` 为空

**说明**

异步非阻塞读批处理配置。

#### 6.2.40 Cam_StartBatchAsync

**格式**

```cpp
CamError CAM_CALL Cam_StartBatchAsync(CameraHandle handle, uint16_t sensorId, uint32_t batchId,
                                      uint32_t timeoutMs, CommandResultCallback callback,
                                      void* pUserData);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4），0 表示广播全部
- `batchId`：本批次编号（由调用方指定）
- `timeoutMs`：命令超时（毫秒）
- `callback`：结果回调
- `pUserData`：调用方透传指针（可为 `NULL`）

**返回值**

- `CAM_PENDING`：命令已提交，结果经回调通知
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`callback` 为空

**说明**

异步非阻塞开始批处理采集。

#### 6.2.41 Cam_StopBatchAsync

**格式**

```cpp
CamError CAM_CALL Cam_StopBatchAsync(CameraHandle handle, uint16_t sensorId,
                                     uint32_t timeoutMs, CommandResultCallback callback,
                                     void* pUserData);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4），0 表示广播全部
- `timeoutMs`：命令超时（毫秒）
- `callback`：结果回调
- `pUserData`：调用方透传指针（可为 `NULL`）

**返回值**

- `CAM_PENDING`：命令已提交，结果经回调通知
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`callback` 为空

**说明**

异步非阻塞停止批处理采集。

#### 6.2.42 Cam_ResetBufferAsync

**格式**

```cpp
CamError CAM_CALL Cam_ResetBufferAsync(CameraHandle handle, uint16_t sensorId,
                                       uint32_t timeoutMs, CommandResultCallback callback,
                                       void* pUserData);
```

**参数**

- `handle`：相机句柄
- `sensorId`：Sensor 编号（1~4），0 表示广播全部
- `timeoutMs`：命令超时（毫秒）
- `callback`：结果回调
- `pUserData`：调用方透传指针（可为 `NULL`）

**返回值**

- `CAM_PENDING`：命令已提交，结果经回调通知
- `CAM_ERR_INVALID_HANDLE`：句柄无效
- `CAM_ERR_INVALID_PARAM`：`callback` 为空

**说明**

异步非阻塞复位指定 Sensor 接收缓冲区。

---

## 7 软件开发接口函数调用顺序

### 7.1 宿主注入模式（fpga_upper.exe 内调用）

宿主 App 已将 `ProtocolHandler*` 注入 SDK，SDK 借用该通信链路，不创建任何 UDP 资源。

```cpp
#include "photon_camera.h"

int main()
{
    // 1. 初始化 SDK
    Cam_Initialize();

    // 2. 宿主注入已创建的 ProtocolHandler*（借用，不释放）
    //    ProtocolHandler* ph = ...;  // 宿主已有
    CamError e = Cam_AttachProtocolHandler(ph);
    if (e != CAM_OK) { /* 处理错误 */ }

    // 3. 打开设备
    CameraHandle handle = nullptr;
    e = Cam_OpenDevice(0, &handle);
    if (e != CAM_OK) { /* 处理错误 */ }

    // 4. 注册数据回调（可选，与 GetFrameSync 二选一）
    Cam_RegisterDataHandler(handle, MyDataHandler, nullptr);

    // 5. 设置 Sensor 参数（同步等待 ACK）
    SensorConfig cfg = {};
    cfg.exposureTime = 1000;   // us
    cfg.gain = 200;
    e = Cam_SetSensorConfig(handle, 1, &cfg);

    // 6. 开始采集
    e = Cam_StartCapture(handle, 1);
    if (e != CAM_OK) { /* 处理错误 */ }

    // 7. 停止采集、关闭设备、释放 SDK
    Cam_StopCapture(handle, 1);
    Cam_CloseDevice(handle);
    Cam_Finalize();
    return 0;
}
```

### 7.2 独立 SDK 模式（第三方直连 FPGA，异步）

第三方进程不依赖上位机，SDK 内部自建唯一一套通信（一个 UDPWorker + 一个 UDPDataThread）。

```cpp
#include "photon_camera.h"

int main()
{
    // 1. 初始化 SDK（独立进程自动创建 Qt 核心环境）
    Cam_Initialize();

    // 2. 打开网络设备：控制端口 5000，数据端口 60000
    CameraHandle handle = nullptr;
    CamError e = Cam_OpenNetworkDevice("192.168.10.10", 5000, 60000, &handle);
    if (e != CAM_OK) { /* 处理错误 */ }

    // 3. 注册数据回调
    Cam_RegisterDataHandler(handle, MyDataHandler, nullptr);

    // 4. 异步开始采集：立即返回 CAM_PENDING，ACK 经回调通知
    e = Cam_StartCaptureAsync(handle, 1, 3000, MyResultCallback, nullptr);
    if (e != CAM_PENDING) { /* 处理错误 */ }

    // ... 等待 MyResultCallback(result=CAM_OK) 与 MyDataHandler 收到帧 ...

    // 5. 异步停止采集
    e = Cam_StopCaptureAsync(handle, 1, 3000, MyResultCallback, nullptr);

    // 6. 关闭设备（先停内部通信线程）、释放 SDK
    Cam_CloseDevice(handle);
    Cam_Finalize();
    return 0;
}

void CAM_CALL MyDataHandler(const FrameInfo* pInfo, const void* pData, void* pUserData)
{
    // 在 SDK 内部回调线程执行；帧数据仅在回调期间有效
    // pInfo->frameId / sensorId / width / height / dataSize / timestamp ...
}

void CAM_CALL MyResultCallback(CamError result, uint16_t commandId, void* pUserData)
{
    // 在 SDK 内部 CommandManager 线程执行；不要在回调内 CloseDevice/Finalize
    if (result != CAM_OK) { /* 处理错误 */ }
}
```

### 7.3 独立 Mock 模式（无 FPGA 验证，TEST-ONLY）

与 7.2 完全相同，仅将 `Cam_OpenNetworkDevice` 换成 `Cam_OpenNetworkDeviceMock`：不绑定任何 UDP，控制命令立即 ACK，开始采集后生成固定模拟帧（1280×1024、sensor=1）。

```cpp
Cam_Initialize();
CameraHandle handle = nullptr;
CamError e = Cam_OpenNetworkDeviceMock("0.0.0.0", 5000, 60000, &handle);
// 其余流程同 7.2（RegisterDataHandler → StartCaptureAsync → 收帧 → StopCaptureAsync）
Cam_CloseDevice(handle);
Cam_Finalize();
```

### 7.4 调用顺序注意事项

1. **初始化**：所有 API 前必须先调用 `Cam_Initialize()`。
2. **模式互斥**：宿主注入（`Cam_AttachProtocolHandler`）与独立 SDK 模式（`Cam_OpenNetworkDevice`/`Mock`）互斥；任意时刻至多存在一套 UDPWorker + UDPDataThread。模式已建立后调用另一模式 API 返回 `CAM_ERR_DEVICE_BUSY`。
3. **单设备限制**：同一时刻仅允许打开一台设备。
4. **异步回调线程**：`CommandResultCallback` 在 SDK 内部 CommandManager 线程执行；不要在回调内调用 `Cam_CloseDevice`/`Cam_Finalize`。
5. **数据回调线程**：`DataHandler` 在 SDK 内部回调线程执行；帧数据仅在回调期间有效，如需保存请自行拷贝。
6. **未实现协议**：ROI、像素尺寸、Z 范围、标定读写、Sensor 信息读取等返回 `CAM_ERR_NOT_SUPPORTED`，SDK 绝不发送未知命令、绝不伪造 FPGA 回复。
7. **资源释放顺序**：先 `Cam_CloseDevice` 关闭所有设备，再 `Cam_Finalize` 释放 SDK；进程退出前务必调用 `Cam_Finalize`。

---

*© 2026 CameraSDK 版权所有*
