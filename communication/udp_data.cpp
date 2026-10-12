#include "udp_data.h"
#include <QElapsedTimer>
#include <QDebug>
#include <QDateTime>
#include <opencv2/imgproc.hpp>

// 判断组好的 AA55 帧是否为图像帧 (Cmd=0x0301)。
static bool is_image_frame(const QByteArray& raw) {
    if (raw.size() < 6) return false;
    int cmd = (static_cast<uint8_t>(raw[4]) << 8) | static_cast<uint8_t>(raw[5]);
    return cmd == CMD_DATA_IMAGE_2D;
}

UDPDataThread::UDPDataThread(const QString& host, int port, int recv_buffer, int max_queue, QObject* parent)
    : QThread(parent), m_host(host), m_port(port), m_recv_buffer(recv_buffer) {
    for (int sid = 1; sid <= 4; sid++)
        m_frame_queues[sid] = std::deque<cv::Mat>();
    m_shared_queue = std::deque<cv::Mat>(max_queue);
    m_fps_timer.start();
    m_last_log_timer.start();
}

UDPDataThread::~UDPDataThread() {
    stop();
}

void UDPDataThread::run() {
    m_running = true;
    m_fps_timer.start();
    m_sock = new QUdpSocket();

    // ReuseAddressHint (Windows 上等价 SO_REUSEADDR, 见 Qt6 qabstractsocket.cpp Q_OS_WIN 分支):
    // 同端口模式下数据 socket 必须【先 bind】且持有 SO_REUSEADDR。对齐 Python 参考实现
    // (udp_data.py 先 setsockopt(SO_REUSEADDR,1) 再 bind): 两个 socket 都持有 SO_REUSEADDR
    // 才能同时 bind 同一端口。Windows 对 SO_REUSEADDR 同名端口把 UDP 单播投递给【先 bind】
    // 的 socket, 因此本 socket 负责接收全部单播 (图像 + 控制应答)。
    // 注意: ShareAddress 在 Windows 不设 SO_REUSEADDR, 不能用于这里。
    if (!m_sock->bind(QHostAddress(m_host), m_port, QAbstractSocket::ReuseAddressHint)) {
        emit sig_error("Data channel bind failed: " + m_sock->errorString());
        m_sock->deleteLater();
        m_sock = nullptr;
        m_running = false;
        return;
    }
    m_sock->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, m_recv_buffer);

    emit sig_ready();

    // 诊断: 查询系统实际分配的接收缓冲区大小 (必须在 sig_ready 之后,
    // 否则 protocol_handler 会因 m_data_ready==false 将其误判为 connect_failed)。
    {
        QVariant actual = m_sock->socketOption(QAbstractSocket::ReceiveBufferSizeSocketOption);
        int actual_bytes = actual.toInt();
        emit sig_error(QString("DBG: UDP recv_buf requested=%1 MB actual=%2 KB (%3 MB)")
                           .arg(m_recv_buffer / (1024.0 * 1024.0), 0, 'f', 1)
                           .arg(actual_bytes / 1024.0, 0, 'f', 1)
                           .arg(actual_bytes / (1024.0 * 1024.0), 0, 'f', 2));
    }

    // 启动解码工作线程: 接收线程只收包+组包 (保证 socket 始终被排空、不丢包),
    // 图像解码在独立线程执行, 不再阻塞接收循环。
    {
        std::lock_guard<std::mutex> lk(m_decode_mutex);
        m_decode_stop = false;
    }
    m_decode_running.store(true, std::memory_order_release);
    m_decode_thread = std::thread(&UDPDataThread::decode_loop, this);

    int recv_count = 0;
    QElapsedTimer last_hex_log;
    last_hex_log.start();

    // 复用接收缓冲: 避免每包重新 malloc 1472B (25FPS 满帧 = 1628 包/帧)。
    // feed()/信号转发都会把需要的数据拷走, 不会保留对 data 的引用, 因此可安全复用。
    QByteArray data;
    data.reserve(1500);

    QElapsedTimer cleanup_timer;
    cleanup_timer.start();

    while (m_running) {
        if (m_sock->waitForReadyRead(10))
        {
            while (m_sock->hasPendingDatagrams()) {
                data.resize(static_cast<int>(m_sock->pendingDatagramSize()));
                QHostAddress sender;
                quint16 sender_port;
                m_sock->readDatagram(data.data(), data.size(), &sender, &sender_port);
                recv_count++;

                if (m_fpga_addr.first.isNull()) {
                    m_fpga_addr = qMakePair(sender, sender_port);
                }

                // 节流诊断: 前 3 包必打, 之后每 5s 最多一条 (避免每 1024 包查时钟)。
                if (recv_count <= 3 || (recv_count % 1024 == 0 && last_hex_log.elapsed() > 5000)) {
                    auto st = m_transport_asm.stats();
                    emit sig_error(QString("DBG: data recv #%1 len=%2 from=%3 head=%4 streams=%5 [start=%6 complete=%7 timeout=%8]")
                                       .arg(recv_count)
                                       .arg(data.size())
                                       .arg(sender.toString())
                                       .arg(data.size() >= 2 ? QString(data.left(2).toHex()) : "??")
                                       .arg(m_transport_asm.stream_count())
                                       .arg(st.frame_start)
                                       .arg(st.frame_complete)
                                       .arg(st.frame_timeout));
                    last_hex_log.restart();
                }

                if (data.size() >= 2 &&
                    static_cast<uint8_t>(data[0]) == TRANSPORT_HEADER_BYTES[0] &&
                    static_cast<uint8_t>(data[1]) == TRANSPORT_HEADER_BYTES[1]) {
                    auto result = m_transport_asm.feed(data);
                    if (!result.assembled_data.isNull()) {
                        if (is_image_frame(result.assembled_data))
                            feed_frame(result.assembled_data);   // 图像 → 解码队列
                        else
                            emit sig_cmd_received(result.assembled_data);
                    }
                }
            }
        }

        // 对标 Python _check_transport_timeout(): 每 100ms 清理一次超时残帧,
        // 无论有无数据都执行。之前只在 waitForReadyRead 超时 (else) 分支调用,
        // 持续收包期间 check_retransmit 永远不触发, 导致流积压。
        if (cleanup_timer.elapsed() > 100)
        {
            m_transport_asm.check_retransmit();
            cleanup_timer.restart();
        }
    }

    // 停止解码工作线程: 排空已入队帧并 join。
    {
        std::lock_guard<std::mutex> lk(m_decode_mutex);
        m_decode_stop = true;
    }
    m_decode_cv.notify_all();
    if (m_decode_thread.joinable()) m_decode_thread.join();
    m_decode_running.store(false, std::memory_order_release);

    if (m_sock) {
        m_sock->close();
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    emit sig_stopped();
}

