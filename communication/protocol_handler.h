#ifndef PROTOCOL_HANDLER_H
#define PROTOCOL_HANDLER_H

#include <QObject>
#include <QMap>
#include <QTimer>
#include <functional>
#include <random>
#include <atomic>
#include <chrono>

#include "communication/udp_worker.h"
#include "communication/udp_data.h"
#include "protocol/control_frame.h"
#include "utils/config.h"
#include "sdk/src/frame_packet.h"

constexpr double ACK_TIMEOUT = 2.0;
constexpr int MAX_RETRIES = 3;

struct PendingRequest {
    int msg_id = 0;
    int cmd_code = 0;
    std::function<void(const ControlFrame&)> callback;
    std::function<void()> error_callback;
    double timestamp = 0.0;
    int retries_left = MAX_RETRIES;
    int device_type = 0;
    int rw = 0;
    QByteArray data;
};

class ProtocolHandler : public QObject {
    Q_OBJECT
public:
    explicit ProtocolHandler(const Config& config, int sensor_id = DEV_SENSOR1,
                             QObject* parent = nullptr);
    ~ProtocolHandler() override;

    // Lifecycle
    void start_all();
    void stop_all();
    void set_network_params(const QString& target_host, int target_port, int local_port);

    // 阻塞等待 UDPWorker 和 UDPDataThread 的事件循环全部就绪（有界超时）。
    // 在 start_all() 之后调用，确保子线程的 QUdpSocket 已绑定且事件循环已启动。
    virtual void wait_all_ready(int timeoutMs = 10000);

    // Frame queue access
    cv::Mat get_latest_frame(int sensor_id = -1);
    std::deque<cv::Mat> get_sensor_frame_queue(int sensor_id);
    uint64_t get_frame_seq(int sensor_id) const;
    int last_pkt_id() const;

