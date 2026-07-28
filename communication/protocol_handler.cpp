#include "protocol_handler.h"
#include "protocol/upgrade_frame.h"
#include "protocol/transport_frame.h"
#include "utils/logger.h"
#include <QDateTime>
#include <QUdpSocket>
#include <chrono>

static double steady_now() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

ProtocolHandler::ProtocolHandler(const Config& config, int sensor_id, QObject* parent)
    : QObject(parent), m_sensor_id(sensor_id),
      m_target_host(config.target_host), m_target_port(config.target_port),
      m_recv_buffer(config.recv_buffer_size),
      m_rng(std::random_device{}()) {

    QString bind_ip = config.host_ip;
    m_ctrl = new UDPWorker(bind_ip, config.control_port, 4 * 1024 * 1024, this);
    m_data = new UDPDataThread(bind_ip, config.data_port, config.recv_buffer_size, 4, this);

    setup_connections();

    m_timeout_timer = new QTimer(this);
    connect(m_timeout_timer, &QTimer::timeout, this, &ProtocolHandler::check_timeouts);
    m_timeout_timer->start(1000);
}

ProtocolHandler::~ProtocolHandler() {
    m_is_destroying = true;
    stop_all();
}

int ProtocolHandler::next_msg_id() {
    std::uniform_int_distribution<int> dist(0, 65535);
    return dist(m_rng);
}

void ProtocolHandler::setup_connections() {
    connect(m_ctrl, &UDPWorker::sig_cmd_received, this, &ProtocolHandler::on_raw_response);
    connect(m_ctrl, &UDPWorker::sig_retransmit_request, this, &ProtocolHandler::on_retransmit_request);
    connect(m_ctrl, &UDPWorker::sig_ready, this, [this]() { emit connected(); });
    connect(m_ctrl, &UDPWorker::sig_stopped, this, [this]() { emit disconnected(); });
    connect(m_ctrl, &UDPWorker::sig_error, this, [this](const QString& err) {
        emit error_occurred(err);
    });

    connect(m_data, &UDPDataThread::sig_cmd_received, this, &ProtocolHandler::on_raw_response);
    connect(m_data, &UDPDataThread::sig_frame_info, this, &ProtocolHandler::data_frame_info);
    connect(m_data, &UDPDataThread::sig_resolution, this, &ProtocolHandler::data_resolution);
    connect(m_data, &UDPDataThread::sig_error, this, [this](const QString& err) {
        emit error_occurred(err);
    });
}

void ProtocolHandler::on_raw_response(const QByteArray& raw) {
    // Check if it's a data packet
    if (raw.size() >= 12) {
        int data_len = (static_cast<uint8_t>(raw[10]) << 8) | static_cast<uint8_t>(raw[11]);
        if (data_len >= 1280) {
            m_data->feed_frame(raw);
            return;
        }
    }

    ControlFrame decoded;
    try {
        decoded = unpack_cmd_frame(raw);
    } catch (const std::exception& e) {
        if (raw.size() >= 12) {
            int dev = (static_cast<uint8_t>(raw[2]) << 8) | static_cast<uint8_t>(raw[3]);
            int cmd = (static_cast<uint8_t>(raw[4]) << 8) | static_cast<uint8_t>(raw[5]);
            int msg_id = (static_cast<uint8_t>(raw[6]) << 8) | static_cast<uint8_t>(raw[7]);
            emit status_message(QString("← Received frame (CRC fail) Dev=0x%1 Cmd=0x%2 MsgID=%3 Len=%4")
                .arg(dev, 4, 16, QChar('0')).arg(cmd, 4, 16, QChar('0')).arg(msg_id));
            emit status_message(QString("  Full frame (%1B): %2").arg(raw.size()).arg(QString(raw.toHex())));
        }
        emit error_occurred(e.what());
        return;
    }

    // Debug: print all received response frames
    emit status_message(QString("← Response Dev=0x%1 Cmd=0x%2 MsgID=%3 RW=0x%4 Len=%5")
        .arg(decoded.device_type, 4, 16, QChar('0'))
        .arg(decoded.cmd_code, 4, 16, QChar('0'))
        .arg(decoded.msg_id)
        .arg(decoded.rw, 2, 16, QChar('0'))
        .arg(decoded.data.size()));

    auto it = m_pending.find(decoded.msg_id);
    if (it != m_pending.end()) {
        auto cb = it->callback;
        int cmd = it->cmd_code;
        m_pending.erase(it);
        if (cb) {
            try {
                cb(decoded);
            } catch (const std::exception& e) {
                emit error_occurred(QString("Callback exception (cmd=0x%1): %2")
                    .arg(cmd, 4, 16, QChar('0')).arg(e.what()));
            }
        }
    } else {
        if (decoded.rw != RW_WRITE) {
            emit control_response(decoded);
        }
    }
}

