#ifndef DATA_DISPATCHER_H
#define DATA_DISPATCHER_H

#include <cstring>

#include <QDeadlineTimer>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QWaitCondition>

#include "protocol/control_frame.h"
#include "sdk/include/photon_camera.h"
#include "sdk/src/frame_packet.h"

// 根据 FPGA 协议命令码自动识别 PixelFormat。
// 命令码由 UDPDataThread 从 aa55 帧头提取，经 FramePacket.cmd_code 传递到此。
inline PixelFormat cmd_to_pixel_format(uint16_t cmd_code) {
    switch (cmd_code) {
        case CMD_DATA_IMAGE_2D:   return PIXEL_FMT_MONO8;
        case CMD_DATA_CENTERLINE: return PIXEL_FMT_CENTERLINE_F32;
        case CMD_DATA_POINTCLOUD: return PIXEL_FMT_POINTCLOUD_XYZ;
        default:                  return PIXEL_FMT_MONO8;  // 未知命令码默认按 2D 灰度处理
    }
}

// FramePacket → FrameInfo（C API 结构体）转换。
// 注意：SDK 不解析载荷、不重新生成元数据。
// dataType 根据 FramePacket.cmd_code 自动识别，无需调用方手动设置。
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
    info.dataType  = cmd_to_pixel_format(p.cmd_code);  // 根据 FPGA 命令码自动识别
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

    // 有界等待 sensorId 的特定类型最新一帧（Cam_GetImageSync 等用）：
    // 与 wait_latest 类似，但额外要求帧的 PixelFormat 匹配 targetType。
    // 若缓存中已有匹配帧则立即返回；否则等待匹配帧到达或超时。
    bool wait_latest_type(uint16_t sensorId, PixelFormat targetType, FramePacket* out, int timeoutMs);

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
