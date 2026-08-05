#include "command_manager.h"

#include <QDateTime>
#include <QMutexLocker>

CommandManager::CommandManager(QObject* parent)
    : QThread(parent) {}

CommandManager::~CommandManager() = default;

uint16_t CommandManager::submit(CommandResultCallback cb, void* userData, int timeoutMs) {
    QMutexLocker lock(&m_mutex);
    const uint16_t id = m_nextId.fetch_add(1);
    Pending p;
    p.cb = cb;
    p.userData = userData;
    p.deadlineMs = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    m_pending.insert(id, p);
    m_cv.wakeAll();   // 唤醒线程重新计算等待时间（可能需更早醒来处理超时）
    return id;
}

void CommandManager::ack(uint16_t commandId) {
    QMutexLocker lock(&m_mutex);
    auto it = m_pending.find(commandId);
    if (it == m_pending.end()) return;   // 已超时 / 已消费：忽略迟到 ACK

    CommandResultCallback cb = it->cb;
    void* ud = it->userData;
    m_pending.erase(it);
    m_tasks.enqueue([cb, commandId, ud]() {
        if (cb) cb(CAM_OK, commandId, ud);
    });
    m_cv.wakeAll();
}

void CommandManager::fail(uint16_t commandId, CamError result) {
    QMutexLocker lock(&m_mutex);
    auto it = m_pending.find(commandId);
    if (it == m_pending.end()) return;

    CommandResultCallback cb = it->cb;
    void* ud = it->userData;
    m_pending.erase(it);
    m_tasks.enqueue([cb, commandId, ud, result]() {
        if (cb) cb(result, commandId, ud);
    });
    m_cv.wakeAll();
}

void CommandManager::request_stop() {
    m_stop.store(true);
    QMutexLocker lock(&m_mutex);
    m_cv.wakeAll();
}

int CommandManager::pending_count() const {
    QMutexLocker lock(&m_mutex);
    return m_pending.size();
}

void CommandManager::run() {
    while (!m_stop.load()) {
        int waitMs = 200;   // 默认轮询周期上限（停止延迟 ≤ 200ms）
        {
            QMutexLocker lock(&m_mutex);
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            qint64 earliest = 0;
            for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
                if (it->deadlineMs <= now) { earliest = now; break; }
                if (earliest == 0 || it->deadlineMs < earliest)
                    earliest = it->deadlineMs;
            }
            if (earliest > 0) {
                const qint64 rem = earliest - now;
                waitMs = static_cast<int>(qBound<qint64>(0LL, rem, 200LL));
            }
            if (m_tasks.isEmpty() && waitMs > 0)
                m_cv.wait(&m_mutex, static_cast<unsigned long>(waitMs));
        }

        // 执行已入队的用户回调任务（ACK 结果 / 主动失败）
        while (!m_stop.load()) {
            std::function<void()> task;
            {
                QMutexLocker lock(&m_mutex);
                if (m_tasks.isEmpty()) break;
                task = m_tasks.dequeue();
            }
            if (task) task();
        }

        check_timeouts();
    }
}

void CommandManager::check_timeouts() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    struct Item { CommandResultCallback cb; void* ud; uint16_t id; };
    QVector<Item> expired;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (it->deadlineMs <= now) {
                expired.append({it->cb, it->userData, it.key()});
                it = m_pending.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const Item& e : expired) {
        if (e.cb) e.cb(CAM_ERR_TIMEOUT, e.id, e.ud);
    }
}
