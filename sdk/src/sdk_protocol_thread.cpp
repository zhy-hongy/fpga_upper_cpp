#include "sdk_protocol_thread.h"

#include "communication/protocol_handler.h"
#include "sdk_test/mock/mock_protocol_handler.h"

SdkProtocolThread::SdkProtocolThread(const Config& config, bool mockMode, QObject* parent)
    : QThread(parent), m_config(config), m_mockMode(mockMode) {}

SdkProtocolThread::~SdkProtocolThread() {
    request_stop();
}

ProtocolHandler* SdkProtocolThread::wait_ready(int timeoutMs) {
    QMutexLocker locker(&m_mutex);
    if (!m_started) {
        m_startCv.wait(&m_mutex, static_cast<unsigned long>(timeoutMs));
    }
    return m_started ? m_handler : nullptr;
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
    {
        QMutexLocker locker(&m_mutex);
        m_handler = h;
        m_started = true;
        m_startCv.wakeAll();
    }

    exec();   // 事件循环：QueuedConnection + QTimer 正常运行

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
