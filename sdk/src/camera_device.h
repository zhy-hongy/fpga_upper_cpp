#ifndef CAMERA_DEVICE_H
#define CAMERA_DEVICE_H

#include <cstdint>
#include <vector>

#include <QMetaObject>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QVector>
#include <QByteArray>

#include "sdk/include/photon_camera.h"
#include "sdk/src/protocol_adapter.h"
#include "sdk/src/data_dispatcher.h"
#include "sdk/src/callback_worker.h"
#include "sdk/src/command_manager.h"

// 批处理数据缓存：Cam_ReceiveBatchData 写入，Cam_GetBatchData 等读取。
// 线程安全：所有方法用 QMutex 保护，可从任意线程调用。
struct BatchBuffer {
    mutable QMutex mutex;
    std::vector<FrameInfo>  infos;        // 每帧的帧头信息
    std::vector<QByteArray> datas;        // 每帧的原始数据
    uint32_t                expectedCount = 0;

    void clear() {
        QMutexLocker lock(&mutex);
        infos.clear();
        datas.clear();
        expectedCount = 0;
    }

    uint32_t pointCount() const {
        QMutexLocker lock(&mutex);
        return static_cast<uint32_t>(infos.size());
    }

    uint32_t dataWidth() const {
        QMutexLocker lock(&mutex);
        return infos.empty() ? 0 : infos[0].width;
    }

    // 追加一帧（CallbackWorker 线程调用）
    void append(const FrameInfo& info, const QByteArray& data) {
        QMutexLocker lock(&mutex);
        infos.push_back(info);
        datas.push_back(data);
    }

    // 所有帧数据拼接为连续内存块
    QByteArray flatten() const {
        QMutexLocker lock(&mutex);
        QByteArray flat;
        for (const auto& d : datas)
            flat.append(d);
        return flat;
    }
};

// CameraDevice 是 SDK 内部设备对象，CameraHandle 句柄即指向它。
//
// 生命周期规则（本步骤核心约束）：
//   1. 只持有【借用】的 ProtocolHandler*，不创建、不释放。
//   2. 通信链路完全复用宿主（UDP / ProtocolHandler / worker 均由宿主持有）。
//   3. Cam_CloseDevice / Cam_Finalize 只销毁 SDK 内部 CameraDevice，
//      绝不删除借用的 ProtocolHandler。
//   4. CameraHandle 只代表 SDK 内部 CameraDevice，不代表宿主对象。
//
// 数据链路（Step6）：
//   ProtocolHandler::frame_ready(FramePacket)
//       → (lambda 直连) DataDispatcher::enqueue   [队列 + 最新帧缓存]
//       → CallbackWorker::run()                    [SDK 唯一新增线程]
//       → DataHandler                              [绝不运行在 UDP/UI 线程]
//   stop_data_path(): 先摘回调 → 停线程 → 断开信号连接，安全销毁。
class CameraDevice {
public:
    // protocol: 宿主已有的 ProtocolHandler*（借用）；index: 网关序号（当前固定 0）
    CameraDevice(ProtocolHandler* protocol, uint32_t index);
    ~CameraDevice();

    bool is_open() const { return m_open; }
    uint32_t index() const { return m_index; }
    QString ip() const { return m_ip; }
    void set_ip(const QString& ip) { m_ip = ip; }

    ProtocolHandler* protocol() const { return m_protocol; }
    ProtocolAdapter* adapter() { return &m_adapter; }
    DataDispatcher* dispatcher() { return m_dispatcher; }
    CallbackWorker* worker() { return m_worker; }
    CommandManager* command_manager() { return m_cmdMgr; }

    void open() { m_open = true; }
    void close() { m_open = false; }

    // 安全停止数据链路：clear_handler → request_stop → wait()（非回调线程场景）
    void stop_data_path();

private:
    ProtocolHandler* m_protocol;   // borrowed, never owned
    ProtocolAdapter  m_adapter;    // 借用指针的适配层（成员，随设备创建）
    uint32_t m_index;              // 网关序号
    QString  m_ip;                 // 网关 IP（当前仅记录，不建网）
    bool     m_open = false;

    DataDispatcher* m_dispatcher = nullptr;   // owned
    CallbackWorker* m_worker = nullptr;       // owned（SDK 唯一新增线程）
    CommandManager* m_cmdMgr = nullptr;       // owned（SDK 异步控制命令管理线程）
    QMetaObject::Connection m_frameConn;      // ProtocolHandler::frame_ready → dispatcher
};

#endif // CAMERA_DEVICE_H
