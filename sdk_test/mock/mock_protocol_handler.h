#ifndef MOCK_PROTOCOL_HANDLER_H
#define MOCK_PROTOCOL_HANDLER_H

#include <cstdint>
#include <atomic>

#include <QMutex>
#include <QTimer>
#include <QVector>

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
//   - 发送命令验证：每个覆盖方法在 ACK 之前，用【与真实 send_cmd 完全一致的打包路径】
//     （ProtocolHandler::build_command_frame + split_into_transport_frames，含内层/外层
//     CRC16）生成本次调用对应的协议命令帧，并记录到 m_sent_cmds 供 SDKTest 校验
//     "调用后是否发送了对应的协议命令"。这不会产生任何 UDP 数据。
//   - 数据链路完全走 SDK：Cam_StartCapture 广播 → 本类 start_capture() 覆盖被调用
//     → 启动 40ms 帧定时器，每帧 emit frame_ready(FramePacket) → CameraDevice
//     (DataDispatcher) → CallbackWorker → DataHandler。帧来源字段按约束标注
//     META_SOURCE_SDK / META_SOURCE_UNKNOWN。
//   - 本类只属于测试代码，不进入正式 SDK。

// 一次"已发送"协议命令的记录（含完整传输帧字节，用于测试校验 CRC/格式）
struct MockSentCmd {
    int cmd_code = 0;              // 内层 aa55 帧功能码
    int device_type = 0;           // 内层 aa55 帧设备类型（sensor 1~4）
    int rw = 0;                    // RW_WRITE(0x40) / RW_READ(0x80)
    QByteArray data;               // 内层 payload
    QByteArray transport_frame;    // 完整 abab...dcdc 传输帧（含外层 CRC16）
};

class MockProtocolHandler : public ProtocolHandler {
public:
    explicit MockProtocolHandler(const Config& config, QObject* parent = nullptr);

    // Mock 无 UDP 通信，永远就绪
    void wait_all_ready(int timeoutMs = 10000) override { (void)timeoutMs; }

    // ── 控制命令覆盖：立即返回成功 ACK / 默认测试数据 ──
    void set_exposure(int us, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void read_exposure(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void set_gain(int gain, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void read_gain(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
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
    void get_batch_data(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void get_all_batch_data(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void set_batch_control(bool start, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void reset_buffer(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void start_capture(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void stop_capture(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    // 0x0300 请求调试数据（keepalive）：仅记录发送，无 UDP / 无 ACK。
    void request_camera_data(bool start, int sensor_id = -1) override;

    // ── 设备信息读取覆盖（立即返回默认测试数据）──
    void read_camera_version(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr) override;
    void read_fpga_version(std::function<void(const ControlFrame&)> callback = nullptr,
                           std::function<void()> error_callback = nullptr) override;
    void read_device_serial(std::function<void(const ControlFrame&)> callback = nullptr,
                            std::function<void()> error_callback = nullptr) override;
    void set_device_serial(uint32_t serial, std::function<void(const ControlFrame&)> callback = nullptr,
                           std::function<void()> error_callback = nullptr) override;
    void read_network_config(std::function<void(const ControlFrame&)> callback = nullptr,
                             std::function<void()> error_callback = nullptr) override;
    void set_network_config(uint32_t dev_ip, uint32_t pc_ip, uint32_t mask, uint32_t gateway,
                            const uint8_t mac[6], std::function<void(const ControlFrame&)> callback = nullptr,
                            std::function<void()> error_callback = nullptr) override;

    // ── 发送命令捕获（供 SDKTest 校验"调用后是否发送了对应协议命令"）──
    // 记录一条已发送命令（线程安全）
    void record_sent(int cmd_code, int device_type, int rw,
                     const QByteArray& data, const QByteArray& transport_frame);
    // 取走并清空全部已记录命令（线程安全）
    QVector<MockSentCmd> take_sent_commands();
    // 未清空的当前记录条数（线程安全）
    int sent_command_count() const;

    // Mock 帧生成（start_capture/stop_capture 覆盖内部调用；SDKTest 也可显式驱动）
    void start_mock_frames();
    void stop_mock_frames();
    bool is_capturing() const { return m_capturing.load(); }
    uint32_t generated_frames() const { return m_frame_id.load(); }

    // Mock 设备信息
    QString mock_fpga_version() const { return m_fpgaVersion; }

    // 静默模式：置 true 后 reply() 不再回调（模拟"无 ACK"），供异步 API 超时测试。
    // 置 false 恢复正常立即回复。影响下一个/后续所有控制命令。
    void set_silent_acks(bool silent) { m_silent_acks = silent; }
    bool silent_acks() const { return m_silent_acks; }

    // Mock 默认测试数据（读命令返回值；Set 不持久化）
    int default_exposure() const { return 1000; }
    int default_gain() const { return 1; }
    int default_sample_rate() const { return 2500; }
    int default_trigger_mode() const { return 0; }
    int default_encoder_type() const { return 0; }
    int default_encoder_input_mode() const { return 0; }
    int default_encoder_step() const { return 0; }
    int default_external_trigger() const { return 0; }
    int default_batch_length() const { return 100; }

    // 记录上次 set_batch_length 的值（供 get_batch_data 生成对应数量的模拟帧）
    int current_batch_length() const { return m_batch_length; }

private:
    // 用真实打包路径生成本次控制命令对应的传输帧并记录；返回首个传输帧。
    // （build_command_frame 生成 aa55 内层帧含内层 CRC，split_into_transport_frames
    //   生成 abab 外层帧含外层 CRC —— 与真实 send_cmd 完全一致）
    QByteArray emit_command(int cmd_code, int rw, const QByteArray& data, int sensor_id);

    void generate_frame();
    static QByteArray build_frame();                     // 固定值填充 1280×1024
    static QByteArray be32(uint32_t v);                  // 4 字节大端（adapter be32 解析）
    static QByteArray be16(uint16_t v);                  // 2 字节大端（adapter be16 解析）
    void reply(std::function<void(const ControlFrame&)> cb,
               const QByteArray& data = QByteArray());   // 立即回调成功 ACK（静默时跳过）

    QTimer* m_frame_timer = nullptr;          // 运行在 mock 所属线程（宿主主线程事件循环）
    std::atomic<bool> m_capturing{false};
    std::atomic<bool> m_silent_acks{false};   // 静默模式：跳过 reply（模拟无 ACK）
    std::atomic<uint32_t> m_frame_id{0};
    QString m_fpgaVersion = QStringLiteral("1.0.0.1");

    mutable QMutex m_cmd_mutex;               // 保护 m_sent_cmds
    QVector<MockSentCmd> m_sent_cmds;         // 已发送命令记录
    int m_batch_length = 100;                  // 上次 set_batch_length 的值
};

#endif // MOCK_PROTOCOL_HANDLER_H
