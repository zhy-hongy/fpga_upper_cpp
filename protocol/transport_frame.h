#ifndef TRANSPORT_FRAME_H
#define TRANSPORT_FRAME_H

#include <QByteArray>
#include <QHash>
#include <QVector>
#include <optional>
#include <ctime>

constexpr uint16_t TRANSPORT_HEADER = 0xABAB;
constexpr uint16_t TRANSPORT_TAIL = 0xDCDC;
constexpr uint8_t TRANSPORT_HEADER_BYTES[] = {0xAB, 0xAB};
constexpr uint8_t TRANSPORT_TAIL_BYTES[] = {0xDC, 0xDC};

constexpr int RETRANSMIT_NORMAL = 0;
constexpr int RETRANSMIT_REQUEST = 1;

// 外层 transport 帧【含 CRC16】：帧头(2)+设备类型(2)+唯一帧ID(2)+总包数(2)
// +当前包编号(2)+重传说明(4)+空(4)+数据长度(2)+CRC16(2)+帧尾(2) = 24。
// CRC 为 CRC-16/MODBUS，覆盖帧头之后、CRC 字段之前（含 Data）的全部数据。
// 设备类型: 0=FPGA, 1~4=相机1~4（对齐 Python 通信/protocol/transport_frame.py）
constexpr int TRANSPORT_OVERHEAD = 24;  // 2+2+2+2+2+4+4+2+2+2
constexpr int MAX_UDP_PAYLOAD = 1472;
constexpr int MAX_TRANSPORT_DATA = MAX_UDP_PAYLOAD - TRANSPORT_OVERHEAD; // 1448

constexpr double ASSEMBLY_TIMEOUT = 0.5; // 0.5s 硬超时: 一帧 1628 分片正常 37ms 完成,
                                         // 0.5s 是 13x 余量, 防止丢包残缺流积压导致
                                         // m_streams 膨胀 → hash 查找变慢 → 恶性循环
constexpr double RETRANSMIT_TIMEOUT_MS = 1.0;
constexpr int MAX_RETRANSMIT_REQUESTS = 2;
constexpr int FORCE_DROP_MISSING = 50;
// 组包器最多同时跟踪的未完成流数量。流式上传期间无 idle，check_retransmit 不触发，
// 缺包帧会永久驻留；此上限 + 新帧开始时的超时清理防止 m_streams 无界增长。
constexpr int MAX_TRANSPORT_STREAMS = 64;

struct TransportFrame {
    int device_type = 0;   // 设备类型 (0=FPGA, 1~4=相机1~4)
    int packet_id = 0;
    int total_packets = 0;
    int current_packet = 0;
    int retransmit = 0;
    int data_len = 0;
    QByteArray data;
};

// Pack/unpack transport frames
QByteArray pack_transport_frame(const QByteArray& data, int packet_id = 0,
                                 int total_packets = 1, int current_packet = 1,
                                 int retransmit = RETRANSMIT_NORMAL,
                                 int device_type = 0);

QByteArray pack_retransmit_request(int frame_id, const QVector<int>& missing_packets,
                                   int device_type = 0);

TransportFrame unpack_transport_frame(const QByteArray& frame);

QVector<QByteArray> split_into_transport_frames(const QByteArray& data,
                                                  int max_data_per_packet = MAX_TRANSPORT_DATA,
                                                  int packet_id = -1,
                                                  int retransmit = RETRANSMIT_NORMAL,
                                                  int device_type = 0);

// Transport assembler (reassembly of multi-packet frames)
class TransportAssembler {
public:
    TransportAssembler(double timeout = ASSEMBLY_TIMEOUT);

    // Feed a raw transport frame; returns (retransmit_request, assembled_data)
    struct FeedResult {
        QByteArray retransmit_frame;
        QByteArray assembled_data;
    };
    FeedResult feed(const QByteArray& raw);

    // Check if retransmit is needed
    struct RetransmitResult {
        QByteArray retransmit_frame;
        int frame_id = 0;
        QVector<int> missing_packets;
    };
    RetransmitResult check_retransmit();

    // 诊断: 返回当前活跃 (未完成) 的流数量
    int stream_count() const { return m_streams.size(); }

    // 帧级统计: 用于判断 UDP 丢包率
    struct FrameStats
    {
        uint64_t frame_start = 0;    // 开始接收的帧总数 (收到首包)
        uint64_t frame_complete = 0; // 成功完成的帧数
        uint64_t frame_timeout = 0;  // 超时清理的未完成帧数
    };
    FrameStats stats() const { return m_stats; }

    // Parse a retransmit request
    QVector<int> feed_retransmit(const QByteArray& raw);

    void cleanup();
    void reset();

private:
    struct StreamState {
        int total_packets = 0;
        QVector<QByteArray> chunks; // 预分配 list, 按 (current_packet-1) 索引 O(1)
        int received_count = 0;     // 已收到的不同分片数 (替代 QMap::size())
        double last_time = 0.0;     // 最后收到分片的时间
        double first_time = 0.0;    // 流创建时间, 用于硬超时清理 (防止永远收不齐的流无限积压)
        int retransmit_count = 0;
        double retransmit_last_time = 0.0;
    };

    double m_timeout;
    QHash<int, StreamState> m_streams; // O(1) 平均查找, 对标 Python dict
    FrameStats m_stats;                // 帧级统计 (丢包证据)
};

#endif // TRANSPORT_FRAME_H