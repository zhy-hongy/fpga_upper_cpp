#include "protocol_adapter.h"

#include "sdk/src/command_manager.h"

#include <QMetaObject>
#include <QThread>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>

namespace {

bool valid_sensor(uint16_t sid) {
    return sid >= 1 && sid <= 4;
}

// 4 字节大端解析
uint32_t be32(const QByteArray& d) {
    if (d.size() < 4) return 0;
    return (static_cast<uint32_t>(static_cast<uint8_t>(d[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d[2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(d[3]));
}

// 2 字节大端解析
uint16_t be16(const QByteArray& d) {
    if (d.size() < 2) return 0;
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(static_cast<uint8_t>(d[0])) << 8) |
        static_cast<uint8_t>(d[1]));
}

}  // namespace

ProtocolAdapter::ProtocolAdapter(ProtocolHandler* protocol)
    : m_protocol(protocol) {}

CamError ProtocolAdapter::invoke_and_wait(const DispatchFn& dispatch,
                                          QByteArray* outData,
                                          int timeoutMs) {
    if (!m_protocol) return CAM_ERR_NOT_INITIALIZED;

    struct Ack {
        bool fired = false;
        QByteArray data;
    };

    auto promise = std::make_shared<std::promise<Ack>>();
    auto future = promise->get_future();

    // 投递到宿主主线程（ProtocolHandler 所在线程）执行转发。
    // 注意：若调用方就是主线程，QueuedConnection 事件要等主线程回到事件循环才执行，
    // 而此时主线程正阻塞在 future.wait_for —— 会一直等到超时。
    bool posted = QMetaObject::invokeMethod(
        m_protocol,
        [dispatch, promise]() {
            auto callback = [promise](const ControlFrame& frame) {
                Ack a;
                a.fired = true;
                a.data = frame.data;
                try { promise->set_value(std::move(a)); }
                catch (...) { /* future 已销毁 / 已消费 */ }
            };
            try {
                dispatch(callback);
            } catch (...) {
                try { promise->set_value(Ack{}); }
                catch (...) {}
            }
        },
        Qt::QueuedConnection);

    if (!posted) return CAM_ERR_UNKNOWN;

    if (future.wait_for(std::chrono::milliseconds(timeoutMs)) == std::future_status::timeout)
        return CAM_ERR_TIMEOUT;

    Ack a = future.get();
    if (!a.fired) return CAM_ERR_UNKNOWN;
    if (outData) *outData = a.data;
    return CAM_OK;
}

// ── Sensor 参数读写 ──

CamError ProtocolAdapter::set_sensor_config(uint16_t sensorId, const SensorConfig& cfg) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    // 当前协议无对应命令：roiStart/roiWidth/pixelPitch/zMin/zMax -> NOT_SUPPORTED
    if (cfg.roiStart != 0 || cfg.roiWidth != 0 || cfg.pixelPitch != 0.0f ||
        cfg.zMin != 0 || cfg.zMax != 0) {
        return CAM_ERR_NOT_SUPPORTED;
    }

    CamError e = invoke_and_wait([this, sensorId, v = cfg.exposureTime](const AckCb& cb) {
        m_protocol->set_exposure(static_cast<int>(v), sensorId, cb);
    });
    if (e != CAM_OK) return e;

    e = invoke_and_wait([this, sensorId, v = cfg.gain](const AckCb& cb) {
        m_protocol->set_gain(static_cast<int>(v), sensorId, cb);
    });
    if (e != CAM_OK) return e;

    e = invoke_and_wait([this, sensorId, v = cfg.exposureDelay](const AckCb& cb) {
        m_protocol->set_exposure_delay(static_cast<int>(v), sensorId, cb);
    });
    if (e != CAM_OK) return e;

    return invoke_and_wait([this, sensorId, v = cfg.sampleRate](const AckCb& cb) {
        m_protocol->set_sample_rate(static_cast<int>(v), sensorId, cb);
    });
}

CamError ProtocolAdapter::get_sensor_config(uint16_t sensorId, SensorConfig& cfg) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;