    // High-level API - Camera controls
    // 这些方法声明为 virtual：SDK 的 ProtocolAdapter 以 ProtocolHandler* 调用它们，
    // Mock 测试子类需要覆盖以立即返回成功 ACK（真实模式无子类，virtual 等价静态绑定）。
    virtual void set_exposure(int us, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void read_exposure(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void set_gain(int gain, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void read_gain(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void set_sample_rate(int rate_hz, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void read_sample_rate(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void set_trigger_mode(int mode, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void read_trigger_mode(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void set_encoder_type(int enc_type, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void read_encoder_type(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void set_encoder_mode(int mode, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void read_encoder_mode(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void set_encoder_interval(int interval, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void read_encoder_interval(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void set_batch_length(int count, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void read_batch_length(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void get_batch_data(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void get_all_batch_data(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void set_external_trigger(bool enabled, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void read_external_trigger(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void set_batch_control(bool start, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void reset_buffer(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void start_capture(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void stop_capture(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);

    // 0x0300 请求调试数据（持续上传 keepalive）：FPGA 收到 0x0001 后开始流式上传
    // (图像+中心线+3D+插值)，需以 ~50ms 心跳周期性重发维持。fire-and-forget：无回调、
    // 不登记 pending、不打印状态日志（GUI 20Hz keepalive 不能刷屏）。
    // sensor_id=-1 时使用默认 sensor (构造时的 DEV_SENSOR1)；调用方需显式传 1~4。
    virtual void request_camera_data(bool start, int sensor_id = -1);

    // ---- 设备信息读取（对齐 通讯协议.md：0x0007/0x0501/0x0502/0x0503）----
    // 相机固件版本走 sensor(1~4)；FPGA 固件版本 / 设备序列号 / 网络参数走 Dev=FPGA(0x0000)。
    virtual void read_camera_version(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    virtual void read_fpga_version(std::function<void(const ControlFrame&)> callback = nullptr,
                                   std::function<void()> error_callback = nullptr);
    virtual void read_device_serial(std::function<void(const ControlFrame&)> callback = nullptr,
                                    std::function<void()> error_callback = nullptr);
    virtual void set_device_serial(uint32_t serial, std::function<void(const ControlFrame&)> callback = nullptr,
                                   std::function<void()> error_callback = nullptr);
    virtual void read_network_config(std::function<void(const ControlFrame&)> callback = nullptr,
                                     std::function<void()> error_callback = nullptr);
    virtual void set_network_config(uint32_t dev_ip, uint32_t pc_ip, uint32_t mask, uint32_t gateway,
                                    const uint8_t mac[6], std::function<void(const ControlFrame&)> callback = nullptr,
                                    std::function<void()> error_callback = nullptr);

protected:
    // 打包一条控制命令帧（aa55 协议帧，含内层 CRC），不发送、不登记 pending。
    // 与 send_cmd 的打包逻辑完全一致（相同 device_type / msg_id / 内层 CRC）。
    // 生成的 msg_id 通过 out_msg_id 返回（可空）。
    // 供 MockProtocolHandler 在验证"调用后是否发送了对应协议命令"时复用同一打包路径。
    QByteArray build_command_frame(int cmd_code, int rw, const QByteArray& data,
                                   int sensor_id, int* out_msg_id = nullptr);

public:
    // Firmware upgrade
    void send_remote_update_request(const QString& file_path,
                                     std::function<void(const ControlFrame&)> callback = nullptr,
                                     std::function<void()> error_callback = nullptr);
    void send_remote_update_control(bool start = true,
                                     std::function<void(const ControlFrame&)> callback = nullptr,
                                     std::function<void()> error_callback = nullptr);
    void send_remote_update_data(int chunk_index, const QByteArray& chunk_data,
                                  std::function<void(const ControlFrame&)> callback = nullptr,
                                  std::function<void()> error_callback = nullptr);
    void send_remote_update_result(std::function<void(const ControlFrame&)> callback = nullptr,
                                    std::function<void()> error_callback = nullptr);

signals:
    void connected();
    void disconnected();
    void connect_failed(const QString& reason);   // socket bind 失败/未就绪 (2026-08)
    void control_response(const ControlFrame& frame);
    void frame_ready(const FramePacket& frame);
    void data_frame_info(int fps, int total, int dropped);
    void data_resolution(int sensor_id, int width, int height);
    void status_message(const QString& msg);
    void error_occurred(const QString& msg);

private slots:
    void on_raw_response(const QByteArray& raw);
    void on_retransmit_request(const QByteArray& raw);
    void check_timeouts();

private:
    int next_msg_id();
    bool send_transport(const QByteArray& frame, int device_type = 0);
    bool send_transport_silent(const QByteArray& frame, int device_type = 0);  // 同 send_transport 但不 emit status_message
    int send_cmd(int cmd_code, int rw, const QByteArray& data = QByteArray(),
                 int sensor_id = -1,
                 std::function<void(const ControlFrame&)> callback = nullptr,
                 std::function<void()> error_callback = nullptr);
    void cancel_stale(int device_type, int cmd_code);

    void setup_connections();

    int m_sensor_id;
    QString m_target_host;
    int m_target_port;
    int m_recv_buffer = 128 * 1024 * 1024; // 128MB, 对齐 config.recv_buffer_size 默认值

    QMap<int, PendingRequest> m_pending;
    QMap<int, QVector<QByteArray>> m_sent_frames;

    UDPWorker* m_ctrl = nullptr;
    UDPDataThread* m_data = nullptr;
    QTimer* m_timeout_timer = nullptr;
    std::mt19937 m_rng;
    bool m_is_destroying = false;
    bool m_same_port_mode = false;  // control_port == data_port 时启用同端口模式
    bool m_ctrl_started = false;    // start_all() 后控制线程是否已启动 (数据线程就绪后启动)

    // 子线程就绪标志（由 sig_ready 信号驱动，wait_all_ready 消费）
    std::atomic<bool> m_ctrl_ready{false};
    std::atomic<bool> m_data_ready{false};
};

#endif // PROTOCOL_HANDLER_H