void ProtocolHandler::on_retransmit_request(const QByteArray& raw) {
    TransportFrame info;
    try {
        info = unpack_transport_frame(raw);
    } catch (...) { return; }

    int frame_id = info.packet_id;
    auto it = m_sent_frames.find(frame_id);
    if (it == m_sent_frames.end()) {
        emit status_message(QString("Retransmit request frame_id=0x%1, but no cache").arg(frame_id, 4, 16, QChar('0')));
        return;
    }

    QVector<int> missing;
    if (info.data.size() >= 2) {
        int n = info.data.size() / 2;
        for (int i = 0; i < n; i++) {
            missing.append(
                (static_cast<uint8_t>(info.data[i * 2]) << 8) |
                static_cast<uint8_t>(info.data[i * 2 + 1]));
        }
    }

    if (!missing.isEmpty()) {
        emit status_message(QString("Retransmit: frame_id=0x%1 missing=%2")
            .arg(frame_id, 4, 16, QChar('0')).arg(missing.size()));
        for (int idx : missing) {
            if (idx >= 1 && idx <= it.value().size()) {
                m_ctrl->send_cmd(m_target_host, m_target_port, it.value()[idx - 1]);
            }
        }
    } else {
        for (const auto& tf : it.value()) {
            m_ctrl->send_cmd(m_target_host, m_target_port, tf);
        }
    }
}

void ProtocolHandler::check_timeouts() {
    double now = steady_now();

    if (!m_ctrl->is_ready()) {
        if (!m_pending.isEmpty()) {
            m_pending.clear();
            emit error_occurred("Network disconnected, all pending requests cancelled");
        }
        return;
    }

    QVector<int> expired;
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (now - it->timestamp > ACK_TIMEOUT)
            expired.append(it.key());
    }

    for (int mid : expired) {
        auto it = m_pending.find(mid);
        if (it == m_pending.end()) continue;

        if (it->retries_left > 0) {
            if (!m_ctrl->is_ready()) {
                m_pending.clear();
                emit error_occurred("Network disconnected, all pending requests cancelled");
                return;
            }

            int new_msg_id = next_msg_id();
            QByteArray frame = pack_cmd_frame(
                it->device_type, it->cmd_code, new_msg_id,
                MSG_TYPE_COMMAND, it->rw, it->data);

            if (!send_transport(frame)) {
                m_pending.remove(mid);
                if (it->error_callback) {
                    try { it->error_callback(); } catch (...) {}
                }
                continue;
            }

            PendingRequest req = it.value();
            req.msg_id = new_msg_id;
            req.timestamp = now;
            req.retries_left--;

            m_pending.remove(mid);
            m_pending[new_msg_id] = req;

            int retry_no = MAX_RETRIES - req.retries_left;
            emit status_message(QString("Cmd timeout (cmd=0x%1), retry #%2")
                .arg(req.cmd_code, 4, 16, QChar('0')).arg(retry_no));
        } else {
            auto cb = it->error_callback;
            int cmd = it->cmd_code;
            m_pending.remove(mid);
            if (cb) {
                try { cb(); } catch (...) {}
            }
            emit error_occurred(QString("Cmd timeout (cmd=0x%1), max retries reached")
                .arg(cmd, 4, 16, QChar('0')));
        }
    }
}

bool ProtocolHandler::send_transport(const QByteArray& frame) {
    if (!m_ctrl->is_ready()) {
        emit error_occurred("Network disconnected, send failed");
        return false;
    }

    QVector<QByteArray> transport_frames = split_into_transport_frames(frame);
    int total = transport_frames.size();

    if (!transport_frames.isEmpty()) {
        try {
            TransportFrame info = unpack_transport_frame(transport_frames[0]);
            m_sent_frames[info.packet_id] = transport_frames;
        } catch (...) {}
    }

    for (int i = 0; i < transport_frames.size(); i++) {
        if (!m_ctrl->send_cmd(m_target_host, m_target_port, transport_frames[i])) {
            emit error_occurred("Network disconnected, send failed");
            return false;
        }
        emit status_message(QString("  → Transport frame [%1/%2] len=%3B hex=%4")
            .arg(i + 1).arg(total).arg(transport_frames[i].size())
            .arg(QString(transport_frames[i].toHex())));
    }
    if (total > 1) {
        emit status_message(QString("  → Protocol frame split into %1 transport frames (original %2B)")
            .arg(total).arg(frame.size()));
    }
    return true;
}

