#ifndef DATA_DISPATCHER_H
#define DATA_DISPATCHER_H

#include <cstring>

#include <QDeadlineTimer>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QWaitCondition>

#include "sdk/include/photon_camera.h"
#include "sdk/src/frame_packet.h"

// FramePacket → FrameInfo（C API 结构体）转换。
// 注意：SDK 不解析载荷、不重新生成元数据。
// FramePacket 不含像素格式字段 → dataType 统一填 PIXEL_FMT_MONO8（2D 灰度默认），
// 如需精确格式应由宿主在 FramePacket 中提供。
inline void frame_to_info(const FramePacket& p, FrameInfo& info) {
    std::memset(&info, 0, sizeof(info));
    info.frameId   = p.frameId;
    info.sensorId  = p.sensorId;
    info.batchId   = p.batchId;
    info.lineIndex = p.lineIndex;
    info.encoder   = p.encoder;
    info.timestamp = p.timestamp;
    info.width     = p.width;
    info.height    = p.height;
    info.dataSize  = static_cast<uint32_t>(p.data.size());
    info.dataType  = PIXEL_FMT_MONO8;  // SDK 默认 2D 灰度，不解析载荷
}

// DataDispatcher: 帧路由队列 + 每传感器最新帧缓存 + 线程隔离。
//
// - 线程安全：enqueue / wait_pop / wait_latest 均可从任意线程调用（内部 mutex + CV）。
// - 只消费 ProtocolHandler::frame_ready 已完成组帧的数据，不解析协议、不组帧。
// - 队列供 CallbackWorker 依次回调；latest 缓存供 Cam_GetFrameSync 有界等待。
// - 帧字段原样入队，不改写任何来源字段。
class DataDispatcher {
public:
    DataDispatcher() = default;
    ~DataDispatcher() = default;

    DataDispatcher(const DataDispatcher&) = delete;
    DataDispatcher& operator=(const DataDispatcher&) = delete;

    // 从任意线程投递一帧：入队 + 更新每传感器最新帧缓存 + 唤醒等待者
    void enqueue(const FramePacket& frame);

    // 阻塞等待并取走队首一帧（CallbackWorker 用）；超时/空返回 false
    bool wait_pop(FramePacket* out, int timeoutMs);

    // 有界等待 sensorId 的最新一帧（Cam_GetFrameSync 用）：
    // 若缓存已有则立即返回；否则等待新帧到达或超时。
    bool wait_latest(uint16_t sensorId, FramePacket* out, int timeoutMs);

    // 非阻塞取队首一帧；无帧返回 false（诊断用）
    bool pop(FramePacket* out);

    // 当前队列深度（诊断用）
    int pending_count() const;

private:
    mutable QMutex m_mutex;
    QWaitCondition m_cv;
    QList<FramePacket> m_queue;             // 待回调帧 FIFO
    QHash<uint16_t, FramePacket> m_latest;  // 每传感器最新帧
};

#endif // DATA_DISPATCHER_H
