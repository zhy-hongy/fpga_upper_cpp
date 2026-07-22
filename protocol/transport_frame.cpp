#include "transport_frame.h"
#include "crc16.h"
#include <random>
#include <stdexcept>
#include <chrono>
#include <QDebug>

static double steady_now() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static int next_packet_id() {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<int> dist(0, 65535);
    return dist(rng);
}

QByteArray pack_transport_frame(const QByteArray& data, int packet_id,
                                 int total_packets, int current_packet,
                                 int retransmit) {
    QByteArray body;
    auto append16 = [&](uint16_t v) {
        body.append(static_cast<char>((v >> 8) & 0xFF));
        body.append(static_cast<char>(v & 0xFF));
    };
    auto append32 = [&](uint32_t v) {
        body.append(static_cast<char>((v >> 24) & 0xFF));
        body.append(static_cast<char>((v >> 16) & 0xFF));
        body.append(static_cast<char>((v >> 8) & 0xFF));
        body.append(static_cast<char>(v & 0xFF));
    };
    append16(static_cast<uint16_t>(packet_id));
    append16(static_cast<uint16_t>(total_packets));
    append16(static_cast<uint16_t>(current_packet));
    append32(static_cast<uint32_t>(retransmit));
    append32(0);
    append16(static_cast<uint16_t>(data.size()));
    body.append(data);

    uint16_t crc = crc16_modbus(body);

    QByteArray frame;
    frame.append(static_cast<char>(0xAB));
    frame.append(static_cast<char>(0xAB));
    frame.append(body);
    append16(crc);
    frame.append(static_cast<char>(0xDC));
    frame.append(static_cast<char>(0xDC));
    return frame;
}

QByteArray pack_retransmit_request(int frame_id, const QVector<int>& missing_packets) {
    QByteArray data;
    for (int p : missing_packets) {
        data.append(static_cast<char>((p >> 8) & 0xFF));
        data.append(static_cast<char>(p & 0xFF));
    }
    return pack_transport_frame(data, frame_id, 0, 0, RETRANSMIT_REQUEST);
}

TransportFrame unpack_transport_frame(const QByteArray& frame) {
    if (frame.size() < TRANSPORT_OVERHEAD) {
        throw std::runtime_error("Transport frame too short");
    }

    auto read16 = [&](int offset) -> uint16_t {
        return (static_cast<uint8_t>(frame[offset]) << 8) |
               static_cast<uint8_t>(frame[offset + 1]);
    };
    auto read32 = [&](int offset) -> uint32_t {
        return (static_cast<uint32_t>(static_cast<uint8_t>(frame[offset])) << 24) |
               (static_cast<uint32_t>(static_cast<uint8_t>(frame[offset + 1])) << 16) |
               (static_cast<uint32_t>(static_cast<uint8_t>(frame[offset + 2])) << 8) |
               static_cast<uint32_t>(static_cast<uint8_t>(frame[offset + 3]));
    };

    uint16_t header = read16(0);
    if (header != TRANSPORT_HEADER) {
        throw std::runtime_error("Transport frame header error");
    }

    uint16_t tail = read16(frame.size() - 2);
    if (tail != TRANSPORT_TAIL) {
        throw std::runtime_error("Transport frame tail error");
    }

    // CRC check
    uint16_t crc_received = read16(frame.size() - 4);
    uint16_t crc_computed = crc16_modbus(frame.mid(2, frame.size() - 6));
    if (crc_received != crc_computed) {
        throw std::runtime_error("Transport frame CRC error");
    }

    TransportFrame tf;
    tf.packet_id = read16(2);
    tf.total_packets = read16(4);
    tf.current_packet = read16(6);
    tf.retransmit = static_cast<int>(read32(8));
    tf.data_len = read16(16);
    tf.data = frame.mid(18, tf.data_len);
    return tf;
}