int ProtocolHandler::send_cmd(int cmd_code, int rw, const QByteArray& data,
                               int sensor_id,
                               std::function<void(const ControlFrame&)> callback,
                               std::function<void()> error_callback) {
    int device_type = (sensor_id >= 0) ? sensor_id : m_sensor_id;
    int msg_id = next_msg_id();

    QByteArray frame = pack_cmd_frame(device_type, cmd_code, msg_id,
                                       MSG_TYPE_COMMAND, rw, data);

    if (!send_transport(frame)) {
        if (error_callback) {
            try { error_callback(); } catch (...) {}
        }
        return msg_id;
    }

    const char* action = (rw == RW_WRITE) ? "Write" : "Read";
    emit status_message(QString("→ Send cmd=0x%1 [%2] Dev=0x%3 msg_id=%4 len=%5 → %6:%7")
        .arg(cmd_code, 4, 16, QChar('0')).arg(action)
        .arg(device_type, 4, 16, QChar('0')).arg(msg_id).arg(frame.size())
        .arg(m_target_host).arg(m_target_port));
    emit status_message(QString("  Protocol frame (%1B): %2").arg(frame.size()).arg(QString(frame.toHex())));

    if (callback) {
        cancel_stale(device_type, cmd_code);
        PendingRequest req;
        req.msg_id = msg_id;
        req.cmd_code = cmd_code;
        req.callback = callback;
        req.error_callback = error_callback;
        req.timestamp = steady_now();
        req.retries_left = MAX_RETRIES;
        req.device_type = device_type;
        req.rw = rw;
        req.data = data;
        m_pending[msg_id] = req;
    }

    return msg_id;
}

void ProtocolHandler::cancel_stale(int device_type, int cmd_code) {
    QVector<int> stale;
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (it->device_type == device_type && it->cmd_code == cmd_code)
            stale.append(it.key());
    }
    for (int mid : stale) {
        m_pending.remove(mid);
    }
    if (!stale.isEmpty()) {
        emit status_message(QString("Cancelled %1 stale request(s) Dev=0x%2 Cmd=0x%3")
            .arg(stale.size()).arg(device_type, 4, 16, QChar('0'))
            .arg(cmd_code, 4, 16, QChar('0')));
    }
}

// ── High-level API ──

void ProtocolHandler::set_exposure(int us, int sensor_id, std::function<void(const ControlFrame&)> cb) {
    QByteArray payload;
    payload.append(static_cast<char>((us >> 24) & 0xFF));
    payload.append(static_cast<char>((us >> 16) & 0xFF));
    payload.append(static_cast<char>((us >> 8) & 0xFF));
    payload.append(static_cast<char>(us & 0xFF));
    send_cmd(CMD_EXPOSURE_TIME, RW_WRITE, payload, sensor_id, cb);
    emit status_message(QString("Set exposure: %1 us").arg(us));
}

void ProtocolHandler::read_exposure(int sensor_id, std::function<void(const ControlFrame&)> cb) {
    send_cmd(CMD_EXPOSURE_TIME, RW_READ, QByteArray(2, '\x00'), sensor_id, cb);
    emit status_message("Read exposure...");
}

void ProtocolHandler::set_gain(int gain, int sensor_id, std::function<void(const ControlFrame&)> cb) {
    QByteArray payload;
    payload.append(static_cast<char>((gain >> 24) & 0xFF));
    payload.append(static_cast<char>((gain >> 16) & 0xFF));
    payload.append(static_cast<char>((gain >> 8) & 0xFF));
    payload.append(static_cast<char>(gain & 0xFF));
    send_cmd(CMD_GAIN, RW_WRITE, payload, sensor_id, cb);
    emit status_message(QString("Set gain: %1").arg(gain));
}

void ProtocolHandler::read_gain(int sensor_id, std::function<void(const ControlFrame&)> cb) {
    send_cmd(CMD_GAIN, RW_READ, QByteArray(2, '\x00'), sensor_id, cb);
    emit status_message("Read gain...");
}

