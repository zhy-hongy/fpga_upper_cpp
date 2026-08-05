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

// ── 控制命令覆盖：立即回调成功 ACK / 默认测试数据 ──

void MockProtocolHandler::set_exposure(int, int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback));
}
void MockProtocolHandler::read_exposure(int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback), be32(static_cast<uint32_t>(default_exposure())));
}
void MockProtocolHandler::set_gain(int, int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback));
}
void MockProtocolHandler::read_gain(int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback), be32(static_cast<uint32_t>(default_gain())));
}
void MockProtocolHandler::set_exposure_delay(int, int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback));
}
void MockProtocolHandler::read_exposure_delay(int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback), be32(static_cast<uint32_t>(default_exposure_delay())));
}
void MockProtocolHandler::set_sample_rate(int, int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback));
}
void MockProtocolHandler::read_sample_rate(int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback), be32(static_cast<uint32_t>(default_sample_rate())));
}
void MockProtocolHandler::set_trigger_mode(int, int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback));
}
void MockProtocolHandler::read_trigger_mode(int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback), be16(static_cast<uint16_t>(default_trigger_mode())));
}
void MockProtocolHandler::set_encoder_type(int, int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback));
}
void MockProtocolHandler::read_encoder_type(int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback), be16(static_cast<uint16_t>(default_encoder_type())));
}
void MockProtocolHandler::set_encoder_mode(int, int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback));
}
void MockProtocolHandler::read_encoder_mode(int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback), be16(static_cast<uint16_t>(default_encoder_input_mode())));
}
void MockProtocolHandler::set_encoder_interval(int, int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback));
}
void MockProtocolHandler::read_encoder_interval(int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback), be32(static_cast<uint32_t>(default_encoder_step())));
}
void MockProtocolHandler::set_external_trigger(bool, int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback));
}
void MockProtocolHandler::read_external_trigger(int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback), be16(static_cast<uint16_t>(default_external_trigger())));
}
void MockProtocolHandler::set_batch_length(int, int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback));
}
void MockProtocolHandler::read_batch_length(int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback), be16(static_cast<uint16_t>(default_batch_length())));
}
void MockProtocolHandler::set_batch_control(bool, int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback));
}
void MockProtocolHandler::reset_buffer(int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback));
}
void MockProtocolHandler::start_capture(int, std::function<void(const ControlFrame&)> callback) {
    reply(std::move(callback));
    start_mock_frames();
}
void MockProtocolHandler::stop_capture(int, std::function<void(const ControlFrame&)> callback) {
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
    ControlFrame frame;
    frame.device_type = DEV_SENSOR1;
    frame.cmd_code = 0;
    frame.msg_id = 0;
    frame.msg_type = MSG_TYPE_RESPONSE;
    frame.rw = RW_READ;
    frame.data = data;
    cb(frame);
}
