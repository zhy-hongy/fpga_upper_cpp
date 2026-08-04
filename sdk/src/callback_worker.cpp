#include "callback_worker.h"

#include <QMutexLocker>

CallbackWorker::CallbackWorker(DataDispatcher* dispatcher, QObject* parent)
    : QThread(parent), m_dispatcher(dispatcher) {}

CallbackWorker::~CallbackWorker() {
    // 注意：不在此处 wait()——由 CameraDevice::stop_data_path() 先 stop+wait，
    // 否则 QThread 析构会因线程仍在运行而 abort。
    clear_handler();
}

void CallbackWorker::set_handler(DataHandler handler, void* userData) {
    QMutexLocker lock(&m_handlerMutex);
    m_handler = handler;
    m_userData = userData;
}

void CallbackWorker::clear_handler() {
    QMutexLocker lock(&m_handlerMutex);
    m_handler = nullptr;
    m_userData = nullptr;
}

bool CallbackWorker::has_handler() const {
    QMutexLocker lock(&m_handlerMutex);
    return m_handler != nullptr;
}

void CallbackWorker::request_stop() {
    m_stop.store(true);
}

void CallbackWorker::run() {
    while (!m_stop.load()) {
        FramePacket frame;
        // 有界等待（200ms）：保证能周期性检查 m_stop，停止延迟可控。
        if (m_dispatcher->wait_pop(&frame, 200)) {
            DataHandler h = nullptr;
            void* ud = nullptr;
            {
                QMutexLocker lock(&m_handlerMutex);
                h = m_handler;
                ud = m_userData;
            }
            if (h) {
                FrameInfo info;
                frame_to_info(frame, info);
                h(&info, frame.data.constData(), ud);   // DataHandler 只在本线程执行
            }
        }
        // wait_pop 超时/空返回 false → 回到循环顶部检查 m_stop
    }
}
