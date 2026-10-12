#include "data_dispatcher.h"

#include <QDebug>

#include <cstring>

#include <QMutexLocker>

static constexpr int MAX_DISPATCHER_QUEUE = 4; // 只保留最新 4 帧, 对标 m_frame_queues

void DataDispatcher::enqueue(const FramePacket& frame) {
    QMutexLocker lock(&m_mutex);

    m_queue.append(frame);
    if (m_queue.size() > MAX_DISPATCHER_QUEUE)
        m_queue.pop_front(); // 丢弃最旧帧, 防止无界积压

    m_latest.insert(frame.sensorId, frame); // 只更新最新帧缓存，不改写来源字段

    // 诊断日志: 每 30 帧打印一次队列深度, 验证是否持续积压
    static int counter = 0;
    if ((++counter % 30) == 0)
    {
        qDebug() << "[DataDispatcher] queue depth =" << m_queue.size()
                 << "counter =" << counter;
    }

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

bool DataDispatcher::wait_latest_type(uint16_t sensorId, PixelFormat targetType,
                                      FramePacket* out, int timeoutMs) {
    QMutexLocker lock(&m_mutex);

    // 辅助 lambda：检查是否存在匹配 sensorId 且 PixelFormat 正确的帧
    auto find_match = [&]() -> QHash<uint16_t, FramePacket>::const_iterator {
        auto it = m_latest.constFind(sensorId);
        if (it != m_latest.constEnd() &&
            cmd_to_pixel_format(it->cmd_code) == targetType) {
            return it;
        }
        return m_latest.constEnd();
    };

    auto it = find_match();
    if (it != m_latest.constEnd()) {
        if (out) *out = it.value();
        return true;
    }

    QDeadlineTimer deadline(timeoutMs);
    while (true) {
        if (deadline.hasExpired()) return false;
        qint64 rem = deadline.remainingTime();
        if (rem < 0)
            m_cv.wait(&m_mutex);                       // 无限等待
        else
            m_cv.wait(&m_mutex, static_cast<unsigned long>(rem));

        it = find_match();                             // 唤醒后重新检查（防伪唤醒 + 类型匹配）
        if (it != m_latest.constEnd()) {
            if (out) *out = it.value();
            return true;
        }
    }
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