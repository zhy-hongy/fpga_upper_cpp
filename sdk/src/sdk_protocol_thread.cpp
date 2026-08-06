#include "sdk_protocol_thread.h"

#include <QTimer>

#include "communication/protocol_handler.h"
#include "sdk_test/mock/mock_protocol_handler.h"

SdkProtocolThread::SdkProtocolThread(const Config& config, bool mockMode, QObject* parent)
    : QThread(parent), m_config(config), m_mockMode(mockMode) {}

SdkProtocolThread::~SdkProtocolThread() {
    request_stop();
}

ProtocolHandler* SdkProtocolThread::wait_ready(int timeoutMs) {
    ProtocolHandler* h = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_started) {
            m_startCv.wait(&m_mutex, static_cast<unsigned long>(timeoutMs));
        }
        if (!m_started || !m_handler) return nullptr;
        h = m_handler;
    }
    // 等待 UDPWorker 和 UDPDataThread 子线程的事件循环完全就绪
    // （QUdpSocket 已绑定、事件循环已启动），确保后续命令不会超时
    h->wait_all_ready(timeoutMs);
    return h;
}

void SdkProtocolThread::run() {
    ProtocolHandler* h = nullptr;
    if (m_mockMode) {
        h = new MockProtocolHandler(m_config);
    } else {
        h = new ProtocolHandler(m_config, DEV_SENSOR1);
        // 端口映射（与宿主一致）：controlPort=本地控制绑定，dataPort=本地数据绑定=FPGA 目标端口。
        h->set_network_params(m_config.target_host, m_config.target_port, m_config.control_port);
        h->start_all();
    }

    // 先设置 m_handler（供 request_stop() 安全退出），
    // 但不设置 m_started——必须等 exec() 事件循环就绪后才通知调用方。
    {
        QMutexLocker locker(&m_mutex);
        m_handler = h;
    }

    // 在事件循环启动后立即通知调用方：此时 QueuedConnection 投递和 QTimer
    // 超时都已在事件循环内正常运行，首次命令不会因事件循环未就绪而超时。
    //
    // 关键：使用在 run() 子线程内创建的 QObject 作为 receiver，而非 this
    // （QThread 对象的线程亲和性属于创建它的线程，不是 run() 所在的子线程）。
    QObject* helper = new QObject();  // 无 parent，线程亲和性 = 当前线程（子线程）
    QTimer::singleShot(0, helper, [this, helper]() {
        QMutexLocker locker(&m_mutex);
        m_started = true;
        m_startCv.wakeAll();
        helper->deleteLater();
    });

    exec();   // 事件循环启动 → singleShot(0) 立即触发 → wait_ready() 返回

    if (!m_mockMode) h->stop_all();
    delete h;
    {
        QMutexLocker locker(&m_mutex);
        m_handler = nullptr;
        m_started = false;
    }
}

void SdkProtocolThread::request_stop() {
    ProtocolHandler* h = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        h = m_handler;
    }
    if (!h) {
        // 尚未创建 handler：若线程已启动则直接退出循环并等待（幂等）。
        if (isRunning()) {
            quit();
            wait();
        }
        return;
    }
    quit();
    wait();
}