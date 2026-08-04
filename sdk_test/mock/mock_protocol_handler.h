#ifndef MOCK_PROTOCOL_HANDLER_H
#define MOCK_PROTOCOL_HANDLER_H

#include <cstdint>
#include <atomic>

#include <QTimer>

#include "communication/protocol_handler.h"
#include "sdk/src/frame_packet.h"

// MockProtocolHandler：无 FPGA 环境下的 SDK 数据链路验证 Mock（仅 sdk_test 使用）。
//
// 架构约束（用户决策：保持 ProtocolHandler 不动）：
//   - 继承真实 ProtocolHandler，但基类【不 start_all()】→ UDPWorker/UDPDataThread
//     线程不启动、不 bind、不 new QUdpSocket（socket 只在 run() 里创建），
//     因此本 Mock 不产生任何 UDP 活动，也不会触发跨线程 socket 断言。
//   - SDK 控制命令走【非虚】基类方法 → 基类传输未启动 → 各等 ACK 超时返回
//     CAM_ERR_TIMEOUT（Mock 模式下控制命令的预期结果；不触碰 UDP、不改通信逻辑）。
//   - 数据链路完全走 SDK：start_mock_frames() 启动 40ms 定时器，每帧 emit
//     frame_ready(FramePacket) → CameraDevice(DataDispatcher) → CallbackWorker
//     → DataHandler。帧来源字段按约束标注 META_SOURCE_SDK / META_SOURCE_UNKNOWN。
//   - 本类只属于测试代码，不进入正式 SDK。
class MockProtocolHandler : public ProtocolHandler {
public:
    explicit MockProtocolHandler(const Config& config, QObject* parent = nullptr);

    // 模拟"采集启动/停止"：控制 Mock 帧定时器（控制命令本身无法拦截时由 SDKTest 显式驱动）
    void start_mock_frames();
    void stop_mock_frames();
    bool is_capturing() const { return m_capturing.load(); }
    uint32_t generated_frames() const { return m_frame_id.load(); }

    // Mock 设备信息
    QString mock_fpga_version() const { return m_fpgaVersion; }

    // Mock 默认参数（与需求一致）
    int default_exposure() const { return 1000; }
    int default_gain() const { return 1; }
    int default_sample_rate() const { return 2500; }
    int default_trigger_mode() const { return 0; }
    int default_encoder_type() const { return 0; }
    int default_batch_length() const { return 100; }

private:
    void generate_frame();
    static QByteArray build_mono8_frame(uint32_t frameId, uint16_t sensorId);

    QTimer* m_frame_timer = nullptr;          // 运行在 mock 所属线程（宿主主线程事件循环）
    std::atomic<bool> m_capturing{false};
    std::atomic<uint32_t> m_frame_id{0};
    QString m_fpgaVersion = QStringLiteral("MOCK_FPGA_V1.0");
};

#endif // MOCK_PROTOCOL_HANDLER_H