    QByteArray data;
    CamError e = invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->read_exposure(sensorId, cb);
    }, &data);
    if (e != CAM_OK) return e;
    cfg.exposureTime = be32(data);

    e = invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->read_gain(sensorId, cb);
    }, &data);
    if (e != CAM_OK) return e;
    cfg.gain = be32(data);

    e = invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->read_exposure_delay(sensorId, cb);
    }, &data);
    if (e != CAM_OK) return e;
    cfg.exposureDelay = be32(data);

    e = invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->read_sample_rate(sensorId, cb);
    }, &data);
    if (e != CAM_OK) return e;
    cfg.sampleRate = be32(data);

    return CAM_OK;
}

// ── Trigger & Batch ──

CamError ProtocolAdapter::set_trigger_config(uint16_t sensorId, const TriggerConfig& cfg) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;

    CamError e = invoke_and_wait([this, sensorId, v = cfg.triggerMode](const AckCb& cb) {
        m_protocol->set_trigger_mode(v, sensorId, cb);
    });
    if (e != CAM_OK) return e;

    e = invoke_and_wait([this, sensorId, v = cfg.encoderType](const AckCb& cb) {
        m_protocol->set_encoder_type(v, sensorId, cb);
    });
    if (e != CAM_OK) return e;

    e = invoke_and_wait([this, sensorId, v = cfg.encoderInputMode](const AckCb& cb) {
        m_protocol->set_encoder_mode(v, sensorId, cb);
    });
    if (e != CAM_OK) return e;

    e = invoke_and_wait([this, sensorId, v = cfg.encoderStep](const AckCb& cb) {
        m_protocol->set_encoder_interval(static_cast<int>(v), sensorId, cb);
    });
    if (e != CAM_OK) return e;

    return invoke_and_wait([this, sensorId, v = (cfg.externalEnable != 0)](const AckCb& cb) {
        m_protocol->set_external_trigger(v, sensorId, cb);
    });
}

CamError ProtocolAdapter::get_trigger_config(uint16_t sensorId, TriggerConfig& cfg) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;

    QByteArray data;
    CamError e = invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->read_trigger_mode(sensorId, cb);
    }, &data);
    if (e != CAM_OK) return e;
    cfg.triggerMode = be16(data);

    e = invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->read_encoder_type(sensorId, cb);
    }, &data);
    if (e != CAM_OK) return e;
    cfg.encoderType = be16(data);

    e = invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->read_encoder_mode(sensorId, cb);
    }, &data);
    if (e != CAM_OK) return e;
    cfg.encoderInputMode = be16(data);

    e = invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->read_encoder_interval(sensorId, cb);
    }, &data);
    if (e != CAM_OK) return e;
    cfg.encoderStep = be32(data);

    e = invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->read_external_trigger(sensorId, cb);
    }, &data);
    if (e != CAM_OK) return e;
    cfg.externalEnable = be16(data);

    return CAM_OK;
}

CamError ProtocolAdapter::set_batch_config(uint16_t sensorId, const BatchConfig& cfg) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return invoke_and_wait([this, sensorId, v = cfg.length](const AckCb& cb) {
        m_protocol->set_batch_length(v, sensorId, cb);
    });
}

CamError ProtocolAdapter::get_batch_config(uint16_t sensorId, BatchConfig& cfg) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    QByteArray data;
    CamError e = invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->read_batch_length(sensorId, cb);
    }, &data);
    if (e != CAM_OK) return e;
    cfg.length = be16(data);
    return CAM_OK;
}

// ── 采集控制（sensorId=0 对 1~4 广播）──

CamError ProtocolAdapter::start_capture(uint16_t sensorId) {
    if (sensorId == 0) {
        CamError result = CAM_OK;
        for (int sid = 1; sid <= 4; ++sid) {
            CamError e = start_capture(static_cast<uint16_t>(sid));
            if (e != CAM_OK) result = e;
        }
        return result;
    }
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->start_capture(sensorId, cb);
    });
}

CamError ProtocolAdapter::stop_capture(uint16_t sensorId) {
    if (sensorId == 0) {
        CamError result = CAM_OK;
        for (int sid = 1; sid <= 4; ++sid) {
            CamError e = stop_capture(static_cast<uint16_t>(sid));
            if (e != CAM_OK) result = e;
        }
        return result;
    }
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->stop_capture(sensorId, cb);
    });
}

