#ifndef CALLBACK_WORKER_H
#define CALLBACK_WORKER_H

#include <atomic>

#include <QMutex>
#include <QThread>
#include <QVector>
#include <QDateTime>

#include "sdk/include/photon_camera.h"
#include "sdk/src/data_dispatcher.h"

// 异步帧回调类型：与 CommandResultCallback 不同，它携带帧数据。
// 同步帧 API 的超时错误通过同步返回值报告；异步帧 API 通过此回调报告。
// 注意：回调在 CallbackWorker 线程执行，不要在回调中调用 Cam_CloseDevice 或 Cam_Finalize。
typedef void(CAM_CALL *AsyncFrameCallback)(
    CamError result,
    const FrameInfo* pFrameInfo,
    const void* pData,
    void* pUserData
);

// CallbackWorker: SDK 唯一新增线程。
//
// - run() 从 DataDispatcher 依次取帧，调用已注册的 DataHandler。
// - DataHandler 绝不运行在 UDP/UI 线程，只在本 worker 线程执行。
// - 与 DataDispatcher 协作：dispatcher 负责队列/最新帧缓存，worker 只负责回调。
// - 生命周期由 CameraDevice 管理：先 clear_handler + request_stop + wait() 再销毁。
// - 额外支持一次性异步帧请求（AsyncFrameRequest）：优先匹配，匹配后自动移除。
class CallbackWorker : public QThread {
    Q_OBJECT
public:
    explicit CallbackWorker(DataDispatcher* dispatcher, QObject* parent = nullptr);
    ~CallbackWorker() override;

    // 线程安全：设置/清除回调（可从任意线程调用）
    void set_handler(DataHandler handler, void* userData);
    void clear_handler();
    bool has_handler() const;

    // 获取当前注册的回调和透传指针（线程安全）
    DataHandler handler() const;
    void* userData() const;

    // 提交一次性异步帧请求：等待 sensorId 的 targetType 类型帧，最多等待 timeoutMs 毫秒。
    // 匹配成功时 callback 收到 CAM_OK + 帧数据；超时时收到 CAM_ERR_TIMEOUT。
    // 线程安全（可从任意线程调用）。
    void submit_async_request(uint16_t sensorId, PixelFormat targetType,
                              uint32_t timeoutMs, AsyncFrameCallback callback,
                              void* userData);

    // 请求停止 run() 循环（配合 wait() 使用，停止延迟 ≤ 200ms）
    void request_stop();

protected:
    void run() override;

private:
    struct AsyncFrameRequest {
        uint16_t sensorId;
        PixelFormat targetType;
        qint64 deadlineMs;          // 绝对截止时间（epoch ms）
        AsyncFrameCallback callback;
        void* userData;
    };

    DataDispatcher* m_dispatcher;   // borrowed, owned by CameraDevice
    mutable QMutex m_handlerMutex;  // 保护 handler + userData
    DataHandler m_handler = nullptr;
    void* m_userData = nullptr;

    mutable QMutex m_requestsMutex; // 保护 m_asyncRequests
    QVector<AsyncFrameRequest> m_asyncRequests;

    std::atomic<bool> m_stop{false};
};

#endif // CALLBACK_WORKER_H