QVector<QByteArray> split_into_transport_frames(const QByteArray& data,
                                                  int max_data_per_packet,
                                                  int packet_id,
                                                  int retransmit) {
    QVector<QByteArray> frames;
    int data_len = data.size();

    if (packet_id < 0) packet_id = next_packet_id();

    if (data_len <= max_data_per_packet) {
        frames.append(pack_transport_frame(data, packet_id, 1, 1, retransmit));
        return frames;
    }

    int total = (data_len + max_data_per_packet - 1) / max_data_per_packet;
    for (int i = 0; i < total; i++) {
        int start = i * max_data_per_packet;
        int end = qMin(start + max_data_per_packet, data_len);
        QByteArray chunk = data.mid(start, end - start);
        frames.append(pack_transport_frame(chunk, packet_id, total, i + 1, retransmit));
    }
    return frames;
}

// TransportAssembler implementation
TransportAssembler::TransportAssembler(double timeout)
    : m_timeout(timeout) {}

TransportAssembler::FeedResult TransportAssembler::feed(const QByteArray& raw) {
    FeedResult result;

    TransportFrame info;
    try {
        info = unpack_transport_frame(raw);
    } catch (...) {
        return result;
    }

    if (info.retransmit == RETRANSMIT_REQUEST) {
        return result;
    }

    int pid = info.packet_id;
    int total = info.total_packets;
    int current = info.current_packet;

    if (total == 1) {
        result.assembled_data = info.data;
        return result;
    }

    double now = steady_now();

    if (current == 1) {
        StreamState state;
        state.total_packets = total;
        state.chunks[1] = info.data;
        state.last_time = now;
        m_streams[pid] = state;
        return result;
    }

    auto it = m_streams.find(pid);
    if (it == m_streams.end()) return result;

    if (total != it->total_packets) {
        m_streams.erase(it);
        return result;
    }

    if (now - it->last_time > m_timeout) {
        m_streams.erase(it);
        return result;
    }

    it->chunks[current] = info.data;
    it->last_time = now;

    if (it->chunks.size() >= static_cast<unsigned>(it->total_packets)) {
        QByteArray assembled;
        for (int i = 1; i <= it->total_packets; i++) {
            auto cit = it->chunks.find(i);
            if (cit == it->chunks.end()) {
                m_streams.erase(it);
                return result;
            }
            assembled.append(cit.value());
        }
        m_streams.erase(it);
        result.assembled_data = assembled;
    }

    return result;
}

TransportAssembler::RetransmitResult TransportAssembler::check_retransmit() {
    RetransmitResult result;
    double now = steady_now();

    for (auto it = m_streams.begin(); it != m_streams.end(); ) {
        if (now - it->last_time > m_timeout) {
            it = m_streams.erase(it);
            continue;
        }

        if (now - it->last_time < RETRANSMIT_TIMEOUT_MS) {
            ++it;
            continue;
        }

        if (it->retransmit_count >= MAX_RETRANSMIT_REQUESTS) {
            int missing_count = it->total_packets - it->chunks.size();
            if (missing_count > FORCE_DROP_MISSING) {
                it = m_streams.erase(it);
                continue;
            }
            ++it;
            continue;
        }

        if (now - it->retransmit_last_time < RETRANSMIT_TIMEOUT_MS) {
            ++it;
            continue;
        }

        QVector<int> missing;
        for (int i = 1; i <= it->total_packets; i++) {
            if (!it->chunks.contains(i))
                missing.append(i);
        }
        if (missing.isEmpty()) { ++it; continue; }

        it->retransmit_count++;
        it->retransmit_last_time = now;

        result.retransmit_frame = pack_retransmit_request(it.key(), missing);
        result.frame_id = it.key();
        result.missing_packets = missing;
        return result;
    }

    return result;
}

QVector<int> TransportAssembler::feed_retransmit(const QByteArray& raw) {
    try {
        TransportFrame info = unpack_transport_frame(raw);
        if (info.retransmit != RETRANSMIT_REQUEST) return {};

        int n = info.data.size() / 2;
        QVector<int> missing;
        for (int i = 0; i < n; i++) {
            missing.append(
                (static_cast<uint8_t>(info.data[i * 2]) << 8) |
                static_cast<uint8_t>(info.data[i * 2 + 1]));
        }
        return missing;
    } catch (...) {
        return {};
    }
}

void TransportAssembler::cleanup() {
    double now = steady_now();
    for (auto it = m_streams.begin(); it != m_streams.end(); ) {
        if (now - it->last_time > m_timeout)
            it = m_streams.erase(it);
        else
            ++it;
    }
}

void TransportAssembler::reset() {
    m_streams.clear();
}