CamError ProtocolAdapter::start_batch(uint16_t sensorId, uint32_t /*batchId*/) {
    if (sensorId == 0) {
        CamError result = CAM_OK;
        for (int sid = 1; sid <= 4; ++sid) {
            CamError e = start_batch(static_cast<uint16_t>(sid), 0);
            if (e != CAM_OK) result = e;
        }
        return result;
    }
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    // batchId 暂不支持，忽略；只下发 batch 开始控制
    return invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->set_batch_control(true, sensorId, cb);
    });
}

CamError ProtocolAdapter::stop_batch(uint16_t sensorId) {
    if (sensorId == 0) {
        CamError result = CAM_OK;
        for (int sid = 1; sid <= 4; ++sid) {
            CamError e = stop_batch(static_cast<uint16_t>(sid));
            if (e != CAM_OK) result = e;
        }
        return result;
    }
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->set_batch_control(false, sensorId, cb);
    });
}

CamError ProtocolAdapter::reset_buffer(uint16_t sensorId) {
    if (sensorId == 0) {
        CamError result = CAM_OK;
        for (int sid = 1; sid <= 4; ++sid) {
            CamError e = reset_buffer(static_cast<uint16_t>(sid));
            if (e != CAM_OK) result = e;
        }
        return result;
    }
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->reset_buffer(sensorId, cb);
    });
}

CamError ProtocolAdapter::get_batch_data(uint16_t sensorId) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return invoke_and_wait([this, sensorId](const AckCb& cb) {
        m_protocol->get_batch_data(sensorId, cb);
    });
}

// =========================================================================
// 异步非阻塞控制 API 转发（立即返回 CAM_PENDING）
// =========================================================================

void ProtocolAdapter::set_command_manager(CommandManager* mgr) {
    m_cmdMgr = mgr;
}

CamError ProtocolAdapter::post_async(const std::function<void(uint16_t)>& dispatch,
                                     CommandResultCallback cb, void* userData,
                                     uint32_t timeoutMs) {
    if (!m_protocol) return CAM_ERR_NOT_INITIALIZED;
    if (!m_cmdMgr) return CAM_ERR_NOT_INITIALIZED;
    if (!cb) return CAM_ERR_INVALID_PARAM;

    // 1) 先注册 commandId（保存 callback），再投递到宿主主线程发送命令。
    const uint16_t id = m_cmdMgr->submit(cb, userData, static_cast<int>(timeoutMs));

    // 2) 投递 dispatch 到 ProtocolHandler 所在线程（不等待 ACK）。
    bool posted = QMetaObject::invokeMethod(
        m_protocol, [dispatch, id]() { dispatch(id); }, Qt::QueuedConnection);

    if (!posted) {
        m_cmdMgr->fail(id, CAM_ERR_UNKNOWN);
        return CAM_ERR_UNKNOWN;
    }
    return CAM_PENDING;
}

CamError ProtocolAdapter::start_capture_async(uint16_t sensorId, uint32_t timeoutMs,
                                              CommandResultCallback cb, void* userData) {
    if (sensorId == 0) {
        CamError result = CAM_PENDING;
        for (int sid = 1; sid <= 4; ++sid) {
            CamError e = start_capture_async(static_cast<uint16_t>(sid), timeoutMs, cb, userData);
            if (e != CAM_PENDING) result = e;
        }
        return result;
    }
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return post_async([this, sensorId](uint16_t id) {
        m_protocol->start_capture(sensorId, [this, id](const ControlFrame&) { m_cmdMgr->ack(id); });
    }, cb, userData, timeoutMs);
}

CamError ProtocolAdapter::stop_capture_async(uint16_t sensorId, uint32_t timeoutMs,
                                             CommandResultCallback cb, void* userData) {
    if (sensorId == 0) {
        CamError result = CAM_PENDING;
        for (int sid = 1; sid <= 4; ++sid) {
            CamError e = stop_capture_async(static_cast<uint16_t>(sid), timeoutMs, cb, userData);
            if (e != CAM_PENDING) result = e;
        }
        return result;
    }
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return post_async([this, sensorId](uint16_t id) {
        m_protocol->stop_capture(sensorId, [this, id](const ControlFrame&) { m_cmdMgr->ack(id); });
    }, cb, userData, timeoutMs);
}

