#ifndef UDP_DATA_H
#define UDP_DATA_H

#include <QThread>
#include <QUdpSocket>
#include <QMutex>
#include <QElapsedTimer>
#include <QMap>
#include <QVector>
#include <QQueue>
#include <deque>
#include <opencv2/core.hpp>
#include "protocol/transport_frame.h"
#include "protocol/crc16.h"
#include "sdk/src/frame_packet.h"

constexpr int DATA_PER_PACKET = 1280;
constexpr int FIRST_PACKET_EXTRA = 8;
constexpr uint8_t FIRST_PKT_HEADER[] = {0xF0, 0x5A, 0xA5, 0x0F};
constexpr int FORCE_RESET_GAP = 50;
constexpr int SENSOR_IDS[] = {1, 2, 3, 4};
constexpr int NUM_SENSORS = 4;

struct SensorAssembly {
    QMap<int, QByteArray> packets;
    int packet_seq = 0;
    int last_pkt_id = -1;
    int image_width = 1280;
    int image_height = 720;
    int expected_packets = 720;
    uint16_t cmd_code = 0;  // FPGA 命令码，用于 frame_to_info 识别数据类型

    void reset() {
        packets.clear();
        packet_seq = 0;
        last_pkt_id = -1;
        expected_packets = image_height;
    }
};

class UDPDataThread : public QThread {
    Q_OBJECT
public:
    explicit UDPDataThread(const QString& host = "0.0.0.0", int port = 60000,
                           int recv_buffer = 10 * 1024 * 1024, int max_queue = 4,
                           QObject* parent = nullptr);
    ~UDPDataThread() override;

    void stop();
    int port() const { return m_port; }

    // Queue access
    cv::Mat get_latest_frame(int sensor_id = -1);
    std::deque<cv::Mat>& get_sensor_queue(int sensor_id);

    int last_pkt_id() const { return m_last_received_pkt_id; }

signals:
    void sig_frame_info(int fps, int total, int dropped);
    void sig_resolution(int sensor_id, int width, int height);
    void sig_frame_ready(const FramePacket& frame);
    void sig_error(const QString& msg);
    void sig_ready();
    void sig_stopped();
    void sig_cmd_received(const QByteArray& data);

public slots:
    void feed_frame(const QByteArray& raw);

protected:
    void run() override;

private:
    void handle_frame(const QByteArray& raw);
    void assemble_and_push(int sensor_id);
    void send_retransmit(const QByteArray& frame);
    void check_retransmit();

    QString m_host;
    int m_port;
    int m_recv_buffer;
    volatile bool m_running = false;
    QUdpSocket* m_sock = nullptr;
    mutable QMutex m_lock;

    // Per-sensor FIFO queues
    QMap<int, std::deque<cv::Mat>> m_frame_queues;
    std::deque<cv::Mat> m_shared_queue;

    TransportAssembler m_transport_asm;
    QPair<QHostAddress, quint16> m_fpga_addr;

    // Per-sensor assembly
    QMap<int, SensorAssembly> m_assemblies;

    int m_last_received_pkt_id = -1;
    int m_frame_count = 0;
    int m_dropped_packets = 0;
    int m_fps = 0;
    int m_fps_counter = 0;
    QElapsedTimer m_fps_timer;
    uint32_t m_frame_id_seq = 0;   // FramePacket::frameId 本地递增序列
};

#endif // UDP_DATA_H