void UDPDataThread::feed_frame(const QByteArray& raw) {
    if (!m_decode_running.load(std::memory_order_acquire)) {
        // 解码线程未启动 (run() 未执行或绑定失败) → 直接处理
        handle_frame(raw);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(m_decode_mutex);
        if (m_decode_queue.size() >= MAX_DECODE_QUEUE)
            m_decode_queue.pop_front();   // 丢弃最旧帧, 保留最新
        m_decode_queue.push_back(raw);

        // 诊断: 每 30 帧打印一次解码队列深度, 观察是否持续积压
        static int push_counter = 0;
        if ((++push_counter % 30) == 0)
        {
            emit sig_error(QString("DBG: decode_queue push depth=%1 (push#%2)")
                               .arg(m_decode_queue.size())
                               .arg(push_counter));
        }
    }
    m_decode_cv.notify_one();
}

void UDPDataThread::decode_loop() {
    static int pop_counter = 0;
    while (true) {
        QByteArray raw;
        {
            std::unique_lock<std::mutex> lk(m_decode_mutex);
            m_decode_cv.wait(lk, [this]() {
                return m_decode_stop || !m_decode_queue.empty();
            });
            if (m_decode_stop && m_decode_queue.empty())
                break;
            raw = std::move(m_decode_queue.front());
            m_decode_queue.pop_front();

            // 诊断: 每 30 帧打印一次解码队列剩余深度, 观察消费速度
            if ((++pop_counter % 30) == 0)
            {
                emit sig_error(QString("DBG: decode_queue pop  depth=%1 (pop#%2)")
                                   .arg(m_decode_queue.size())
                                   .arg(pop_counter));
            }
        }
        handle_frame(raw);
    }
}

