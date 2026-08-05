#include "mock_protocol_handler.h"

#include <QDateTime>

#include <utility>

MockProtocolHandler::MockProtocolHandler(const Config& config, QObject* parent)
    : ProtocolHandler(config, DEV_SENSOR1, parent),
      m_frame_timer(new QTimer(this)) {
    // 基类已创建 UDPWorker/UDPDataThread 对象，但【不 start_all()】：
    // 两个线程不启动、不 bind、不 new QUdpSocket → 无任何 UDP 活动。
    connect(m_frame_timer, &QTimer::timeout, this, [this] { generate_frame(); });
}

// ── 发送命令捕获：复用真实打包路径（build_command_frame + split_into_transport_frames）──

QByteArray MockProtocolHandler::emit_command(int cmd_code, int rw,
                                             const QByteArray& data,
                                             int sensor_id) {
    int msg_id = 0;
    const QByteArray ctrl = build_command_frame(cmd_code, rw, data, sensor_id, &msg_id);
    const int device_type = (sensor_id >= 0) ? sensor_id : DEV_SENSOR1;
    const QVector<QByteArray> tfs = split_into_transport_frames(ctrl);

    QByteArray first;
    for (const QByteArray& tf : tfs) {
        record_sent(cmd_code, device_type, rw, data, tf);
        if (first.isEmpty()) first = tf;
    }
    return first;
}

void MockProtocolHandler::record_sent(int cmd_code, int device_type, int rw,
                                      const QByteArray& data,
                                      const QByteArray& transport_frame) {
    QMutexLocker locker(&m_cmd_mutex);
    m_sent_cmds.append({cmd_code, device_type, rw, data, transport_frame});
}

QVector<MockSentCmd> MockProtocolHandler::take_sent_commands() {
    QMutexLocker locker(&m_cmd_mutex);
    QVector<MockSentCmd> out = m_sent_cmds;
    m_sent_cmds.clear();
    return out;
}

int MockProtocolHandler::sent_command_count() const {
    QMutexLocker locker(&m_cmd_mutex);
    return m_sent_cmds.size();
}

// ── 控制命令覆盖：立即回调成功 ACK / 默认测试数据，并记录"已发送"的命令帧 ──

void MockProtocolHandler::set_exposure(int us, int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_EXPOSURE_TIME, RW_WRITE, be32(static_cast<uint32_t>(us)), sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::read_exposure(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_EXPOSURE_TIME, RW_READ, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback), be32(static_cast<uint32_t>(default_exposure())));
}
void MockProtocolHandler::set_gain(int gain, int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_GAIN, RW_WRITE, be32(static_cast<uint32_t>(gain)), sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::read_gain(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_GAIN, RW_READ, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback), be32(static_cast<uint32_t>(default_gain())));
}
void MockProtocolHandler::set_exposure_delay(int delay_us, int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_EXPOSURE_DELAY, RW_WRITE, be32(static_cast<uint32_t>(delay_us)), sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::read_exposure_delay(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_EXPOSURE_DELAY, RW_READ, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback), be32(static_cast<uint32_t>(default_exposure_delay())));
}
void MockProtocolHandler::set_sample_rate(int rate_hz, int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_SAMPLE_RATE, RW_WRITE, be32(static_cast<uint32_t>(rate_hz)), sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::read_sample_rate(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_SAMPLE_RATE, RW_READ, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback), be32(static_cast<uint32_t>(default_sample_rate())));
}
void MockProtocolHandler::set_trigger_mode(int mode, int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_TRIGGER_MODE, RW_WRITE, be16(static_cast<uint16_t>(mode)), sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::read_trigger_mode(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_TRIGGER_MODE, RW_READ, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback), be16(static_cast<uint16_t>(default_trigger_mode())));
}
void MockProtocolHandler::set_encoder_type(int enc_type, int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_ENCODER_TYPE, RW_WRITE, be16(static_cast<uint16_t>(enc_type)), sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::read_encoder_type(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_ENCODER_TYPE, RW_READ, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback), be16(static_cast<uint16_t>(default_encoder_type())));
}
void MockProtocolHandler::set_encoder_mode(int mode, int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_ENCODER_MODE, RW_WRITE, be16(static_cast<uint16_t>(mode)), sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::read_encoder_mode(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_ENCODER_MODE, RW_READ, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback), be16(static_cast<uint16_t>(default_encoder_input_mode())));
}
void MockProtocolHandler::set_encoder_interval(int interval, int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_ENCODER_INTERVAL, RW_WRITE, be32(static_cast<uint32_t>(interval)), sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::read_encoder_interval(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_ENCODER_INTERVAL, RW_READ, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback), be32(static_cast<uint32_t>(default_encoder_step())));
}
void MockProtocolHandler::set_external_trigger(bool enabled, int sensor_id, std::function<void(const ControlFrame&)> callback) {
    QByteArray payload(2, '\0');
    if (enabled) payload[1] = 0x01;
    emit_command(CMD_EXTERNAL_TRIGGER, RW_WRITE, payload, sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::read_external_trigger(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_EXTERNAL_TRIGGER, RW_READ, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback), be16(static_cast<uint16_t>(default_external_trigger())));
}
void MockProtocolHandler::set_batch_length(int count, int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_BATCH_LENGTH, RW_WRITE, be16(static_cast<uint16_t>(count)), sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::read_batch_length(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_BATCH_LENGTH, RW_READ, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback), be16(static_cast<uint16_t>(default_batch_length())));
}
void MockProtocolHandler::set_batch_control(bool start, int sensor_id, std::function<void(const ControlFrame&)> callback) {
    QByteArray payload(2, '\0');
    if (start) payload[1] = 0x01;
    emit_command(CMD_BATCH_CONTROL, RW_WRITE, payload, sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::reset_buffer(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_RESET_BUFFER, RW_WRITE, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::start_capture(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_START_STOP, RW_WRITE, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback));
    start_mock_frames();
}
void MockProtocolHandler::stop_capture(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_START_STOP, RW_WRITE, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback));
    stop_mock_frames();
}

