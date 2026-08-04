#ifndef PROTOCOL_HANDLER_H
#define PROTOCOL_HANDLER_H

#include <QObject>
#include <QMap>
#include <QTimer>
#include <functional>
#include <random>

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

    // Frame queue access
    cv::Mat get_latest_frame(int sensor_id = -1);
    std::deque<cv::Mat>& get_sensor_frame_queue(int sensor_id);
    int last_pkt_id() const;

    // High-level API - Camera controls
    void set_exposure(int us, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void read_exposure(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void set_gain(int gain, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void read_gain(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void set_frame_rate(int fps, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void set_exposure_delay(int delay_us, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void read_exposure_delay(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void set_sample_rate(int rate_hz, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void read_sample_rate(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void set_trigger_mode(int mode, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void read_trigger_mode(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void set_encoder_type(int enc_type, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void read_encoder_type(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void set_encoder_mode(int mode, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void read_encoder_mode(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void set_encoder_interval(int interval, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void read_encoder_interval(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void set_batch_length(int count, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void read_batch_length(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void get_batch_data(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void set_external_trigger(bool enabled, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void read_external_trigger(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void set_batch_control(bool start, int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void reset_buffer(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void start_capture(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);
    void stop_capture(int sensor_id, std::function<void(const ControlFrame&)> callback = nullptr);

    // FPGA version
    void read_fpga_version(std::function<void(const ControlFrame&)> callback = nullptr,
                           std::function<void()> error_callback = nullptr);

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
    bool send_transport(const QByteArray& frame);
    int send_cmd(int cmd_code, int rw, const QByteArray& data = QByteArray(),
                 int sensor_id = -1,
                 std::function<void(const ControlFrame&)> callback = nullptr,
                 std::function<void()> error_callback = nullptr);
    void cancel_stale(int device_type, int cmd_code);

    void setup_connections();

    int m_sensor_id;
    QString m_target_host;
    int m_target_port;
    int m_recv_buffer = 16 * 1024 * 1024;

    QMap<int, PendingRequest> m_pending;
    QMap<int, QVector<QByteArray>> m_sent_frames;

    UDPWorker* m_ctrl = nullptr;
    UDPDataThread* m_data = nullptr;
    QTimer* m_timeout_timer = nullptr;
    std::mt19937 m_rng;
    bool m_is_destroying = false;
};

#endif // PROTOCOL_HANDLER_H
