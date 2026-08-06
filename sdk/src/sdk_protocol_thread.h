#ifndef SDK_PROTOCOL_THREAD_H
#define SDK_PROTOCOL_THREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>

#include "utils/config.h"

class ProtocolHandler;

// 独立 SDK 模式内部通信宿主线程。
//
// 架构（用户决策"直接复用 ProtocolHandler"）：
//   - run() 在自身线程创建内部 ProtocolHandler（或测试用 MockProtocolHandler），
//     使 ProtocolAdapter 的 QueuedConnection 投递 + ProtocolHandler 内部 QTimer
//     超时在事件循环内正常运行。
//   - 真实模式只创建【一套】UDPWorker + UDPDataThread（= 一个 ProtocolHandler），
//     端口映射与宿主一致：控制绑定 controlPort、数据绑定 dataPort、命令发往 ip:dataPort。
//   - Mock 模式不 start_all()：mock 基类不启动 → 零 socket、零 UDP 活动。
//   - 线程/对象由 SDK 拥有；request_stop() 停通信 + 退出事件循环 + 等待线程结束。
class SdkProtocolThread : public QThread {
    Q_OBJECT
public:
    explicit SdkProtocolThread(const Config& config, bool mockMode, QObject* parent = nullptr);
    ~SdkProtocolThread() override;

    // 阻塞等待 run() 完成内部 handler 创建（有界超时），返回内部 handler（thread-safe）。
    // 超时返回 nullptr（调用方应 request_stop + delete）。
    ProtocolHandler* wait_ready(int timeoutMs = 10000);

    // 停内部通信 + 退出事件循环 + 等待线程退出（幂等，可重复调用）。
    // 禁止在 SdkProtocolThread 自身线程内调用（内部 wait() 会自死锁）。
    void request_stop();

protected:
    void run() override;

private:
    Config m_config;
    bool m_mockMode = false;
    ProtocolHandler* m_handler = nullptr;   // 仅在 run() 线程访问
    QMutex m_mutex;
    QWaitCondition m_startCv;
    bool m_started = false;
};

#endif // SDK_PROTOCOL_THREAD_H
