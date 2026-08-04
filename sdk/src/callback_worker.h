#ifndef CALLBACK_WORKER_H
#define CALLBACK_WORKER_H

#include <atomic>

#include <QMutex>
#include <QThread>

#include "sdk/include/photon_camera.h"
#include "sdk/src/data_dispatcher.h"

// CallbackWorker: SDK 唯一新增线程。
//
// - run() 从 DataDispatcher 依次取帧，调用已注册的 DataHandler。
// - DataHandler 绝不运行在 UDP/UI 线程，只在本 worker 线程执行。
// - 与 DataDispatcher 协作：dispatcher 负责队列/最新帧缓存，worker 只负责回调。
// - 生命周期由 CameraDevice 管理：先 clear_handler + request_stop + wait() 再销毁。
class CallbackWorker : public QThread {
    Q_OBJECT
public:
    explicit CallbackWorker(DataDispatcher* dispatcher, QObject* parent = nullptr);
    ~CallbackWorker() override;

    // 线程安全：设置/清除回调（可从任意线程调用）
    void set_handler(DataHandler handler, void* userData);
    void clear_handler();
    bool has_handler() const;

    // 请求停止 run() 循环（配合 wait() 使用，停止延迟 ≤ 200ms）
    void request_stop();

protected:
    void run() override;

private:
    DataDispatcher* m_dispatcher;   // borrowed, owned by CameraDevice
    mutable QMutex m_handlerMutex;  // 保护 handler + userData
    DataHandler m_handler = nullptr;
    void* m_userData = nullptr;
    std::atomic<bool> m_stop{false};
};

#endif // CALLBACK_WORKER_H
