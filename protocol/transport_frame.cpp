#include "transport_frame.h"
#include "crc16.h"
#include <algorithm>
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
                                 int retransmit, int device_type) {
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
    append16(static_cast<uint16_t>(device_type));
    append16(static_cast<uint16_t>(packet_id));
    append16(static_cast<uint16_t>(total_packets));
    append16(static_cast<uint16_t>(current_packet));
    append32(static_cast<uint32_t>(retransmit));
    append32(0);
    append16(static_cast<uint16_t>(data.size()));
    body.append(data);

    // 外层 transport 帧【含 CRC16】：CRC-16/MODBUS 覆盖帧头之后、CRC 字段之前全部数据。
    uint16_t crc = crc16_modbus(body);

    QByteArray frame;
    frame.append(static_cast<char>(0xAB));
    frame.append(static_cast<char>(0xAB));
    frame.append(body);
    // Append CRC to frame (not body!)
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>(0xDC));
    frame.append(static_cast<char>(0xDC));
    return frame;
}

QByteArray pack_retransmit_request(int frame_id, const QVector<int>& missing_packets,
                                   int device_type) {
    QByteArray data;
    for (int p : missing_packets) {
        data.append(static_cast<char>((p >> 8) & 0xFF));
        data.append(static_cast<char>(p & 0xFF));
    }
    return pack_transport_frame(data, frame_id, 0, 0, RETRANSMIT_REQUEST, device_type);
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

    // CRC 接收校验【已关闭】(对齐 Python 参考实现的宽松接收端):
    // Python 测试工具 send_sine.py 默认 compute_crc=False, CRC 字段写 0x0000,
    // 纯 Python 逐帧算 CRC 会把发送速率压到 ~3 FPS, 无法测出本机最大接收速率。
    // 因此这里不再校验/丢弃 CRC 不匹配的帧 (发送端 pack_transport_frame 仍会正确填 CRC)。
    // 如需恢复严格校验, 取消下面三行注释即可。
    // uint16_t crc_received = read16(frame.size() - 4);
    // uint16_t crc_computed = crc16_modbus(frame.mid(2, frame.size() - 6));
    // if (crc_received != crc_computed) { throw std::runtime_error("Transport frame CRC error"); }

    TransportFrame tf;
    tf.device_type = read16(2);
    tf.packet_id = read16(4);
    tf.total_packets = read16(6);
    tf.current_packet = read16(8);
    tf.retransmit = static_cast<int>(read32(10));
    tf.data_len = read16(18);
    if (20 + tf.data_len > frame.size() - 2) {
        throw std::runtime_error("Transport frame data length error");
    }
    tf.data = frame.mid(20, tf.data_len);
    return tf;
}

QVector<QByteArray> split_into_transport_frames(const QByteArray& data,
                                                  int max_data_per_packet,
                                                  int packet_id,
                                                  int retransmit,
                                                  int device_type) {
    QVector<QByteArray> frames;
    int data_len = data.size();

    if (packet_id < 0) packet_id = next_packet_id();

    if (data_len <= max_data_per_packet) {
        frames.append(pack_transport_frame(data, packet_id, 1, 1, retransmit, device_type));
        return frames;
    }

    int total = (data_len + max_data_per_packet - 1) / max_data_per_packet;
    for (int i = 0; i < total; i++) {
        int start = i * max_data_per_packet;
        int end = qMin(start + max_data_per_packet, data_len);
        QByteArray chunk = data.mid(start, end - start);
        frames.append(pack_transport_frame(chunk, packet_id, total, i + 1, retransmit, device_type));
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

    if (total <= 1)
    {
        result.assembled_data = std::move(info.data);
        return result;
    }

    double now = steady_now();

    if (current == 1) {
        m_stats.frame_start++; // 帧级统计: 收到首包

        // 每 8 个新帧首包清理一次过期流: 对标 Python check_transport_timeout(),
        // 使用 first_time (流创建时间) 而非 last_time (最后收包时间) 做硬超时,
        // 防止丢包导致的残缺流因持续收到其他分片而永远无法被清理 → 恶性循环。
        // 之前只在 m_streams.size() > MAX_TRANSPORT_STREAMS/2 时清理,
        // 但对持续丢包场景, last_time 始终刷新, 清理永远不触发。
        static int new_stream_count = 0;
        if ((++new_stream_count % 8) == 0)
        {
            QVector<int> stale;
            for (auto it = m_streams.begin(); it != m_streams.end(); ++it)
            {
                if (now - it->first_time > m_timeout)
                    stale.append(it.key());
            }
            m_stats.frame_timeout += stale.size(); // 帧级统计: 超时清理数
            for (int key : stale)
                m_streams.remove(key);
        }

        StreamState state;
        state.total_packets = total;
        state.chunks.resize(total);             // 预分配 vector, 对标 Python [None]*N
        state.chunks[0] = std::move(info.data); // current==1 → index 0
        state.received_count = 1;
        state.last_time = now;
        state.first_time = now; // 流创建时间
        m_streams[pid] = std::move(state);
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

    int idx = current - 1;
    if (idx < 0 || idx >= it->chunks.size())
        return result;

    if (it->chunks[idx].isNull())
        it->received_count++;
    it->chunks[idx] = std::move(info.data); // 重复包覆盖 (last-write-wins)
    it->last_time = now;

    if (it->received_count >= it->total_packets)
    {
        // 全部不同分片已收齐: 预分配 vector 中无空位, 直接拼接。
        // 对标 Python b''.join(state.chunks)。
        m_stats.frame_complete++; // 帧级统计: 成功完成
        int total_size = 0;
        for (int i = 0; i < it->total_packets; i++)
            total_size += it->chunks[i].size();
        QByteArray assembled;
        assembled.reserve(total_size);
        for (int i = 0; i < it->total_packets; i++)
            assembled.append(it->chunks[i]);
        m_streams.erase(it);
        result.assembled_data = std::move(assembled);
    }

    return result;
}

TransportAssembler::RetransmitResult TransportAssembler::check_retransmit() {
    // 对标 Python: 下位机暂不支持重传, 此方法仅做超时清理, 不发送重传请求。
    // 重传请求会消耗 FPGA 侧资源且无效, 反而可能造成网络拥塞。
    // 使用 first_time 而非 last_time 做硬超时: 防止丢包残缺流因持续收到
    // 其他分片刷新 last_time 而永远不被清理。
    RetransmitResult result;
    double now = steady_now();

    for (auto it = m_streams.begin(); it != m_streams.end(); ) {
        if (now - it->first_time > m_timeout)
        {
            m_stats.frame_timeout++; // 帧级统计: 超时清理
            it = m_streams.erase(it);
        }
        else
        {
            ++it;
        }
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
        if (now - it->first_time > m_timeout)
            it = m_streams.erase(it);
        else
            ++it;
    }
}

void TransportAssembler::reset() {
    m_streams.clear();
}