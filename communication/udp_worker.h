#ifndef UDP_WORKER_H
#define UDP_WORKER_H

#include <QThread>
#include <QUdpSocket>
#include <QMutex>
#include <QQueue>
#include "protocol/transport_frame.h"

class UDPWorker : public QThread {
    Q_OBJECT
public:
    explicit UDPWorker(const QString& host = "0.0.0.0", int port = 5000,
                       int recv_buffer = 4 * 1024 * 1024, QObject* parent = nullptr);
    ~UDPWorker() override;

    bool is_ready() const;
    bool send_cmd(const QString& ip, int port, const QByteArray& data);
    void stop();

    int port() const { return m_port; }

signals:
    void sig_cmd_received(const QByteArray& data);
    void sig_retransmit_request(const QByteArray& data);
    void sig_error(const QString& msg);
    void sig_ready();
    void sig_stopped();

protected:
    void run() override;

private:
    struct PendingDatagram {
        QString ip;
        int port;
        QByteArray data;
    };

    void flush_pending_sends();

    QString m_host;
    int m_port;
    int m_recv_buffer;
    volatile bool m_running = false;
    QUdpSocket* m_sock = nullptr;
    TransportAssembler m_transport_asm;
    QMutex m_send_mutex;
    QQueue<PendingDatagram> m_send_queue;
    qint64 m_last_send_err_ms = -2000;  // 发送失败节流上报 (ms 时间戳)
};

#endif // UDP_WORKER_H