CamError ProtocolAdapter::set_sensor_config_async(uint16_t sensorId, const SensorConfig& cfg,
                                                  uint32_t timeoutMs,
                                                  CommandResultCallback cb, void* userData) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    if (cfg.roiStart != 0 || cfg.roiWidth != 0 || cfg.pixelPitch != 0.0f ||
        cfg.zMin != 0 || cfg.zMax != 0) {
        return CAM_ERR_NOT_SUPPORTED;
    }
    return post_async([this, sensorId, v = cfg.exposureTime, g = cfg.gain,
                       d = cfg.exposureDelay, s = cfg.sampleRate](uint16_t id) {
        auto state = std::make_shared<std::atomic<int>>(4);
        auto done = [this, id, state]() {
            if (state->fetch_sub(1) == 1) m_cmdMgr->ack(id);
        };
        m_protocol->set_exposure(static_cast<int>(v), sensorId, [done](const ControlFrame&) { done(); });
        m_protocol->set_gain(static_cast<int>(g), sensorId, [done](const ControlFrame&) { done(); });
        m_protocol->set_exposure_delay(static_cast<int>(d), sensorId, [done](const ControlFrame&) { done(); });
        m_protocol->set_sample_rate(static_cast<int>(s), sensorId, [done](const ControlFrame&) { done(); });
    }, cb, userData, timeoutMs);
}

CamError ProtocolAdapter::get_sensor_config_async(uint16_t sensorId, SensorConfig* out,
                                                  uint32_t timeoutMs,
                                                  CommandResultCallback cb, void* userData) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    if (!out) return CAM_ERR_INVALID_PARAM;
    return post_async([this, sensorId, out](uint16_t id) {
        auto state = std::make_shared<std::atomic<int>>(4);
        auto done = [this, id, state]() {
            if (state->fetch_sub(1) == 1) m_cmdMgr->ack(id);
        };
        m_protocol->read_exposure(sensorId, [out, done](const ControlFrame& f) { out->exposureTime = be32(f.data); done(); });
        m_protocol->read_gain(sensorId, [out, done](const ControlFrame& f) { out->gain = be32(f.data); done(); });
        m_protocol->read_exposure_delay(sensorId, [out, done](const ControlFrame& f) { out->exposureDelay = be32(f.data); done(); });
        m_protocol->read_sample_rate(sensorId, [out, done](const ControlFrame& f) { out->sampleRate = be32(f.data); done(); });
    }, cb, userData, timeoutMs);
}

CamError ProtocolAdapter::set_trigger_config_async(uint16_t sensorId, const TriggerConfig& cfg,
                                                   uint32_t timeoutMs,
                                                   CommandResultCallback cb, void* userData) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return post_async([this, sensorId, tm = cfg.triggerMode, et = cfg.encoderType,
                       em = cfg.encoderInputMode, es = cfg.encoderStep,
                       ee = cfg.externalEnable](uint16_t id) {
        auto state = std::make_shared<std::atomic<int>>(5);
        auto done = [this, id, state]() {
            if (state->fetch_sub(1) == 1) m_cmdMgr->ack(id);
        };
        m_protocol->set_trigger_mode(tm, sensorId, [done](const ControlFrame&) { done(); });
        m_protocol->set_encoder_type(et, sensorId, [done](const ControlFrame&) { done(); });
        m_protocol->set_encoder_mode(em, sensorId, [done](const ControlFrame&) { done(); });
        m_protocol->set_encoder_interval(static_cast<int>(es), sensorId, [done](const ControlFrame&) { done(); });
        m_protocol->set_external_trigger(ee != 0, sensorId, [done](const ControlFrame&) { done(); });
    }, cb, userData, timeoutMs);
}

CamError ProtocolAdapter::get_trigger_config_async(uint16_t sensorId, TriggerConfig* out,
                                                   uint32_t timeoutMs,
                                                   CommandResultCallback cb, void* userData) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    if (!out) return CAM_ERR_INVALID_PARAM;
    return post_async([this, sensorId, out](uint16_t id) {
        auto state = std::make_shared<std::atomic<int>>(5);
        auto done = [this, id, state]() {
            if (state->fetch_sub(1) == 1) m_cmdMgr->ack(id);
        };
        m_protocol->read_trigger_mode(sensorId, [out, done](const ControlFrame& f) { out->triggerMode = be16(f.data); done(); });
        m_protocol->read_encoder_type(sensorId, [out, done](const ControlFrame& f) { out->encoderType = be16(f.data); done(); });
        m_protocol->read_encoder_mode(sensorId, [out, done](const ControlFrame& f) { out->encoderInputMode = be16(f.data); done(); });
        m_protocol->read_encoder_interval(sensorId, [out, done](const ControlFrame& f) { out->encoderStep = be32(f.data); done(); });
        m_protocol->read_external_trigger(sensorId, [out, done](const ControlFrame& f) { out->externalEnable = be16(f.data); done(); });
    }, cb, userData, timeoutMs);
}