void ProtocolHandler::set_frame_rate(int fps, int sensor_id, std::function<void(const ControlFrame&)> cb) {
    QByteArray payload;
    payload.append(static_cast<char>((fps >> 24) & 0xFF));
    payload.append(static_cast<char>((fps >> 16) & 0xFF));
    payload.append(static_cast<char>((fps >> 8) & 0xFF));
    payload.append(static_cast<char>(fps & 0xFF));
    send_cmd(CMD_FRAME_RATE, RW_WRITE, payload, sensor_id, cb);
    emit status_message(QString("Set frame rate: %1 FPS").arg(fps));
}

void ProtocolHandler::set_exposure_delay(int delay_us, int sensor_id, std::function<void(const ControlFrame&)> cb) {
    QByteArray payload;
    payload.append(static_cast<char>((delay_us >> 24) & 0xFF));
    payload.append(static_cast<char>((delay_us >> 16) & 0xFF));
    payload.append(static_cast<char>((delay_us >> 8) & 0xFF));
    payload.append(static_cast<char>(delay_us & 0xFF));
    send_cmd(CMD_EXPOSURE_DELAY, RW_WRITE, payload, sensor_id, cb);
    emit status_message(QString("Set exposure delay (Cam%1): %2 us").arg(sensor_id).arg(delay_us));
}

void ProtocolHandler::set_sample_rate(int rate_hz, int sensor_id, std::function<void(const ControlFrame&)> cb) {
    QByteArray payload;
    payload.append(static_cast<char>((rate_hz >> 24) & 0xFF));
    payload.append(static_cast<char>((rate_hz >> 16) & 0xFF));
    payload.append(static_cast<char>((rate_hz >> 8) & 0xFF));
    payload.append(static_cast<char>(rate_hz & 0xFF));
    send_cmd(CMD_SAMPLE_RATE, RW_WRITE, payload, sensor_id, cb);
    emit status_message(QString("Set sample rate (Cam%1): %2 Hz").arg(sensor_id).arg(rate_hz));
}

void ProtocolHandler::set_trigger_mode(int mode, int sensor_id, std::function<void(const ControlFrame&)> cb) {
    QByteArray payload;
    payload.append(static_cast<char>((mode >> 8) & 0xFF));
    payload.append(static_cast<char>(mode & 0xFF));
    send_cmd(CMD_TRIGGER_MODE, RW_WRITE, payload, sensor_id, cb);
    const char* names[] = {"", "Continuous", "Encoder", "External"};
    emit status_message(QString("Set trigger mode (Cam%1): %2").arg(sensor_id).arg(mode >= 1 && mode <= 3 ? names[mode] : "Unknown"));
}

void ProtocolHandler::set_encoder_type(int enc_type, int sensor_id, std::function<void(const ControlFrame&)> cb) {
    QByteArray payload;
    payload.append(static_cast<char>((enc_type >> 8) & 0xFF));
    payload.append(static_cast<char>(enc_type & 0xFF));
    send_cmd(CMD_ENCODER_TYPE, RW_WRITE, payload, sensor_id, cb);
    emit status_message(QString("Set encoder type (Cam%1): %2").arg(sensor_id).arg(enc_type == 1 ? "Differential" : "Single-ended"));
}

void ProtocolHandler::set_encoder_mode(int mode, int sensor_id, std::function<void(const ControlFrame&)> cb) {
    QByteArray payload;
    payload.append(static_cast<char>((mode >> 8) & 0xFF));
    payload.append(static_cast<char>(mode & 0xFF));
    send_cmd(CMD_ENCODER_MODE, RW_WRITE, payload, sensor_id, cb);
    emit status_message(QString("Set encoder mode (Cam%1): %2").arg(sensor_id).arg(mode));
}

void ProtocolHandler::set_encoder_interval(int interval, int sensor_id, std::function<void(const ControlFrame&)> cb) {
    QByteArray payload;
    payload.append(static_cast<char>((interval >> 24) & 0xFF));
    payload.append(static_cast<char>((interval >> 16) & 0xFF));
    payload.append(static_cast<char>((interval >> 8) & 0xFF));
    payload.append(static_cast<char>(interval & 0xFF));
    send_cmd(CMD_ENCODER_INTERVAL, RW_WRITE, payload, sensor_id, cb);
    emit status_message(QString("Set encoder interval (Cam%1): %2").arg(sensor_id).arg(interval));
}

