#ifndef TRANSPORT_FRAME_H
#define TRANSPORT_FRAME_H

#include <QByteArray>
#include <QMap>
#include <QVector>
#include <optional>
#include <ctime>

constexpr uint16_t TRANSPORT_HEADER = 0xABAB;
constexpr uint16_t TRANSPORT_TAIL = 0xDCDC;
constexpr uint8_t TRANSPORT_HEADER_BYTES[] = {0xAB, 0xAB};
constexpr uint8_t TRANSPORT_TAIL_BYTES[] = {0xDC, 0xDC};

constexpr int RETRANSMIT_NORMAL = 0;
constexpr int RETRANSMIT_REQUEST = 1;

constexpr int TRANSPORT_OVERHEAD = 22;  // 2+2+2+2+4+4+2+2+2
constexpr int MAX_UDP_PAYLOAD = 1472;
constexpr int MAX_TRANSPORT_DATA = MAX_UDP_PAYLOAD - TRANSPORT_OVERHEAD; // 1450

constexpr double ASSEMBLY_TIMEOUT = 3.0;
constexpr double RETRANSMIT_TIMEOUT_MS = 1.0;
constexpr int MAX_RETRANSMIT_REQUESTS = 2;
constexpr int FORCE_DROP_MISSING = 50;

struct TransportFrame {
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
                                 int retransmit = RETRANSMIT_NORMAL);

QByteArray pack_retransmit_request(int frame_id, const QVector<int>& missing_packets);

TransportFrame unpack_transport_frame(const QByteArray& frame);

QVector<QByteArray> split_into_transport_frames(const QByteArray& data,
                                                  int max_data_per_packet = MAX_TRANSPORT_DATA,
                                                  int packet_id = -1,
                                                  int retransmit = RETRANSMIT_NORMAL);

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

    // Parse a retransmit request
    QVector<int> feed_retransmit(const QByteArray& raw);

    void cleanup();
    void reset();

private:
    struct StreamState {
        int total_packets = 0;
        QMap<int, QByteArray> chunks;
        double last_time = 0.0;
        int retransmit_count = 0;
        double retransmit_last_time = 0.0;
    };

    double m_timeout;
    QMap<int, StreamState> m_streams;
};

#endif // TRANSPORT_FRAME_H