void UDPDataThread::log_throttled(const QString& msg) {
    // 节流：每 2s 最多输出一条同类型错误，避免异常帧刷屏
    if (m_last_log_timer.elapsed() >= 2000) {
        m_last_log_timer.restart();
        emit sig_error(msg);
    }
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

    if (raw.size() < 18) return;
    int dev = read16(2);
    int cmd = read16(4);
    int data_len = static_cast<int>(read32(10));
    if (raw.size() < 18 + data_len) return;

    // 非图像命令 → 转发应答路由。
    // 同端口模式 (control_port == data_port): 控制应答由数据 socket 收到,
    // 经 sig_cmd_received → ProtocolHandler::on_raw_response 按 msg_id 路由。
    // 正常模式: 数据线程不连接该信号, 此转发不会产生副作用。
    if (cmd != CMD_DATA_IMAGE_2D) {
        emit sig_cmd_received(raw);
        return;
    }

    // ── Cmd = 0x0301 图像帧 ──
    const int full = FPGA_FULL_IMAGE_SIZE;
    QByteArray payload;
    if (data_len >= full + 2) {
        // 新协议: [2B 结果码][图像数据]
        int result_code = read16(14);
        if (result_code != 0x0000) {
            log_throttled(QString("⚠ 图像帧结果码异常: Dev=%1 结果码=0x%2 (期望 0x0000)")
                .arg(dev, 0, 16).arg(result_code, 4, 16, QChar('0')));
            return;
        }
        payload = raw.mid(16, full);
    } else if (data_len >= full) {
        // 兼容旧全帧: 无结果码
        payload = raw.mid(14, full);
    } else if (data_len == 2) {
        // 图像帧无数据 (仅结果码)
        int result_code = read16(14);
        log_throttled(QString("图像帧无数据: Dev=%1 结果码=0x%2")
            .arg(dev, 0, 16).arg(result_code, 4, 16, QChar('0')));
        emit sig_cmd_received(raw);
        return;
    } else if (data_len >= 1280) {
        // 0x0301 但尺寸不匹配 (非全帧) → 诊断后丢弃, 不转发 (防递归)
        log_throttled(QString("⚠ 图像帧尺寸不匹配: Dev=%1 Len=%2, 期望 %3 或 %4")
            .arg(dev, 0, 16).arg(data_len).arg(full).arg(full + 2));
        return;
    } else {
        // 小帧且非全帧图像 → 按普通命令帧处理
        emit sig_cmd_received(raw);
        return;
    }

    // 解码 FPGA 交织格式 → 行优先 uint8 图
    cv::Mat img;
    if (!decode_fpga_image(payload, FPGA_WIDTH, FPGA_HEIGHT, img)) {
        log_throttled(QString("FPGA 图像解码失败: Dev=%1 payload=%2B")
            .arg(dev, 0, 16).arg(payload.size()));
        return;
    }

    emit sig_resolution(dev, FPGA_WIDTH, FPGA_HEIGHT);

    // 推入 per-sensor 队列 + shared 队列 (UI poll_frame 轮询消费)。
    // 解码在锁外完成, 这里只对队列写加锁, 避免长时间阻塞 UI 轮询线程。
    uint64_t frame_seq;
    {
        QMutexLocker locker(&m_lock);
        frame_seq = ++m_frame_id_seq;
        m_sensor_seq[dev] = frame_seq;

        auto& queue = m_frame_queues[dev];
        queue.push_back(img);
        if (queue.size() > 4) queue.pop_front();

        m_shared_queue.push_back(img);
        if (m_shared_queue.size() > 4) m_shared_queue.pop_front();
    }

    // [实验] 临时关闭 SDK FramePacket 分发: 验证 DataDispatcher 是否
    // 是帧率逐渐下降的根因。若注释后 27 FPS 稳定不再掉到 9~14 FPS,
    // 则确认问题在 SDK/DataDispatcher 支路, 而非 UDP 接收或解码。
    // 测试完成后需恢复 emit sig_frame_ready(pkt)。
#if 0
    FramePacket pkt;
    pkt.frameId = static_cast<uint32_t>(frame_seq);
    pkt.sensorId = static_cast<uint16_t>(dev);
    pkt.batchId = 0;
    pkt.lineIndex = 0;
    pkt.encoder = 0;
    pkt.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch() * 1000);
    pkt.frameIdSource = META_SOURCE_SDK;
    pkt.encoderSource = META_SOURCE_UNKNOWN;
    pkt.timestampSource = META_SOURCE_SDK;
    pkt.width = FPGA_WIDTH;
    pkt.height = FPGA_HEIGHT;
    pkt.cmd_code = CMD_DATA_IMAGE_2D;
    pkt.data = QByteArray(reinterpret_cast<const char*>(img.data), img.total());
    emit sig_frame_ready(pkt);
#endif

    m_last_received_pkt_id = read16(6);  // AA55 msg_id, 供 UI 显示

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
}

cv::Mat UDPDataThread::get_latest_frame(int sensor_id) {
    QMutexLocker locker(&m_lock);
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

std::deque<cv::Mat> UDPDataThread::get_sensor_queue(int sensor_id) {
    QMutexLocker locker(&m_lock);
    auto it = m_frame_queues.find(sensor_id);
    if (it != m_frame_queues.end()) return it.value();
    return std::deque<cv::Mat>();
}

uint64_t UDPDataThread::get_frame_seq(int sensor_id) const {
    QMutexLocker locker(&m_lock);
    return m_sensor_seq.value(sensor_id, 0);
}

void UDPDataThread::stop() {
    m_running = false;
    // 不在调用线程 close()：socket 由 UDPDataThread 线程拥有，跨线程 close 触发
    // Qt 断言。run() 循环退出时会在本线程内关闭并清理 socket。
    wait(3000);
}