void ProtocolHandler::set_batch_length(int count, int sensor_id, std::function<void(const ControlFrame&)> cb) {
    QByteArray payload;
    payload.append(static_cast<char>((count >> 8) & 0xFF));
    payload.append(static_cast<char>(count & 0xFF));
    send_cmd(CMD_BATCH_LENGTH, RW_WRITE, payload, sensor_id, cb);
    emit status_message(QString("Set batch length (Cam%1): %2").arg(sensor_id).arg(count));
}

void ProtocolHandler::get_batch_data(int sensor_id, std::function<void(const ControlFrame&)> cb) {
    send_cmd(CMD_GET_BATCH_DATA, RW_READ, QByteArray(2, '\x00'), sensor_id, cb);
    emit status_message(QString("Get batch data (Cam%1)").arg(sensor_id));
}

void ProtocolHandler::set_external_trigger(bool enabled, int sensor_id, std::function<void(const ControlFrame&)> cb) {
    QByteArray payload(2, '\x00');
    if (enabled) payload[1] = 0x01;
    send_cmd(CMD_EXTERNAL_TRIGGER, RW_WRITE, payload, sensor_id, cb);
    emit status_message(QString("Set external trigger (Cam%1): %2").arg(sensor_id).arg(enabled ? "On" : "Off"));
}

void ProtocolHandler::set_batch_control(bool start, int sensor_id, std::function<void(const ControlFrame&)> cb) {
    QByteArray payload(2, '\x00');
    if (start) payload[1] = 0x01;
    send_cmd(CMD_BATCH_CONTROL, RW_WRITE, payload, sensor_id, cb);
    emit status_message(QString("Batch control (Cam%1): %2").arg(sensor_id).arg(start ? "Start" : "Stop"));
}

void ProtocolHandler::reset_buffer(int sensor_id, std::function<void(const ControlFrame&)> cb) {
    send_cmd(CMD_RESET_BUFFER, RW_WRITE, QByteArray(2, '\x00'), sensor_id, cb);
    emit status_message(QString("Reset buffer (Cam%1)").arg(sensor_id));
}

void ProtocolHandler::start_capture(int sensor_id, std::function<void(const ControlFrame&)> cb) {
    send_cmd(CMD_START_STOP, RW_WRITE, QByteArray(2, '\x00'), sensor_id, cb);
    emit status_message("Start capture");
}

void ProtocolHandler::stop_capture(int sensor_id, std::function<void(const ControlFrame&)> cb) {
    send_cmd(CMD_START_STOP, RW_WRITE, QByteArray(2, '\x00'), sensor_id, cb);
    emit status_message("Stop capture");
}

void ProtocolHandler::read_fpga_version(std::function<void(const ControlFrame&)> cb,
                                         std::function<void()> error_cb) {
    send_cmd(CMD_READ_VERSION, RW_READ, QByteArray(2, '\x00'), DEV_FPGA, cb, error_cb);
    emit status_message("Read FPGA version...");
}

// ── Firmware upgrade ──

void ProtocolHandler::send_remote_update_request(const QString& file_path,
                                                   std::function<void(const ControlFrame&)> cb,
                                                   std::function<void()> error_cb) {
    int file_size = get_firmware_file_size(file_path);
    int file_crc = compute_file_crc(file_path);
    int msg_id = next_msg_id();

    QByteArray frame = pack_remote_update_request(msg_id, file_size, file_crc);

    if (!send_transport(frame)) {
        if (error_cb) { try { error_cb(); } catch (...) {} }
        return;
    }

    emit status_message(QString("→ Send remote update request [size=%1B, CRC=0x%2]")
        .arg(file_size).arg(file_crc, 4, 16, QChar('0')));
    emit status_message(QString("  Protocol frame (%1B): %2").arg(frame.size()).arg(QString(frame.toHex())));

    if (cb) {
        PendingRequest req;
        req.msg_id = msg_id;
        req.cmd_code = CMD_REMOTE_UPDATE_REQUEST;
        req.callback = cb;
        req.error_callback = error_cb;
        req.timestamp = steady_now();
        req.retries_left = MAX_RETRIES;
        req.device_type = DEV_FPGA;
        req.rw = RW_WRITE;
        m_pending[msg_id] = req;
    }
}

