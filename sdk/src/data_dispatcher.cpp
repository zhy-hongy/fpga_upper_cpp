#include "data_dispatcher.h"

#include <cstring>

#include <QMutexLocker>

void DataDispatcher::enqueue(const FramePacket& frame) {
    QMutexLocker lock(&m_mutex);
    m_queue.append(frame);
    m_latest.insert(frame.sensorId, frame);   // 只更新最新帧缓存，不改写来源字段
    m_cv.wakeAll();
}

bool DataDispatcher::wait_pop(FramePacket* out, int timeoutMs) {
    QMutexLocker lock(&m_mutex);
    if (!m_queue.isEmpty()) {
        if (out) *out = m_queue.takeFirst();
        return true;
    }
    if (timeoutMs > 0)
        m_cv.wait(&m_mutex, static_cast<unsigned long>(timeoutMs));
    if (!m_queue.isEmpty()) {
        if (out) *out = m_queue.takeFirst();
        return true;
    }
    return false;
}

bool DataDispatcher::wait_latest(uint16_t sensorId, FramePacket* out, int timeoutMs) {
    QMutexLocker lock(&m_mutex);
    auto it = m_latest.constFind(sensorId);
    if (it != m_latest.constEnd()) {
        if (out) *out = it.value();
        return true;
    }

    QDeadlineTimer deadline(timeoutMs);
    while (it == m_latest.constEnd()) {
        if (deadline.hasExpired()) return false;
        qint64 rem = deadline.remainingTime();
        if (rem < 0)
            m_cv.wait(&m_mutex);                       // 无期限等待（timeoutMs<0）
        else
            m_cv.wait(&m_mutex, static_cast<unsigned long>(rem));
        it = m_latest.constFind(sensorId);             // 唤醒后重新检查（防伪唤醒）
    }
    if (out) *out = it.value();
    return true;
}

bool DataDispatcher::pop(FramePacket* out) {
    QMutexLocker lock(&m_mutex);
    if (m_queue.isEmpty()) return false;
    if (out) *out = m_queue.takeFirst();
    return true;
}

int DataDispatcher::pending_count() const {
    QMutexLocker lock(&m_mutex);
    return m_queue.size();
}
