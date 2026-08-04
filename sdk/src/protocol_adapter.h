#ifndef PROTOCOL_ADAPTER_H
#define PROTOCOL_ADAPTER_H

#include <cstdint>
#include <functional>
#include <memory>

#include "sdk/include/photon_camera.h"
#include "communication/protocol_handler.h"

// ProtocolAdapter 是宿主 ProtocolHandler 的适配层（借用指针，不拥有）。
//
// 职责（只做转发，不是网络层）：
//   1. 保存借用的 ProtocolHandler*
//   2. 控制 API 转发：调用 ProtocolHandler 已有公开方法
//   3. 线程切换：通过 QMetaObject::invokeMethod 投递到宿主主线程执行
//   4. 等待 ACK：ACK 回调在控制线程触发，经 promise/CV 通知调用方线程，有界超时
//
// 禁止：创建 ProtocolHandler / 创建 UDP socket / 创建 worker / 重新解析协议 / 重新组帧。
// 注意：若从 UI 主线程调用，会阻塞至 ACK 或超时，建议从工作线程调用。
class ProtocolAdapter {
public:
    explicit ProtocolAdapter(ProtocolHandler* protocol);  // 借用，可传 nullptr

    bool is_valid() const { return m_protocol != nullptr; }
    ProtocolHandler* protocol() const { return m_protocol; }

    // ---- 控制 API 转发 ----
    // sensorId 1~4 单通道；sensorId=0 仅用于采集/批处理/缓冲操作，对 1~4 广播。
    CamError set_sensor_config(uint16_t sensorId, const SensorConfig& config);
    CamError get_sensor_config(uint16_t sensorId, SensorConfig& config);
    CamError set_trigger_config(uint16_t sensorId, const TriggerConfig& config);
    CamError get_trigger_config(uint16_t sensorId, TriggerConfig& config);
    CamError set_batch_config(uint16_t sensorId, const BatchConfig& config);
    CamError get_batch_config(uint16_t sensorId, BatchConfig& config);
    CamError start_capture(uint16_t sensorId);
    CamError stop_capture(uint16_t sensorId);
    CamError start_batch(uint16_t sensorId, uint32_t batchId);
    CamError stop_batch(uint16_t sensorId);
    CamError reset_buffer(uint16_t sensorId);

private:
    // ACK 回调类型（与 ProtocolHandler 方法回调签名一致）
    using AckCb = std::function<void(const ControlFrame&)>;
    // 转发调度类型：接收一个 ACK 回调，并把它传给具体 ProtocolHandler 方法
    using DispatchFn = std::function<void(const AckCb&)>;

    // 通用：把 dispatch 投递到宿主主线程执行，等待其 ACK 回调（有界超时）。
    // outData 非空时，把 ACK 响应帧的 data 拷贝给调用方（用于 Get 解析 BE 数据）。
    CamError invoke_and_wait(const DispatchFn& dispatch, QByteArray* outData = nullptr,
                             int timeoutMs = 4000);

    ProtocolHandler* m_protocol;  // borrowed, never owned
};

#endif // PROTOCOL_ADAPTER_H
