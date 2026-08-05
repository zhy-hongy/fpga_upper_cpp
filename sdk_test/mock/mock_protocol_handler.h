#ifndef MOCK_PROTOCOL_HANDLER_H
#define MOCK_PROTOCOL_HANDLER_H

#include <cstdint>
#include <atomic>

#include <QTimer>

#include "communication/protocol_handler.h"
#include "sdk/src/frame_packet.h"

// MockProtocolHandler：无 FPGA 环境下的 SDK 控制 + 数据链路验证 Mock（仅 sdk_test 使用）。
//
// 架构约束（用户决策：只放宽"ProtocolHandler 控制方法 virtual"，其余不动）：
//   - 继承真实 ProtocolHandler，但基类【不 start_all()】→ UDPWorker/UDPDataThread
//     线程不启动、不 bind、不 new QUdpSocket → 不产生任何 UDP 活动。
//   - 控制命令：ProtocolAdapter 以 ProtocolHandler* 调用【virtual】控制方法，
//     虚拟分发到本类的覆盖实现 → 立即回调成功 ACK（写命令）或返回默认测试数据
//     （读命令，数据按 adapter 的 be32/be16 解析编码）。因此 Mock 模式下所有
//     控制 C API 均立即返回 CAM_OK（<100ms），无需 UDP、无超时等待。
//   - 数据链路完全走 SDK：Cam_StartCapture 广播 → 本类 start_capture() 覆盖被调用
//     → 启动 40ms 帧定时器，每帧 emit frame_ready(FramePacket) → CameraDevice
//     (DataDispatcher) → CallbackWorker → DataHandler。帧来源字段按约束标注
//     META_SOURCE_SDK / META_SOURCE_UNKNOWN。
//   - 本类只属于测试代码，不进入正式 SDK。
class MockProtocolHandler : public ProtocolHandler {
public:
    explicit MockProtocolHandler(const Config& config, QObject* parent = nullptr);

    // ── 控制命令覆盖：立即返回成功 ACK / 默认测试数据 ──
    void set_exposure(int us, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void read_exposure(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void set_gain(int gain, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void read_gain(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void set_exposure_delay(int delay_us, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void read_exposure_delay(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void set_sample_rate(int rate_hz, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void read_sample_rate(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void set_trigger_mode(int mode, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void read_trigger_mode(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void set_encoder_type(int enc_type, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void read_encoder_type(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void set_encoder_mode(int mode, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void read_encoder_mode(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void set_encoder_interval(int interval, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void read_encoder_interval(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void set_external_trigger(bool enabled, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void read_external_trigger(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void set_batch_length(int count, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void read_batch_length(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void set_batch_control(bool start, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void reset_buffer(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void start_capture(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void stop_capture(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;

    // Mock 帧生成（start_capture/stop_capture 覆盖内部调用；SDKTest 也可显式驱动）
    void start_mock_frames();
    void stop_mock_frames();
    bool is_capturing() const { return m_capturing.load(); }
    uint32_t generated_frames() const { return m_frame_id.load(); }

    // Mock 设备信息
    QString mock_fpga_version() const { return m_fpgaVersion; }

    // Mock 默认测试数据（读命令返回值；Set 不持久化）
    int default_exposure() const { return 1000; }
    int default_gain() const { return 1; }
    int default_exposure_delay() const { return 100; }
    int default_sample_rate() const { return 2500; }
    int default_trigger_mode() const { return 0; }
    int default_encoder_type() const { return 0; }
    int default_encoder_input_mode() const { return 0; }
    int default_encoder_step() const { return 0; }
    int default_external_trigger() const { return 0; }
    int default_batch_length() const { return 100; }

private:
    void generate_frame();
    static QByteArray build_frame();                     // 固定值填充 1280×1024
    static QByteArray be32(uint32_t v);                  // 4 字节大端（adapter be32 解析）
    static QByteArray be16(uint16_t v);                  // 2 字节大端（adapter be16 解析）
    static void reply(std::function<void(const ControlFrame&)> cb,
                      const QByteArray& data = QByteArray());   // 立即回调成功 ACK

    QTimer* m_frame_timer = nullptr;          // 运行在 mock 所属线程（宿主主线程事件循环）
    std::atomic<bool> m_capturing{false};
    std::atomic<uint32_t> m_frame_id{0};
    QString m_fpgaVersion = QStringLiteral("MOCK_FPGA_V1.0");
};

#endif // MOCK_PROTOCOL_HANDLER_H
