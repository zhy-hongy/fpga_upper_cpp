# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

The project uses CMake + Ninja + MSVC (Visual Studio 2026) with vcpkg for dependencies. The Ninja build requires the MSVC environment to be initialized first.

```bash
# From a VS Developer Command Prompt (or run vcvars64.bat first):
call D:\software\VS\VC\Auxiliary\Build\vcvars64.bat

# Configure (only needed once, or after CMakeLists.txt changes):
cmake -S D:\CODE2\FPGA_TEST\fpga_upper_cpp \
      -B D:\CODE2\FPGA_TEST\fpga_upper_cpp\out\build\x64-Debug \
      -DCMAKE_TOOLCHAIN_FILE=D:/software/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -G Ninja

# Build (Debug):
cmake --build D:\CODE2\FPGA_TEST\fpga_upper_cpp\out\build\x64-Debug

# Run:
D:\CODE2\FPGA_TEST\fpga_upper_cpp\out\build\x64-Debug\fpga_upper.exe
```

**Important**: the Ninja build cannot run from plain Git Bash / PowerShell — it fails with `fatal error C1083: cannot open include file: '<type_traits>'` because the MSVC `INCLUDE`/`LIB` env vars aren't set. Always `call vcvars64.bat` (or use the VS IDE) first.

Build configuration lives in `CMakeSettings.json` (x64-Debug / x64-Release with Ninja). The Release config uses `RelWithDebInfo`.

Qt and OpenCV DLLs are deployed automatically by the CMake POST_BUILD step (see below). No manual DLL copying is required.

## Dependencies (via vcpkg)

- **Qt6** (Core, Gui, Widgets, Network, **Svg**) — `qtbase` + `qtsvg` ports
- **OpenCV** (core, imgproc, imgcodecs) — `opencv4` port with `world` feature
- C++17, CMake ≥ 3.16, AUTOMOC / AUTORCC / AUTOUIC enabled

## Architecture

```
fpga_upper_cpp/
├── main.cpp                      — Entry point. Sets Fusion style, dark theme applied via MainWindow QSS
├── CMakeLists.txt                — Build config + POST_BUILD asset/plugin deployment
├── CMakeSettings.json            — VS CMake preset (Ninja, vcpkg toolchain)
├── resources.qrc                 — Qt resources: arrow SVG icons (compiled into exe)
├── assets/                       — Copied next to exe at build time (logo.jpg, arrows, icons)
│
├── utils/
│   ├── config.h/.cpp            — JSON config (network/camera/UI defaults), Config::load()/save()
│   └── logger.h/.cpp            — Singleton Logger, LOG_INFO/LOG_WARN/LOG_ERROR/LOG_DEBUG macros
│
├── protocol/                     — Wire-format encode/decode
│   ├── transport_frame.h/.cpp   — UDP transport layer: 0xABAB header, CRC-16/MODBUS, 0xDCDC tail.
│   │                              Multi-packet split/assemble + automatic retransmit (TransportAssembler)
│   ├── control_frame.h/.cpp     — Command frames: 0xAA55 header, device type, cmd code, msg_id,
│   │                              rw flag (0x40 write / 0x80 read), 0xFAFA tail
│   ├── camera_frame.h           — CameraFrame: cv::Mat image + contour profile points
│   ├── upgrade_frame.h/.cpp     — Firmware upgrade protocol (chunk split, status codes)
│   └── crc16.h/.cpp             — CRC-16/MODBUS
│
├── communication/                — Network I/O (worker threads)
│   ├── udp_worker.h/.cpp        — Control-channel UDP thread: send cmds, receive responses,
│   │                              TransportAssembler for multi-packet responses
│   ├── udp_data.h/.cpp          — Data-channel UDP thread: camera image reassembly (1280 B/packet,
│   │                              0xF05AA50F first-packet header), per-sensor assembly, cv::Mat
│   │                              reconstruction, retransmit logic
│   └── protocol_handler.h/.cpp  — Central coordinator (QObject on main thread). Wraps UDPWorker +
│                                  UDPDataThread. High-level camera API (set_exposure, set_gain, etc.)
│                                  with pending-request map, ACK timeout, retries. Emits high-level signals.
│
└── ui/
    ├── main_window.h/.cpp       — Main window: toolbar, 1/2/4 image views, right panel (width 350)
    │                              with tabs (触发 / 图像 / 系统). Dark QSS embedded as DARK_QSS const.
    │                              Firmware upgrade UI, protocol analyzer launcher.
    ├── widgets.h/.cpp           — ImageView (QGraphicsView subclass): camera label overlay, centerline,
    │                              contour, 3D heatmap, measure lines. StatusBarWidget.
    └── protocol_analyzer.h/.cpp — Standalone hex-frame analyzer dialog (parses transport + inner frames)
```

