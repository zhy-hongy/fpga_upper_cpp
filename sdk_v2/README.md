# FPGA C++ SDK 2.0

按 `D:\CODE2\fpga_upper` 的现行 Python 上位机移植。协议基准为本地同步文档的 2026-10-10 版本；飞书页面在本次会话的网页工具中不可直接访问。协议副本保存在 `docs/protocol.md`。

新实现位于 `sdk_v2/`，Qt 演示位于 `demo_v2/`。旧 `sdk/`、`communication/`、`protocol/` 和旧界面保留，包括原有未提交的修改。根 CMake 默认构建新实现；设置 `FPGA_BUILD_LEGACY_APP=ON` 可以同时构建旧程序。新 SDK 使用 `fpga::Device` C++ API，不兼容旧 `Cam_*` ABI。

## 构建与运行

在项目根目录执行：

```powershell
.\build_sdk_v2.ps1 -Configuration Release
.\out\sdk-v2-package\bin\fpga_sdk_demo.exe --simulate
```

脚本初始化本机 MSVC 环境，构建、运行 CTest，并生成 `out/sdk-v2-package/`。安装包包含 `FPGASDK.dll`、导入库、公共头文件、CMake 配置、示例和 Qt demo 所需运行库/平台插件。可选的 `FPGAVision.dll` 提供 OpenCV 视觉检测。

当前本机构建环境：Visual Studio 2026 / MSVC 19.51，vcpkg 的 Qt6 / OpenCV。脚本显式设置本机中文 `/showIncludes` 前缀，避免 Ninja 丢失头文件依赖。首次改用已有构建目录可执行 `-Clean`。如果换成英文编译器，删除或修改脚本的 `FPGA_MSVC_INCLUDE_PREFIX` 设置。

核心 SDK 本身不依赖 Qt 或 OpenCV，应用可以完全没有 Qt 事件循环：

```powershell
.\build_sdk_v2.ps1 -Configuration Release -NoDemo -NoVision
```

也可以独立配置 `sdk_v2/`，关闭 `FPGA_BUILD_DEMO` 和 `FPGA_BUILD_VISION`。DLL 公共 API 使用 C++17/STL；第三方需要使用兼容的 MSVC 工具集、运行库和构建配置。此包不是跨编译器 C ABI。

## 第三方调用

```cmake
find_package(FPGASDK CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE FPGA::SDK)
```

将安装包路径加入 `CMAKE_PREFIX_PATH`，并把 `bin/FPGASDK.dll` 放在应用旁边。可直接用 `examples/` 验证第三方集成。视觉检测需 `find_package(FPGASDK CONFIG REQUIRED COMPONENTS Vision)`，然后链接 `FPGA::Vision`，并安装兼容 OpenCV。

```cpp
#include <fpga/device.hpp>

fpga::Device device;
fpga::ConnectionOptions network;
network.device_ip = "192.168.10.10";
network.local_ip = "192.168.10.100";
device.open(network);

// open 只表示成功绑定 UDP，实际在线状态由心跳确认。
auto status = device.read(0, 0x0401, {std::chrono::milliseconds{1000}, 0});
if (!status) throw std::runtime_error(status.error);
auto exposure = device.read(1, 0x0001);
auto ack = device.write_u32(1, 0x0001, 10000);

device.on_camera([](std::shared_ptr<const fpga::CameraEvent> frame) {
    // frame->data.image 是拥有像素内存的 Gray8 图像。
    // 也可以读取 centerline / points3d / interpolated。
});
device.start_capture(0x0f, false); // 4 路，0x0301 原始图像由 SDK 重排
// ...
device.stop_capture();
device.close();
```

`request_async()` 返回独立 future。常规 `read/write` 在应用工作线程同步调用即可；GUI demo 使用专用任务线程。`on_camera/on_batch/on_response/on_error` 在 SDK 的解码线程调用，使用 Qt 时应投递到主线程。回调里可以发送异步命令，但不能等待下一帧或关闭/销毁同一 `Device`。SDK 检查回调线程上的关闭/等待操作，避免自等待。

`latest_frame()` 获取已有缓存，`wait_next_frame(device, after_sequence, timeout)` 只等待比指定 SDK 序号更新的帧。帧和点集拥有独立内存，保留 shared_ptr 不会因下一帧到来而失效。

## 功能与数据约定

