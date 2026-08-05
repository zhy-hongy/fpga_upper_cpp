#ifndef SDK_TEST_H
#define SDK_TEST_H

#include <atomic>

#include <QThread>

#include "photon_camera.h"
#include "utils/config.h"

class ProtocolHandler;
class MockProtocolHandler;

// SDKTest: 最小化 SDK 功能验证 Demo。
//
// 运行在 fpga_upper.exe 宿主环境内，复用宿主已有的 ProtocolHandler（真实模式），
// 或使用独立 MockProtocolHandler（Mock 模式，无 FPGA 环境）：
//   - 不创建新的 UDP / ProtocolHandler / UDPWorker / UDPDataThread
//   - 不绕过 SDK 直接访问 FPGA，只通过 PhotonCameraSDK.dll 的 C API 交互
//   - 自身是一个 QThread：控制 API 阻塞等待 ACK，需在非 UI 线程调用；
//     "定时器线程"即本线程内的 Cam_GetFrameSync 周期轮询循环
//
// mockMode=true（Mock 模式，用户决策"控制方法 virtual，Mock 立即成功"）：
//   - 附加 MockProtocolHandler（基类不启动 → 无 socket、零 UDP 活动）
//   - ProtocolHandler 控制方法 virtual，Mock 覆盖后：写命令立即回调成功 ACK，
//     读命令返回默认测试数据（按 adapter 的 be32/be16 解析编码）→ 全部 <100ms
//   - Cam_StartCapture → Mock 覆盖启动 40ms 帧定时器（固定 sensor=1、1280×1024、
//     固定值填充）；数据链路完全走 SDK：frame_ready → DataDispatcher →
//     CallbackWorker → DataHandler
//
// run() 调用流程（覆盖全部 C API 主接口 + 超时/线程验证）：
//   Initialize → AttachProtocolHandler → OpenDevice → GetDeviceInformation
//   → RegisterDataHandler → [超时测试: 采集前 GetFrameSync 预期 TIMEOUT]
//   → Get/SetSensorConfig / Get/SetTriggerConfig / Get/SetBatchConfig
//   → StartCapture → (收帧回调) → GetFrameSync 轮询 → StopCapture
//   → UnregisterDataHandler → CloseDevice → Finalize
//
// 日志通过信号 log() 跨线程投递到主线程输出（回调线程不操作 UI）。
class SDKTest : public QThread {
    Q_OBJECT
public:
    explicit SDKTest(ProtocolHandler* protocol, const Config& config,
                     bool mockMode, QObject* parent = nullptr);
    ~SDKTest() override;

    // 请求提前停止（在等待/轮询处检查），可安全重复调用
    void request_stop();

    // 已收到的回调帧数（测试线程读取，CallbackWorker 线程写入）
    int frame_count() const { return m_frame_count.load(); }

signals:
    void log(const QString& line);   // 测试日志（可跨线程队列投递到主线程）

protected:
    void run() override;

private:
    // C 调用约定静态回调：在 SDK 的 CallbackWorker 线程执行
    static void CAM_CALL on_data(const FrameInfo* info, const void* data, void* user);
    void on_frame_received(const FrameInfo* info);
    void log_line(const QString& line);

    ProtocolHandler* m_protocol;      // borrowed，宿主生命周期，SDK 不负责（真实模式）
    Config m_config;                  // 用于构造 MockProtocolHandler 基类
    bool m_mock_mode = false;         // true = Mock 模式（无 FPGA）
    MockProtocolHandler* m_mock = nullptr;   // owned，仅在 Mock 模式创建
    quintptr m_test_thread_id = 0;    // run() 所在线程 id，用于验证回调线程不同
    CameraHandle m_handle = nullptr;
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_frame_count{0};
};

#endif // SDK_TEST_H
