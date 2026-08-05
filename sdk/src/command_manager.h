#ifndef COMMAND_MANAGER_H
#define COMMAND_MANAGER_H

#include <atomic>
#include <functional>

#include <QHash>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>

#include "sdk/include/photon_camera.h"

// CommandManager: SDK 异步控制命令管理（全异步非阻塞）。
//
// 职责：
//   - submit()   从任意线程注册一个待 ACK 命令，返回唯一 commandId（线程安全）。
//   - ack()      收到 ACK 时由 ACK 回调线程调用（宿主主线程 / ProtocolHandler 线程），
//                把 CAM_OK 结果投递到本线程执行用户回调（线程安全）。
//   - 超时       由本线程 run() 循环轮询 pending 截止时间，超时投递 CAM_ERR_TIMEOUT。
//
// 线程约束：
//   - 用户回调（CommandResultCallback）始终在本 CommandManager 线程执行，
//     绝不运行在 UDP / UI / 调用线程。
//   - 用户回调内不应阻塞等待本管理器派发的结果（会死锁），也不应在本线程 join 本线程。
class CommandManager : public QThread {
    Q_OBJECT
public:
    explicit CommandManager(QObject* parent = nullptr);
    ~CommandManager() override;

    // 注册一个待 ACK 命令并返回 commandId（线程安全）。cb 不能为空。
    uint16_t submit(CommandResultCallback cb, void* userData, int timeoutMs = 4000);

    // ACK 已到达：投递 CAM_OK 结果（线程安全，可从宿主主线程 / ACK 回调调用）。
    void ack(uint16_t commandId);

    // 主动失败（如 queued 投递失败）：投递指定错误结果（线程安全）。
    void fail(uint16_t commandId, CamError result);

    // 请求停止 run() 循环（配合 wait()，停止延迟 ≤ 200ms）。
    void request_stop();

    // 当前挂起命令数（诊断用，线程安全）。
    int pending_count() const;

protected:
    void run() override;

private:
    struct Pending {
        CommandResultCallback cb = nullptr;
        void* userData = nullptr;
        qint64 deadlineMs = 0;   // 绝对截止时间（epoch ms）
    };
    void check_timeouts();                        // 本线程内执行
    void deliver(uint16_t id, CamError result);   // 本线程内执行

    mutable QMutex m_mutex;
    QWaitCondition m_cv;
    QQueue<std::function<void()>> m_tasks;        // 待投递的用户回调任务
    QHash<uint16_t, Pending> m_pending;           // commandId -> 挂起命令
    std::atomic<uint16_t> m_nextId{1};
    std::atomic<bool> m_stop{false};
};

#endif // COMMAND_MANAGER_H