// ── Mock 帧生成 ──

void MockProtocolHandler::start_mock_frames() {
    m_capturing.store(true);
    m_frame_timer->start(40);   // 40ms/帧
}

void MockProtocolHandler::stop_mock_frames() {
    m_frame_timer->stop();
    m_capturing.store(false);
}

void MockProtocolHandler::generate_frame() {
    if (!m_capturing.load()) return;

    const uint32_t frameId = m_frame_id.fetch_add(1) + 1;
    FramePacket pkt;
    pkt.frameId = frameId;
    pkt.sensorId = static_cast<uint16_t>(DEV_SENSOR1);   // 固定 sensor 1
    pkt.batchId = 0;
    pkt.lineIndex = 0;
    pkt.encoder = 0;
    pkt.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000;  // us
    pkt.frameIdSource = META_SOURCE_SDK;
    pkt.timestampSource = META_SOURCE_SDK;
    pkt.encoderSource = META_SOURCE_UNKNOWN;
    pkt.width = 1280;
    pkt.height = 1024;
    pkt.data = build_frame();

    // 直连连接 → SDK DataDispatcher::enqueue（线程安全），CallbackWorker 消费。
    emit frame_ready(pkt);
}

QByteArray MockProtocolHandler::build_frame() {
    // 固定值填充 1280×1024 单帧（MONO8）
    return QByteArray(1280 * 1024, static_cast<char>(0x80));
}

QByteArray MockProtocolHandler::be32(uint32_t v) {
    QByteArray d(4, Qt::Uninitialized);
    d[0] = static_cast<char>((v >> 24) & 0xFF);
    d[1] = static_cast<char>((v >> 16) & 0xFF);
    d[2] = static_cast<char>((v >> 8) & 0xFF);
    d[3] = static_cast<char>(v & 0xFF);
    return d;
}

QByteArray MockProtocolHandler::be16(uint16_t v) {
    QByteArray d(2, Qt::Uninitialized);
    d[0] = static_cast<char>((v >> 8) & 0xFF);
    d[1] = static_cast<char>(v & 0xFF);
    return d;
}

void MockProtocolHandler::reply(std::function<void(const ControlFrame&)> cb,
                                const QByteArray& data) {
    if (!cb) return;
    if (m_silent_acks.load()) return;   // 静默模式：模拟无 ACK（超时测试）
    ControlFrame frame;
    frame.device_type = DEV_SENSOR1;
    frame.cmd_code = 0;
    frame.msg_id = 0;
    frame.msg_type = MSG_TYPE_RESPONSE;
    frame.rw = RW_READ;
    frame.data = data;
    cb(frame);
}
