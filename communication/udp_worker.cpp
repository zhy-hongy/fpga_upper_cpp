#include "udp_worker.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QDateTime>

// Global pointer for QUdpSocket in thread
UDPWorker::UDPWorker(const QString& host, int port, int recv_buffer, QObject* parent)
    : QThread(parent), m_host(host), m_port(port), m_recv_buffer(recv_buffer) {}

UDPWorker::~UDPWorker() {
    stop();
}

void UDPWorker::run() {
    m_running = true;
    m_sock = new QUdpSocket();

    if (!m_sock->bind(QHostAddress(m_host), m_port)) {
        emit sig_error("UDP bind failed: " + m_sock->errorString());
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
        flush_pending_sends();
        if (m_sock->waitForReadyRead(100)) {
            while (m_sock->hasPendingDatagrams()) {
                QByteArray data;
                data.resize(static_cast<int>(m_sock->pendingDatagramSize()));
                QHostAddress sender;
                quint16 sender_port;
                m_sock->readDatagram(data.data(), data.size(), &sender, &sender_port);
                recv_count++;

                if (recv_count <= 2 || last_hex_log.elapsed() > 5000) {
                    emit sig_error(QString("DBG: ctrl recv #%1 len=%2 from=%3:%4 head=%5")
                        .arg(recv_count).arg(data.size()).arg(sender.toString()).arg(sender_port)
                        .arg(data.size() >= 2 ? QString(data.left(2).toHex()) : "??"));
                    last_hex_log.restart();
                }

                if (data.size() >= 2 &&
                    static_cast<uint8_t>(data[0]) == 0xAB &&
                    static_cast<uint8_t>(data[1]) == 0xAB) {
                    if (data.size() >= 12) {
                        uint32_t retransmit = (static_cast<uint8_t>(data[8]) << 24) |
                                              (static_cast<uint8_t>(data[9]) << 16) |
                                              (static_cast<uint8_t>(data[10]) << 8) |
                                              static_cast<uint8_t>(data[11]);
                        if (static_cast<int>(retransmit) == RETRANSMIT_REQUEST) {
                            emit sig_retransmit_request(data);
                            continue;
                        }
                    }
                    auto result = m_transport_asm.feed(data);
                    if (!result.assembled_data.isNull()) {
                        emit sig_cmd_received(result.assembled_data);
                    }
                }
            }
        }
    }

    if (m_sock) {
        m_sock->close();
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    emit sig_stopped();
}

bool UDPWorker::is_ready() const {
    return m_sock != nullptr && m_running;
}

// 线程安全发送：socket 由 run()（UDPWorker 线程）创建并拥有，
// writeDatagram 不能在调用线程（主线程）直接执行——会触发 Qt 跨线程
// sendEvent 断言。这里只入队，真正写 socket 由 run() 循环内 flush_pending_sends()
// 在 UDPWorker 线程完成。
bool UDPWorker::send_cmd(const QString& ip, int port, const QByteArray& data) {
    if (!is_ready()) {
        emit sig_error("Send failed: socket not ready");
        return false;
    }
    QMutexLocker locker(&m_send_mutex);
    m_send_queue.enqueue({ip, port, data});
    return true;
}

void UDPWorker::flush_pending_sends() {
    QQueue<PendingDatagram> to_send;
    {
        QMutexLocker locker(&m_send_mutex);
        if (m_send_queue.isEmpty()) return;
        to_send.swap(m_send_queue);
    }
    while (!to_send.isEmpty()) {
        PendingDatagram d = to_send.dequeue();
        if (m_sock)
            m_sock->writeDatagram(d.data, QHostAddress(d.ip), d.port);
    }
}

void UDPWorker::stop() {
    m_running = false;
    // 不在调用线程 close()：socket 由 UDPWorker 线程拥有，跨线程 close 同样触发
    // Qt 断言。run() 循环退出时会在本线程内关闭并清理 socket。
    wait(3000);
}
