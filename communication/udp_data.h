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
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <opencv2/core.hpp>
#include "protocol/transport_frame.h"
#include "protocol/control_frame.h"
#include "protocol/crc16.h"
#include "sdk/src/frame_packet.h"
#include "communication/fpga_image_decoder.h"

class UDPDataThread : public QThread {
    Q_OBJECT
public:
    explicit UDPDataThread(const QString &host = "0.0.0.0", int port = 60000,
                           int recv_buffer = 128 * 1024 * 1024, int max_queue = 4,
                           QObject *parent = nullptr);
    ~UDPDataThread() override;

    void stop();
    int port() const { return m_port; }

    // Queue access
    // 线程安全：与解码线程写入 m_frame_queues / m_shared_queue 共用 m_lock 互斥，
    // 避免 UI 轮询线程与解码线程并发读写 std::deque 造成数据竞争 (UB)。
    cv::Mat get_latest_frame(int sensor_id = -1);
    // 返回拷贝 (而非引用)：调用方在锁外安全持有，不受解码线程后续写影响。
    std::deque<cv::Mat> get_sensor_queue(int sensor_id);
    // 该传感器最近一帧的单调递增序号 (0 = 尚未收到)。供 UI 判断"是否有新帧"，
    // 避免轮询时对同一帧重复渲染。
    uint64_t get_frame_seq(int sensor_id) const;

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
    void handle_frame(const QByteArray &raw);
    // 解码工作线程循环: 从 m_decode_queue 取整帧数据并解码
    void decode_loop();
    // 节流日志：≥2s 一条，避免图像帧异常刷屏
    void log_throttled(const QString& msg);

    QString m_host;
    int m_port;
    int m_recv_buffer;
    volatile bool m_running = false;
    QUdpSocket* m_sock = nullptr;
    mutable QMutex m_lock;

    // ── 解码工作线程 (2026-08 优化) ──
    // 接收线程只负责 收包 + 组包 (TransportAssembler), 保证 socket 始终被排空、
    // 不丢包; 图像解码放在独立线程, 解码耗时不再阻塞接收循环、也不再长时间持有
    // m_lock 阻塞 UI 轮询。队列有界 (MAX_DECODE_QUEUE), 超限丢弃最旧帧保留最新。
    std::thread m_decode_thread;
    std::mutex m_decode_mutex;
    std::condition_variable m_decode_cv;
    std::deque<QByteArray> m_decode_queue;
    bool m_decode_stop = false;
    std::atomic<bool> m_decode_running{false};
    static constexpr int MAX_DECODE_QUEUE = 16; // 对标 Python queue.Queue(maxsize=32), 提升吞吐余量

    // Per-sensor FIFO queues
    QMap<int, std::deque<cv::Mat>> m_frame_queues;
    std::deque<cv::Mat> m_shared_queue;
    QMap<int, uint64_t> m_sensor_seq;   // 每传感器最新帧序号 (单调递增)

    TransportAssembler m_transport_asm;
    QPair<QHostAddress, quint16> m_fpga_addr;

    int m_last_received_pkt_id = -1;
    int m_frame_count = 0;
    int m_dropped_packets = 0;
    int m_fps = 0;
    int m_fps_counter = 0;
    QElapsedTimer m_fps_timer;
    QElapsedTimer m_last_log_timer;      // 节流日志计时
    uint32_t m_frame_id_seq = 0;         // FramePacket::frameId 本地递增序列
};

#endif // UDP_DATA_H