- 42 条命令元数据：Python 上位机的 40 条命令，以及文档中的 `0x0012/0x0013` HDR 命令。支持曝光、增益、ROI、CMOS 区域、供电、测试图、行时间、激光、四路曝光延迟、触发、编码器、采样、批处理、插值、网络、型号、序列号和标定。原始命令入口允许调试其它功能码。
- 24B 外层 `ABAB` 和 18B 内层 `AA55` 编解码、CRC16/MODBUS、1448B 载荷分包。支持乱序/重复分片、冲突检测、分设备组包、超时回收及有界内存。控制应答按设备、功能码、消息号、类型、读写标志匹配。`0x0300` 请求允许匹配 `0x0301` 应答。
- `0x0300/0x0301` 综合数据的两种 Length 口径、图像声明高度修正、原始 lane 重排、中心线、Q16.16/float32 点云与插值；另有 `0x0302/0x0303/0x0304` 独立数值格式和旧纯图像兼容。
- `0x0204` 实时批与 `0x0205` 全部批：18B 公共头，记录中保留帧号、原始 40bit 时间戳、批次编号及对齐字节，Q16.16 高度转为 double。实时批严格限制 50 条；全部批支持更多记录。
- 批长和批控制默认发送给转发板及相机 1~4，遇到失败会返回具体应答。曝光延迟直接发送四个 uint32 时钟计数，每个计数 20ns；采样频率按 50MHz 周期换算；供电命令的 0 为上电、1 为断电。
- 独立 UDP 8888 升级协议 `0x80→0x81→0x82→0x83`，固件 FPAC 打包/校验、CRC32、多个设备顺序升级、取消和进度。只重试请求帧；控制和数据不盲目重发。CRC32 与 Python 一样追加到最后一个现有分块，最终块可能为 1028B。转发板先升级，未响应设备末尾再尝试一次。
- 对焦 Tenengrad、ROI/平滑/峰值历史；显式加载上位机现有 `LaserProfiler_*` DLL，配置提取/插值并复制结果；SF 中心线/点云/插值误差、有效值掩码、网格/列延迟诊断。
- 可选 OpenCV 模块：激光线倾角、十字靶标对准与追踪、棋盘格角点。公共接口与默认算法参数按 Python 移植。
- 设备发现、无重试的重复寄存器收发与固定期望读回比较、图像/点云/批数据导出，见 `fpga/tools.hpp`。

图像为 Gray8，中心线坐标是像素；综合点云/插值以设备上传 Q16.16 转换后的值为准。独立 `0x0304` 保留旧格式的原始坐标和 int16 高度，不能把它和综合数据的 Q16.16 解释混用。无效数值 `-999` 等保留在 SDK 原始结果中，使用 `valid_numeric()` 决定显示/比较。批时间戳的单位未在协议中说明，因此只保存原始计数。

`0x0502` 同时兼容协议的 4B 版本号和 Python 当前使用的 8B 字符串。默认严格校验 CRC；若对接 CRC 字段为零的旧测试发送器，可明确设置 `ConnectionOptions::verify_crc=false`。损坏传输帧尾只在完整结构、合法字段和真实 CRC 都正确时允许恢复。当前 FPGA 暂未实现分包补传，SDK 对不完整帧按超时丢弃并计数；命令 ACK 超时重试和分包补传是不同机制。

SDK 的在途请求、接收组包和解码队列分别有数量与字节限制。连续采集每路最多一个在途请求，避免 FPGA 积压旧请求。统计中的 `dropped_events` 表示应用处理速度跟不上有界回调队列；控制 future 的应答完成不依赖 UI 回调是否及时消费。

## Qt 演示

默认界面提供明确的 UDP 模拟模式，模拟器是真实 socket/协议收发。`--simulate` 自动连接并显示四路图像；硬件模式先通过心跳确认再显示在线。

演示包括四路图像与轮廓/点云曲线、可选原始重排、采集间隔、参数写入及读回、触发/编码器/批控制、插值配置、网络/型号/序列号/标定、原始寄存器、对焦、已有算法 DLL 的 SF 比较、固件解析/升级、PNG/CSV 导出和通信统计。固件模拟模式只解析固件，不会向模拟设备宣称完成真实烧录。

```powershell
.\out\sdk-v2-package\bin\fpga_sdk_demo.exe
```

算法 DLL、kernel.xml 和 neural_net.xml 使用显式可配置路径；演示不把它们作为内置替代算法。上位机的 Vivado/JTAG 调试、TShark/Npcap 抓包和离线 RTL 定点仿真属于外部调试工具，未嵌入 SDK。

## 验证

`build_sdk_v2.ps1` 自动运行 CTest。设置了本机 Python 参考路径时，额外执行原 Python 代码的协议差分、真实抓包解码差分及原生算法/对焦/SF 差分。视觉模块另有 `vision_reference_compare.py` 对照三种 Python detector。

传输测试覆盖四相机并行、错误来源与身份、乱序、重复及冲突分片、丢包、超时/重试、关闭取消、反复连接、寄存器尺寸、请求内存限制及采集停止。升级测试使用独立真实 UDP 对端验证设备顺序、最后 CRC、数据块 ACK、错误块号、请求重试、取消和结果确认。GUI smoke 测试走实际参数按钮路径并生成截图和日志。

实机探测工具 `fpga_hardware_probe` 不写寄存器、不升级固件。它读取心跳/配置，并可用 `--image` 获取每个在线相机的一帧；使用新的输出目录保留原始应答和图像。实际设备测试结果单独保存，不把 loopback 模拟结果等同于实机长期稳定性测试。
