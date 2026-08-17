#include "udp_data.h"
#include <QElapsedTimer>
#include <QDebug>
#include <QDateTime>
#include <opencv2/imgproc.hpp>

UDPDataThread::UDPDataThread(const QString& host, int port, int recv_buffer, int max_queue, QObject* parent)
    : QThread(parent), m_host(host), m_port(port), m_recv_buffer(recv_buffer) {
    for (int sid : SENSOR_IDS) {
        m_frame_queues[sid] = std::deque<cv::Mat>();
        m_assemblies[sid] = SensorAssembly();
    }
    m_shared_queue = std::deque<cv::Mat>(max_queue);
    m_fps_timer.start();
}

UDPDataThread::~UDPDataThread() {
    stop();
}

void UDPDataThread::run() {
    m_running = true;
    m_fps_timer.start();
    m_sock = new QUdpSocket();

    if (!m_sock->bind(QHostAddress(m_host), m_port, QAbstractSocket::ReuseAddressHint)) {
        emit sig_error("Data channel bind failed: " + m_sock->errorString());
        m_sock->deleteLater();
        m_sock = nullptr;
        m_running = false;
        return;
    }
    m_sock->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, m_recv_buffer);

    emit sig_ready();

    int recv_count = 0;
    QElapsedTimer last_hex_log;
    last_hex_log.start();

    while (m_running) {
        if (m_sock->waitForReadyRead(1000)) {
            while (m_sock->hasPendingDatagrams()) {
                QByteArray data;
                data.resize(static_cast<int>(m_sock->pendingDatagramSize()));
                QHostAddress sender;
                quint16 sender_port;
                m_sock->readDatagram(data.data(), data.size(), &sender, &sender_port);
                recv_count++;

                if (m_fpga_addr.first.isNull()) {
                    m_fpga_addr = qMakePair(sender, sender_port);
                }

                if (recv_count <= 3 || last_hex_log.elapsed() > 5000) {
                    emit sig_error(QString("DBG: data recv #%1 len=%2 from=%3 head=%4")
                        .arg(recv_count).arg(data.size()).arg(sender.toString())
                        .arg(data.size() >= 2 ? QString(data.left(2).toHex()) : "??"));
                    last_hex_log.restart();
                }

                if (data.size() >= 2 &&
                    static_cast<uint8_t>(data[0]) == TRANSPORT_HEADER_BYTES[0] &&
                    static_cast<uint8_t>(data[1]) == TRANSPORT_HEADER_BYTES[1]) {
                    auto result = m_transport_asm.feed(data);
                    if (!result.retransmit_frame.isNull() && !m_fpga_addr.first.isNull()) {
                        send_retransmit(result.retransmit_frame);
                    }
                    if (!result.assembled_data.isNull()) {
                        QMutexLocker locker(&m_lock);
                        handle_frame(result.assembled_data);
                    }
                }
            }
        } else {
            check_retransmit();
        }
    }

    if (m_sock) {
        m_sock->close();
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    emit sig_stopped();
}

void UDPDataThread::send_retransmit(const QByteArray& frame) {
    if (m_sock && !m_fpga_addr.first.isNull()) {
        m_sock->writeDatagram(frame, m_fpga_addr.first, m_fpga_addr.second);
    }
}

void UDPDataThread::check_retransmit() {
    auto result = m_transport_asm.check_retransmit();
    if (!result.retransmit_frame.isNull() && !m_fpga_addr.first.isNull()) {
        send_retransmit(result.retransmit_frame);
        if (!result.missing_packets.isEmpty()) {
            emit sig_error(QString("Request retransmit: frame_id=0x%1 missing=%2 packets")
                .arg(result.frame_id, 4, 16, QChar('0'))
                .arg(result.missing_packets.size()));
        }
    }
}

void UDPDataThread::feed_frame(const QByteArray& raw) {
    QMutexLocker locker(&m_lock);
    handle_frame(raw);
}