CamError ProtocolAdapter::set_batch_config_async(uint16_t sensorId, const BatchConfig& cfg,
                                                 uint32_t timeoutMs,
                                                 CommandResultCallback cb, void* userData) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return post_async([this, sensorId, v = cfg.length](uint16_t id) {
        m_protocol->set_batch_length(v, sensorId, [this, id](const ControlFrame&) { m_cmdMgr->ack(id); });
    }, cb, userData, timeoutMs);
}

CamError ProtocolAdapter::get_batch_config_async(uint16_t sensorId, BatchConfig* out,
                                                 uint32_t timeoutMs,
                                                 CommandResultCallback cb, void* userData) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    if (!out) return CAM_ERR_INVALID_PARAM;
    return post_async([this, sensorId, out](uint16_t id) {
        m_protocol->read_batch_length(sensorId, [out, this, id](const ControlFrame& f) {
            out->length = be16(f.data);
            m_cmdMgr->ack(id);
        });
    }, cb, userData, timeoutMs);
}

CamError ProtocolAdapter::start_batch_async(uint16_t sensorId, uint32_t /*batchId*/,
                                            uint32_t timeoutMs,
                                            CommandResultCallback cb, void* userData) {
    if (sensorId == 0) {
        CamError result = CAM_PENDING;
        for (int sid = 1; sid <= 4; ++sid) {
            CamError e = start_batch_async(static_cast<uint16_t>(sid), 0, timeoutMs, cb, userData);
            if (e != CAM_PENDING) result = e;
        }
        return result;
    }
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return post_async([this, sensorId](uint16_t id) {
        m_protocol->set_batch_control(true, sensorId, [this, id](const ControlFrame&) { m_cmdMgr->ack(id); });
    }, cb, userData, timeoutMs);
}

CamError ProtocolAdapter::stop_batch_async(uint16_t sensorId, uint32_t timeoutMs,
                                           CommandResultCallback cb, void* userData) {
    if (sensorId == 0) {
        CamError result = CAM_PENDING;
        for (int sid = 1; sid <= 4; ++sid) {
            CamError e = stop_batch_async(static_cast<uint16_t>(sid), timeoutMs, cb, userData);
            if (e != CAM_PENDING) result = e;
        }
        return result;
    }
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return post_async([this, sensorId](uint16_t id) {
        m_protocol->set_batch_control(false, sensorId, [this, id](const ControlFrame&) { m_cmdMgr->ack(id); });
    }, cb, userData, timeoutMs);
}

CamError ProtocolAdapter::reset_buffer_async(uint16_t sensorId, uint32_t timeoutMs,
                                             CommandResultCallback cb, void* userData) {
    if (sensorId == 0) {
        CamError result = CAM_PENDING;
        for (int sid = 1; sid <= 4; ++sid) {
            CamError e = reset_buffer_async(static_cast<uint16_t>(sid), timeoutMs, cb, userData);
            if (e != CAM_PENDING) result = e;
        }
        return result;
    }
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return post_async([this, sensorId](uint16_t id) {
        m_protocol->reset_buffer(sensorId, [this, id](const ControlFrame&) { m_cmdMgr->ack(id); });
    }, cb, userData, timeoutMs);
}

CamError ProtocolAdapter::get_batch_data_async(uint16_t sensorId, uint32_t timeoutMs,
                                               CommandResultCallback cb, void* userData) {
    if (!valid_sensor(sensorId)) return CAM_ERR_INVALID_PARAM;
    return post_async([this, sensorId](uint16_t id) {
        m_protocol->get_batch_data(sensorId, [this, id](const ControlFrame&) { m_cmdMgr->ack(id); });
    }, cb, userData, timeoutMs);
}

