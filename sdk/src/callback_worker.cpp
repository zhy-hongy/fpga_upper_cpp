#include "callback_worker.h"

#include <QMutexLocker>
#include <QDateTime>

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

DataHandler CallbackWorker::handler() const {
    QMutexLocker lock(&m_handlerMutex);
    return m_handler;
}

void* CallbackWorker::userData() const {
    QMutexLocker lock(&m_handlerMutex);
    return m_userData;
}

void CallbackWorker::submit_async_request(uint16_t sensorId, PixelFormat targetType,
                                          uint32_t timeoutMs, AsyncFrameCallback callback,
                                          void* userData) {
    if (!callback) return;
    QMutexLocker lock(&m_requestsMutex);
    AsyncFrameRequest req;
    req.sensorId = sensorId;
    req.targetType = targetType;
    req.deadlineMs = QDateTime::currentMSecsSinceEpoch() + static_cast<qint64>(timeoutMs);
    req.callback = callback;
    req.userData = userData;
    m_asyncRequests.append(req);
}

void CallbackWorker::request_stop() {
    m_stop.store(true);
}

void CallbackWorker::run() {
    while (!m_stop.load()) {
        FramePacket frame;
        // 有界等待（200ms）：保证能周期性检查 m_stop 和超时，停止延迟可控。
        if (m_dispatcher->wait_pop(&frame, 200)) {
            // 1) 先检查异步请求列表：是否有匹配 sensorId + PixelFormat 的请求
            bool matched = false;
            {
                QMutexLocker lock(&m_requestsMutex);
                for (int i = 0; i < m_asyncRequests.size(); ++i) {
                    const AsyncFrameRequest& req = m_asyncRequests[i];
                    if (req.sensorId == frame.sensorId &&
                        req.targetType == cmd_to_pixel_format(frame.cmd_code)) {
                        // 匹配成功：拷贝数据并回调
                        FrameInfo info;
                        frame_to_info(frame, info);
                        AsyncFrameCallback cb = req.callback;
                        void* ud = req.userData;
                        m_asyncRequests.removeAt(i);
                        // 不持锁执行回调（避免死锁）
                        lock.unlock();
                        if (cb) cb(CAM_OK, &info, frame.data.constData(), ud);
                        lock.relock();
                        matched = true;
                        break;
                    }
                }
            }

            // 2) 如果异步请求未匹配，走正常的 DataHandler 路径
            if (!matched) {
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
        }

        // 3) 检查异步请求超时（每轮循环都检查，最坏延迟 200ms）
        {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            QMutexLocker lock(&m_requestsMutex);
            for (int i = m_asyncRequests.size() - 1; i >= 0; --i) {
                if (m_asyncRequests[i].deadlineMs <= now) {
                    AsyncFrameCallback cb = m_asyncRequests[i].callback;
                    void* ud = m_asyncRequests[i].userData;
                    m_asyncRequests.removeAt(i);
                    lock.unlock();
                    if (cb) cb(CAM_ERR_TIMEOUT, nullptr, nullptr, ud);
                    lock.relock();
                }
            }
        }
    }
}