## Signal / Data Flow

```
┌──────────────┐  sig_cmd_received   ┌──────────────────┐  connected/disconnected   ┌────────────┐
│  UDPWorker   │ ──────────────────► │  ProtocolHandler │  control_response        │  MainWindow │
│  (QThread)   │                     │  (main thread)   │  data_frame_info         │  (QMainWin) │
│ ctrl: :5000  │  sig_retransmit     │  ┌────────────┐  │  data_resolution         │            │
│              │ ──────────────────► │  │ PendingReq ├─►│  status_message          │  slots:     │
└──────────────┘                     │  │ map        │  │  error_occurred          │  on_xxx()   │
                                     │  └────────────┘  └─────────────────────────┘            │
┌──────────────┐  sig_frame_info     └──────────────────┘                                      │
│UDPDataThread │ ──────────────────►                                                            │
│  (QThread)   │  sig_resolution                                                                 │
│ data: :60000 │ ──────────────────►                                                             │
│              │  get_latest_frame()  ◄── MainWindow polls every 10 ms via QTimer (poll_frame)   │
└──────────────┘                                                                                 │
```

- **UDPWorker / UDPDataThread** emit signals; **ProtocolHandler** connects to them.
- **ProtocolHandler** emits high-level signals; **MainWindow** connects as receiver.
- MainWindow polls `ProtocolHandler::get_latest_frame(sensor_id)` on a 10 ms QTimer and renders into the active `ImageView`.
- Two nested protocols: outer **transport** frame (0xABAB … CRC16 … 0xDCDC, multi-packet + retransmit) wraps an inner **command** frame (0xAA55 … 0xFAFA) for control; image data uses its own 0xF05AA50F header + 1280 B/packet scheme.

## Key Gotchas

1. **Build env**: must run from VS Developer Command Prompt (`vcvars64.bat`). Ninja fails without MSVC env vars.
2. **QWidget parenting**: ImageViews must be parented to `m_display_container` and explicitly `hide()`d when not in use. `setParent(nullptr)` makes them top-level windows at (0,0) that block toolbar buttons.
3. **Disconnect on shutdown**: use `QObject::disconnect(this)` (static — disconnects signals TO this as receiver), NOT `this->disconnect()` (member — disconnects FROM this). The wrong overload causes a debug assertion "Called object is not of the correct type" in ~MainWindow when ProtocolHandler signals arrive during child-object cleanup.
4. **QSS arrow icons**: arrow sub-controls (`QComboBox::down-arrow`, `QSpinBox::up-arrow`/`down-arrow`) need an explicit `image:` (SVG via `:/assets/arrow_*.svg`, requires Qt6::Svg module). Just setting width/height doesn't draw anything.
5. **Radio/checkbox indicators**: unselected `QRadioButton::indicator`/`QCheckBox::indicator` must have an explicit `border` + `background-color` or the circle/square is invisible against the dark background.
6. **CMake plugin deployment**: `platforms/` and `imageformats/` Qt plugins are auto-copied to the output dir by POST_BUILD (`qt_deploy_plugin_subdir`). This covers `qwindows*.dll` and `qsvg*.dll`.
7. **Style precedence**: the dark theme lives entirely in `DARK_QSS` (a `static const char*` in `main_window.cpp`), applied via `setStyleSheet(DARK_QSS)`. The Fusion base style is set in `main.cpp`.