void UDPDataThread::handle_frame(const QByteArray& raw) {
    auto read16 = [&](int offset) -> uint16_t {
        return (static_cast<uint8_t>(raw[offset]) << 8) |
               static_cast<uint8_t>(raw[offset + 1]);
    };
    auto read32 = [&](int offset) -> uint32_t {
        return (static_cast<uint32_t>(static_cast<uint8_t>(raw[offset])) << 24) |
               (static_cast<uint32_t>(static_cast<uint8_t>(raw[offset + 1])) << 16) |
               (static_cast<uint32_t>(static_cast<uint8_t>(raw[offset + 2])) << 8) |
               static_cast<uint32_t>(static_cast<uint8_t>(raw[offset + 3]));
    };

    int dev = read16(2);
    int cmd = read16(4);
    int pkt_id = read16(6);
    int data_len = static_cast<int>(read32(10));

    if (data_len < DATA_PER_PACKET) {
        // 数据线程不处理控制帧——控制帧的 ACK 通过 UDPWorker（控制端口）接收。
        // 如果在此处 emit sig_cmd_received，当 FPGA 未运行时，控制命令自环回
        // 会导致"写入成功但实际无应答"的假阳性。
        return;
    }

    bool valid_sensor = false;
    for (int sid : SENSOR_IDS) { if (dev == sid) { valid_sensor = true; break; } }
    if (!valid_sensor) return;

    SensorAssembly& asm_ = m_assemblies[dev];
    QByteArray payload = raw.mid(14, data_len);

    // Check if first packet (with F05AA50F header)
    bool is_first = (data_len >= FIRST_PACKET_EXTRA &&
                     payload.size() >= 4 &&
                     static_cast<uint8_t>(payload[0]) == FIRST_PKT_HEADER[0] &&
                     static_cast<uint8_t>(payload[1]) == FIRST_PKT_HEADER[1] &&
                     static_cast<uint8_t>(payload[2]) == FIRST_PKT_HEADER[2] &&
                     static_cast<uint8_t>(payload[3]) == FIRST_PKT_HEADER[3]);

    if (is_first) {
        int w = read16(4);
        int h = read16(6);
        asm_.image_width = w;
        asm_.image_height = h;
        asm_.expected_packets = h;
        asm_.cmd_code = static_cast<uint16_t>(cmd);  // 保存 FPGA 命令码，供 assemble_and_push 设置 FramePacket.cmd_code

        emit sig_resolution(dev, w, h);

        if (!asm_.packets.isEmpty()) {
            assemble_and_push(dev);
        }

        asm_.packets[0] = payload;
        asm_.packet_seq = 1;
        asm_.last_pkt_id = pkt_id;
        m_last_received_pkt_id = pkt_id;
        return;
    }

    // Normal data packet
    if (asm_.last_pkt_id >= 0) {
        int gap = pkt_id - asm_.last_pkt_id - 1;
        if (gap > 0) {
            m_dropped_packets += gap;
            if (gap >= FORCE_RESET_GAP) {
                asm_.reset();
                return;
            }
        }
    }

    asm_.packets[asm_.packet_seq] = payload;
    asm_.packet_seq++;
    asm_.last_pkt_id = pkt_id;
    m_last_received_pkt_id = pkt_id;

    if (asm_.packet_seq >= asm_.expected_packets) {
        assemble_and_push(dev);
    }
}

void UDPDataThread::assemble_and_push(int sensor_id) {
    SensorAssembly& asm_ = m_assemblies[sensor_id];
    int w = asm_.image_width;
    int h = asm_.image_height;
    int n_packets = asm_.expected_packets;

    QByteArray raw;
    raw.reserve(w * h);

    for (int i = 0; i < n_packets; i++) {
        auto it = asm_.packets.find(i);
        if (it == asm_.packets.end()) {
            raw.append(QByteArray(DATA_PER_PACKET, '\x00'));
        } else if (i == 0) {
            raw.append(it.value().mid(FIRST_PACKET_EXTRA));
        } else {
            raw.append(it.value());
        }
    }

    raw = raw.left(w * h);

    cv::Mat temp_mat(h, w, CV_8UC1, raw.data());
    cv::Mat img = temp_mat.clone();
    if (img.empty()) {
        asm_.reset();
        return;
    }

    // Push to per-sensor queue
    auto& queue = m_frame_queues[sensor_id];
    queue.push_back(img);
    if (queue.size() > 4) queue.pop_front();

    // Shared queue
    m_shared_queue.push_back(img);
    if (m_shared_queue.size() > 4) m_shared_queue.pop_front();

    // Emit FramePacket to SDK consumers (queued connection copies the packet).
    // Metadata rules: current FPGA protocol has no batchId/lineIndex/encoder/timestamp,
    // so those stay 0 and their sources are marked accordingly -- never faked.
    FramePacket pkt;
    pkt.frameId = ++m_frame_id_seq;
    pkt.sensorId = static_cast<uint16_t>(sensor_id);
    pkt.batchId = 0;
    pkt.lineIndex = 0;
    pkt.encoder = 0;
    pkt.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch() * 1000);
    pkt.frameIdSource = META_SOURCE_SDK;
    pkt.encoderSource = META_SOURCE_UNKNOWN;
    pkt.timestampSource = META_SOURCE_SDK;
    pkt.width = static_cast<uint16_t>(w);
    pkt.height = static_cast<uint16_t>(h);
    pkt.cmd_code = asm_.cmd_code;  // FPGA 命令码 → frame_to_info 自动识别 PixelFormat
    pkt.data = raw;
    emit sig_frame_ready(pkt);

    // Statistics (1 Hz)
    m_frame_count++;
    m_fps_counter++;
    qint64 elapsed = m_fps_timer.elapsed();
    if (elapsed >= 1000) {
        m_fps = static_cast<int>(m_fps_counter * 1000.0 / elapsed);
        m_fps_counter = 0;
        m_fps_timer.restart();
        emit sig_frame_info(m_fps, m_frame_count, m_dropped_packets);
    }

    asm_.reset();
}

cv::Mat UDPDataThread::get_latest_frame(int sensor_id) {
    if (sensor_id > 0) {
        auto it = m_frame_queues.find(sensor_id);
        if (it != m_frame_queues.end() && !it.value().empty()) {
            return it.value().back();
        }
        return cv::Mat();
    }
    if (!m_shared_queue.empty()) {
        return m_shared_queue.back();
    }
    return cv::Mat();
}

std::deque<cv::Mat>& UDPDataThread::get_sensor_queue(int sensor_id) {
    static std::deque<cv::Mat> empty;
    auto it = m_frame_queues.find(sensor_id);
    if (it != m_frame_queues.end()) return it.value();
    return empty;
}

void UDPDataThread::stop() {
    m_running = false;
    // 不在调用线程 close()：socket 由 UDPDataThread 线程拥有，跨线程 close 触发
    // Qt 断言。run() 循环退出时会在本线程内关闭并清理 socket。
    wait(3000);
}
