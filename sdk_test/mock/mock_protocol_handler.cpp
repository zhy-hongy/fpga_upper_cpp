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
    m_batch_length = count;  // 记录值，供 get_batch_data 生成帧
    emit_command(CMD_BATCH_LENGTH, RW_WRITE, be16(static_cast<uint16_t>(count)), sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::read_batch_length(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_BATCH_LENGTH, RW_READ, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback), be16(static_cast<uint16_t>(m_batch_length)));
}
void MockProtocolHandler::get_batch_data(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_GET_BATCH_DATA, RW_READ, QByteArray(2, '\0'), sensor_id);

    // 生成 m_batch_length 帧模拟点云数据（CMD_DATA_POINTCLOUD），
    // 在 ACK 之前同步 emit，确保 Cam_ReceiveBatchData 能收到数据帧。
    const int count = m_batch_length;
    for (int i = 0; i < count; ++i) {
        FramePacket pkt;
        pkt.frameId = m_frame_id.fetch_add(1) + 1;
        pkt.sensorId = static_cast<uint16_t>(sensor_id > 0 ? sensor_id : DEV_SENSOR1);
        pkt.batchId = 0;
        pkt.lineIndex = static_cast<uint32_t>(i);
        pkt.encoder = 0;
        pkt.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000;
        pkt.frameIdSource = META_SOURCE_SDK;
        pkt.timestampSource = META_SOURCE_SDK;
        pkt.encoderSource = META_SOURCE_UNKNOWN;
        pkt.width = 1280;        // 1280 点/行
        pkt.height = 1;
        pkt.cmd_code = CMD_DATA_POINTCLOUD;  // 3D 点云类型
        // 每点 3 个 float (x,y,z) = 12 字节，总计 1280 * 12 = 15360 字节
        pkt.data = QByteArray(1280 * 12, static_cast<char>(0xAB));
        emit frame_ready(pkt);
    }

    reply(std::move(callback), QByteArray(128, '\0'));  // 128 字节零填充默认批处理响应
}
void MockProtocolHandler::set_batch_control(bool start, int sensor_id, std::function<void(const ControlFrame&)> callback) {
    // 0x0204 写数据 4B：高 2B 开关(0x0001 启动 / 0x0000 停止)，低 2B 批次序号(置 0)
    QByteArray payload(4, '\0');
    if (start) payload[1] = 0x01;
    emit_command(CMD_BATCH_CONTROL, RW_WRITE, payload, sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::reset_buffer(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_RESET_BUFFER, RW_WRITE, QByteArray(2, '\0'), sensor_id);
    reply(std::move(callback));
}
void MockProtocolHandler::start_capture(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_BATCH_CONTROL, RW_WRITE, QByteArray("\x00\x01\x00\x00", 4), sensor_id);
    reply(std::move(callback));
    start_mock_frames();
}
void MockProtocolHandler::stop_capture(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_BATCH_CONTROL, RW_WRITE, QByteArray(4, '\0'), sensor_id);
    reply(std::move(callback));
    stop_mock_frames();
}

// ── 设备信息读取覆盖：记录命令 + 返回默认测试数据 ──

void MockProtocolHandler::read_camera_version(int sensor_id, std::function<void(const ControlFrame&)> callback) {
    emit_command(CMD_CAMERA_VERSION, RW_READ, QByteArray("\x00\x01", 2), sensor_id);
    reply(std::move(callback), QByteArray("\x01\x00\x00\x01", 4));  // "1.0.0.1"
}
void MockProtocolHandler::read_fpga_version(std::function<void(const ControlFrame&)> callback,
                                            std::function<void()> error_callback) {
    emit_command(CMD_FPGA_VERSION, RW_READ, QByteArray(2, '\0'), DEV_FPGA);
    reply(std::move(callback), QByteArray("\x01\x00\x00\x01", 4));  // "1.0.0.1"
    (void)error_callback;
}
void MockProtocolHandler::read_device_serial(std::function<void(const ControlFrame&)> callback,
                                             std::function<void()> error_callback) {
    emit_command(CMD_DEVICE_SERIAL, RW_READ, QByteArray(2, '\0'), DEV_FPGA);
    reply(std::move(callback), be32(0x12345678));
    (void)error_callback;
}
void MockProtocolHandler::set_device_serial(uint32_t serial, std::function<void(const ControlFrame&)> callback,
                                            std::function<void()> error_callback) {
    emit_command(CMD_DEVICE_SERIAL, RW_WRITE, be32(serial), DEV_FPGA);
    reply(std::move(callback));
    (void)error_callback;
}
void MockProtocolHandler::read_network_config(std::function<void(const ControlFrame&)> callback,
                                              std::function<void()> error_callback) {
    // 24B：设备IP + 本机IP + 掩码 + 网关 + 设备MAC(2B 预留 + 6B)
    QByteArray payload;
    payload.append(be32(0xC0A80A0A));   // 192.168.10.10
    payload.append(be32(0xC0A80A64));   // 192.168.10.100
    payload.append(be32(0xFFFFFF00));   // 255.255.255.0
    payload.append(be32(0xC0A80A01));   // 192.168.10.1
    payload.append(QByteArray(2, '\0'));  // MAC 前 2B 预留
    payload.append(QByteArray("\x00\x00\x02\x00\x00\x01", 6));
    emit_command(CMD_NETWORK_CONFIG, RW_READ, QByteArray(2, '\0'), DEV_FPGA);
    reply(std::move(callback), payload);
    (void)error_callback;
}
void MockProtocolHandler::set_network_config(uint32_t dev_ip, uint32_t pc_ip, uint32_t mask, uint32_t gateway,
                                             const uint8_t mac[6], std::function<void(const ControlFrame&)> callback,
                                             std::function<void()> error_callback) {
    QByteArray payload;
    payload.append(be32(dev_ip));
    payload.append(be32(pc_ip));
    payload.append(be32(mask));
    payload.append(be32(gateway));
    payload.append(QByteArray(2, '\0'));  // MAC 前 2B 预留
    for (int i = 0; i < 6; ++i) payload.append(static_cast<char>(mac[i]));
    emit_command(CMD_NETWORK_CONFIG, RW_WRITE, payload, DEV_FPGA);
    reply(std::move(callback));
    (void)error_callback;
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