## CameraSDK 架构约束与集成规范

### 1. SDK 职责与禁止项（硬约束）

CameraSDK.dll 是宿主通信链路的**适配层 + 数据回调出口**。它复用宿主已有的通信栈，**严禁自建任何通信层**：

- ❌ 禁止创建 UDP socket
- ❌ 禁止创建 `UDPWorker`
- ❌ 禁止创建 `UDPDataThread`
- ❌ 禁止创建第二个 `ProtocolHandler`
- ❌ 禁止重新解析协议 / 重新组帧
- ❌ **不负责 `ProtocolHandler` 生命周期**：SDK 只保存借用的 `ProtocolHandler*`（`Cam_AttachProtocolHandler` 传入），`Cam_CloseDevice` / `Cam_Finalize` 绝不 delete 它；宿主负责创建与销毁

SDK 内唯一允许新增的线程是 `CallbackWorker`（数据回调专用）。`CameraHandle` 只代表 SDK 内部 `CameraDevice`，不代表宿主对象。

### 2. ABI / ODR 风险

当前 SDK DLL 通过 `ProtocolHandler*` 裸指针与宿主交互，且 `protocol/`、`communication/`、`utils/` 源码在 EXE 与 DLL 中**各编译一份**（存在双份 `ProtocolHandler` 类型）。

**必须保持以下一致**：
- 同一源码版本
- 同一 MSVC 编译器
- 同一 Qt 版本
- 同一编译配置（Debug/Release、Qt 链接等）

**任何 `ProtocolHandler` 类布局修改，必须同时重新编译**：
- `fpga_upper.exe`
- `CameraSDK.dll`

**未来优化方案**：抽取共享的 `fpga_core.dll`，由 EXE 与 SDK 共同链接，消除双份类型与 ODR 风险。

### 3. 线程模型

**数据链路（帧 → SDK 回调）**：
```
UDPDataThread
    ↓ sig_frame_ready(FramePacket)
ProtocolHandler::frame_ready
    ↓ (SDK lambda 直连)
DataDispatcher        (队列 + 每传感器最新帧缓存，线程安全)
    ↓
CallbackWorker        (SDK 唯一新增线程)
    ↓
DataHandler           (绝不运行在 UDP/UI 线程)
```

**控制链路（C API → FPGA）**：
```
C API
    ↓
CameraDevice          (句柄校验)
    ↓
ProtocolAdapter       (QMetaObject::invokeMethod 投递到宿主主线程 + 有界等 ACK)
    ↓
ProtocolHandler
    ↓
UDPWorker
```

### 4. API 使用限制

- **控制 API 建议从工作线程调用**：`invoke_and_wait` 会把命令投递到宿主主线程执行并阻塞等待 ACK；若从 UI 主线程调用，会因主线程被占用而一直等到超时（默认 4s/命令）。
- **`DataHandler` 回调内禁止销毁自身 `CameraHandle`**：回调运行在 `CallbackWorker`，回调内调用 `Cam_CloseDevice` 关闭同一设备不受支持（worker 无法自等待，存在 use-after-free）。关闭设备应由其他线程调用。
- **`Cam_GetFrameSync` 返回最新缓存帧，不保证新帧**：若该 sensor 已有缓存帧则立即返回（可能重复返回同一帧）；只有缓存为空时才等待新帧直至超时。需要"等下一帧"语义请使用 `Cam_RegisterDataHandler` 回调。
- **`Cam_GetFrameSync` 从 UI 主线程调用会超时**：等待期间持全局锁并阻塞事件循环，建议工作线程调用。