void ProtocolHandler::send_remote_update_control(bool start,
                                                   std::function<void(const ControlFrame&)> cb,
                                                   std::function<void()> error_cb) {
    uint8_t ctrl_cmd = start ? UPDATE_CTRL_START : UPDATE_CTRL_ABORT;
    int msg_id = next_msg_id();

    QByteArray frame = pack_remote_update_control(msg_id, ctrl_cmd);

    if (!send_transport(frame)) {
        if (error_cb) { try { error_cb(); } catch (...) {} }
        return;
    }

    emit status_message(QString("→ Send remote update control [%1]").arg(start ? "Start" : "Abort"));
    emit status_message(QString("  Protocol frame (%1B): %2").arg(frame.size()).arg(QString(frame.toHex())));

    if (cb) {
        PendingRequest req;
        req.msg_id = msg_id;
        req.cmd_code = CMD_REMOTE_UPDATE_CONTROL;
        req.callback = cb;
        req.error_callback = error_cb;
        req.timestamp = steady_now();
        req.retries_left = MAX_RETRIES;
        req.device_type = DEV_FPGA;
        req.rw = RW_WRITE;
        m_pending[msg_id] = req;
    }
}

void ProtocolHandler::send_remote_update_data(int chunk_index, const QByteArray& chunk_data,
                                                std::function<void(const ControlFrame&)> cb,
                                                std::function<void()> error_cb) {
    int msg_id = next_msg_id();
    QByteArray frame = pack_remote_update_data(msg_id, chunk_index, chunk_data);

    if (!send_transport(frame)) {
        if (error_cb) { try { error_cb(); } catch (...) {} }
        return;
    }

    emit status_message(QString("→ Send remote update data chunk [%1] len=%2B")
        .arg(chunk_index).arg(chunk_data.size()));
    emit status_message(QString("  Protocol frame (%1B): %2").arg(frame.size()).arg(QString(frame.toHex())));

    if (cb) {
        PendingRequest req;
        req.msg_id = msg_id;
        req.cmd_code = CMD_REMOTE_UPDATE_DATA;
        req.callback = cb;
        req.error_callback = error_cb;
        req.timestamp = steady_now();
        req.retries_left = MAX_RETRIES;
        req.device_type = DEV_FPGA;
        req.rw = RW_WRITE;
        req.data = chunk_data;
        m_pending[msg_id] = req;
    }
}

void ProtocolHandler::send_remote_update_result(std::function<void(const ControlFrame&)> cb,
                                                  std::function<void()> error_cb) {
    int msg_id = next_msg_id();
    QByteArray frame = pack_remote_update_result(msg_id);

    if (!send_transport(frame)) {
        if (error_cb) { try { error_cb(); } catch (...) {} }
        return;
    }

    emit status_message("→ Send remote update result query");
    emit status_message(QString("  Protocol frame (%1B): %2").arg(frame.size()).arg(QString(frame.toHex())));

    if (cb) {
        PendingRequest req;
        req.msg_id = msg_id;
        req.cmd_code = CMD_REMOTE_UPDATE_RESULT;
        req.callback = cb;
        req.error_callback = error_cb;
        req.timestamp = steady_now();
        req.retries_left = MAX_RETRIES;
        req.device_type = DEV_FPGA;
        req.rw = RW_READ;
        m_pending[msg_id] = req;
    }
}

// ── Frame queue access ──

cv::Mat ProtocolHandler::get_latest_frame(int sensor_id) {
    return m_data->get_latest_frame(sensor_id);
}

std::deque<cv::Mat>& ProtocolHandler::get_sensor_frame_queue(int sensor_id) {
    return m_data->get_sensor_queue(sensor_id);
}

int ProtocolHandler::last_pkt_id() const {
    return m_data->last_pkt_id();
}

// ── Lifecycle ──

void ProtocolHandler::start_all() {
    m_ctrl->start();
    m_data->start();
    emit status_message(QString("Protocol handler started (ctrl:%1, data:%2)")
        .arg(m_ctrl->port()).arg(m_data->port()));
}

void ProtocolHandler::stop_all() {
    m_ctrl->stop();
    m_data->stop();
    m_timeout_timer->stop();
    m_pending.clear();

    // Only emit signal if not destroying (to avoid signal during object destruction)
    if (!m_is_destroying) {
        emit status_message("Protocol handler stopped");
    }
}

void ProtocolHandler::set_network_params(const QString& target_host, int target_port, int local_port) {
    stop_all();

    m_target_host = target_host;
    m_target_port = target_port;

    delete m_ctrl;
    delete m_data;

    m_ctrl = new UDPWorker("0.0.0.0", local_port, 4 * 1024 * 1024, this);
    m_data = new UDPDataThread("0.0.0.0", m_target_port, m_recv_buffer, 4, this);

    setup_connections();
}
