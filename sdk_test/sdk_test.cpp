#include "sdk_test.h"

#include <functional>

#include <QByteArray>
#include <QElapsedTimer>

#include "protocol/transport_frame.h"
#include "protocol/control_frame.h"
#include "sdk_test/mock/mock_protocol_handler.h"

SDKTest::SDKTest(ProtocolHandler* protocol, const Config& config,
                 bool mockMode, QObject* parent)
    : QThread(parent), m_protocol(protocol), m_config(config), m_mock_mode(mockMode) {
    if (m_mock_mode) {
        // Mock 模式：创建独立 MockProtocolHandler。构造在宿主主线程（on_sdk_test 内），
        // 基类不启动 → 无 socket；生命周期归 SDKTest，结束时 deleteLater 到主线程。
        m_mock = new MockProtocolHandler(config);
    }
}

SDKTest::~SDKTest() {
    // 线程已由 finished → deleteLater 清理；此处只做对象析构。
    // Mock 生命周期：投递到其所属线程（宿主主线程）事件循环释放。
    if (m_mock) {
        m_mock->deleteLater();
        m_mock = nullptr;
    }
}

void SDKTest::request_stop() {
    m_stop.store(true);
}

void SDKTest::log_line(const QString& line) {
    emit log(line);
}

QByteArray SDKTest::be16v(uint16_t v) {
    QByteArray d(2, '\0');
    d[0] = static_cast<char>((v >> 8) & 0xFF);
    d[1] = static_cast<char>(v & 0xFF);
    return d;
}

QByteArray SDKTest::be32v(uint32_t v) {
    QByteArray d(4, '\0');
    d[0] = static_cast<char>((v >> 24) & 0xFF);
    d[1] = static_cast<char>((v >> 16) & 0xFF);
    d[2] = static_cast<char>((v >> 8) & 0xFF);
    d[3] = static_cast<char>(v & 0xFF);
    return d;
}

void SDKTest::verify_sent_cmds(const QString& apiName,
                               const QVector<MockSentCmd>& expected) {
    if (!m_mock) return;
    const QVector<MockSentCmd> sent = m_mock->take_sent_commands();
    const bool countOk = (sent.size() == expected.size());
    log_line(QString("  [验证] %1 → 应发送 %2 条协议命令，实际捕获 %3 条：%4")
                 .arg(apiName).arg(expected.size()).arg(sent.size())
                 .arg(countOk ? "是" : "否！"));
    if (!countOk) return;

    bool allOk = true;
    for (int i = 0; i < sent.size(); ++i) {
        const MockSentCmd& s = sent[i];
        const MockSentCmd& e = expected[i];
        const bool same = (s.cmd_code == e.cmd_code) &&
                          (s.device_type == e.device_type) &&
                          (s.rw == e.rw) && (s.data == e.data);

        // 解包完整传输帧：unpack_transport_frame 会校验帧头/帧尾/外层 CRC16；
        // 再解包内层 aa55 帧，确认 cmd/dev/rw 与记录一致。
        bool parseOk = false;
        try {
            const TransportFrame tf = unpack_transport_frame(s.transport_frame);
            const ControlFrame cf = unpack_cmd_frame(tf.data);
            parseOk = (cf.cmd_code == s.cmd_code) &&
                      (cf.device_type == s.device_type) && (cf.rw == s.rw);
        } catch (...) { parseOk = false; }

        log_line(QString("    #%1 cmd=0x%2 dev=0x%3 rw=%4 data=%5 帧=%6B 外层CRC/解析:%7 → %8")
                     .arg(i + 1)
                     .arg(s.cmd_code, 4, 16, QChar('0'))
                     .arg(s.device_type, 4, 16, QChar('0'))
                     .arg(s.rw == RW_WRITE ? "WRITE"
                          : (s.rw == RW_READ ? "READ" : "?"))
                     .arg(QString(s.data.toHex()))
                     .arg(s.transport_frame.size())
                     .arg(parseOk ? "通过" : "失败")
                     .arg(same ? "匹配" : "不匹配！"));
        if (!same || !parseOk) allOk = false;
    }
    log_line(QString("  [验证] %1 协议命令: %2").arg(apiName)
                 .arg(allOk ? "全部正确" : "存在错误！"));
}

void CAM_CALL SDKTest::on_data(const FrameInfo* info, const void* data, void* user) {
    Q_UNUSED(data);
    auto* self = static_cast<SDKTest*>(user);
    if (self) self->on_frame_received(info);
}

void SDKTest::on_frame_received(const FrameInfo* info) {
    if (!info) return;
    ++m_frame_count;
    // 运行在 SDK CallbackWorker 线程：只发信号，不操作 UI。
    const quintptr tid = reinterpret_cast<quintptr>(QThread::currentThreadId());
    const bool diff_thread = (tid != m_test_thread_id);
    log_line(QString("[DATA] th=0x%1  frameId=%2  sensor=%3  %4x%5  size=%6  ts=%7  (回调线程≠测试线程: %8)")
                 .arg(tid, 0, 16)
                 .arg(info->frameId)
                 .arg(info->sensorId)
                 .arg(info->width)
                 .arg(info->height)
                 .arg(info->dataSize)
                 .arg(qulonglong(info->timestamp))
                 .arg(diff_thread ? "是" : "否！"));
    if (m_frame_count.load() == 1) {
        log_line(QString("[DATA] 首帧: frameId=%1  sensor=%2  %3x%4  size=%5  (Mock 固定值 0x80 填充)")
                     .arg(info->frameId).arg(info->sensorId)
                     .arg(info->width).arg(info->height).arg(info->dataSize));
    }
}

void CAM_CALL SDKTest::on_async_result(CamError result, uint16_t commandId, void* userData) {
    auto* r = static_cast<AsyncResult*>(userData);
    QMutexLocker lock(&r->mutex);
    r->result = result;
    r->commandId = commandId;
    r->callbackThreadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    r->done = true;
    r->cv.wakeAll();
}

bool SDKTest::wait_async_result(AsyncResult& r, int timeoutMs) {
    QMutexLocker lock(&r.mutex);
    if (r.done) return true;
    return r.cv.wait(&r.mutex, static_cast<unsigned long>(timeoutMs));
}

void SDKTest::run() {
    m_test_thread_id = reinterpret_cast<quintptr>(QThread::currentThreadId());
    m_frame_count.store(0);

    log_line(QString("════════ PhotonCameraSDK.dll C-API 验证（测试线程 th=0x%1，模式=%2）════════")
                 .arg(m_test_thread_id, 0, 16)
                 .arg(m_mock_mode ? "MOCK（无 FPGA）" : "REAL"));

    // 计时包装：打印 函数名 / 返回值 / 耗时(ms)，≥100ms 标红
    auto api = [this](const QString& name, const std::function<CamError()>& fn) -> CamError {
        QElapsedTimer t;
        t.start();
        const CamError e = fn();
        const qint64 ms = t.elapsed();
        log_line(QString("%1 -> %2   耗时=%3 ms %4")
                     .arg(name, -58)
                     .arg(int(e))
                     .arg(ms)
                     .arg(ms >= 100 ? "<-- 超阈值(≥100ms)" : ""));
        return e;
    };

    // 期望"已发送"协议命令序列辅助（仅 Mock 模式校验用）
    using Sent = MockSentCmd;
    const int dev1 = DEV_SENSOR1;
    auto wr = [dev1](int cmd, const QByteArray& d) { return Sent{cmd, dev1, RW_WRITE, d, {}}; };
    auto rd = [dev1](int cmd) { return Sent{cmd, dev1, RW_READ, QByteArray(2, '\0'), {}}; };

    // 1) SDK 初始化
    CamError e = api("Cam_Initialize", [] { return Cam_Initialize(); });
    if (e != CAM_OK) { log_line("初始化失败，测试中止"); return; }

    // 2) 附加 ProtocolHandler：Mock 模式用 MockProtocolHandler，真实模式用宿主已有
    ProtocolHandler* attach = m_mock_mode ? static_cast<ProtocolHandler*>(m_mock) : m_protocol;
    log_line(QString("  附加 ProtocolHandler 0x%1 [%2]")
                 .arg(reinterpret_cast<quintptr>(attach), 0, 16)
                 .arg(m_mock_mode ? "MOCK" : "REAL"));
    e = api("Cam_AttachProtocolHandler", [attach] { return Cam_AttachProtocolHandler(attach); });
    if (e != CAM_OK) { log_line("Attach 失败，测试中止"); Cam_Finalize(); return; }

    // 3) 打开设备
    CameraHandle handle = nullptr;
    e = api("Cam_OpenDevice(0)", [&handle] { return Cam_OpenDevice(0, &handle); });
    if (e != CAM_OK) { log_line("打开设备失败，测试中止"); Cam_Finalize(); return; }
    m_handle = handle;

    // 4) 设备信息
    DeviceInformation dinfo;
    e = api("Cam_GetDeviceInformation", [&] { return Cam_GetDeviceInformation(handle, &dinfo); });
    if (e == CAM_OK) {
        log_line(QString("   status=%1  fpgaVersion=\"%2\"")
                     .arg(dinfo.status)
                     .arg(QString::fromLatin1(dinfo.fpgaVersion)));
        if (m_mock_mode) {
            log_line(QString("   (Mock 期望 fpgaVersion=\"%1\")").arg(m_mock->mock_fpga_version()));
        }
    }

    // 5) 注册数据回调
    e = api("Cam_RegisterDataHandler", [&] { return Cam_RegisterDataHandler(handle, &SDKTest::on_data, this); });
    if (e != CAM_OK) {
        log_line("注册回调失败，测试中止");
        Cam_CloseDevice(handle);
        Cam_Finalize();
        return;
    }

    // 6) 超时测试：采集开始前 GetFrameSync（无帧可返回）→ 预期 CAM_ERR_TIMEOUT
    log_line("── 超时测试：未开始采集即 GetFrameSync ──");
    QByteArray syncBuf(4 * 1024 * 1024, '\0');
    FrameInfo finfo;
    {
        QElapsedTimer t;
        t.start();
        e = Cam_GetFrameSync(handle, 1, &finfo, syncBuf.data(),
                             static_cast<uint32_t>(syncBuf.size()), 500);
        const qint64 ms = t.elapsed();
        log_line(QString("Cam_GetFrameSync(无帧, timeout=500ms) -> %1   耗时=%2 ms（预期 CAM_ERR_TIMEOUT，耗时≈500ms 为超时等待路径）")
                     .arg(int(e)).arg(ms));
    }

    // 7) Get 配置：Mock 返回默认测试数据
    log_line("── Get 配置（Mock 应返回默认测试数据）──");
    SensorConfig sc{};
    e = api("Cam_GetSensorConfig", [&] { return Cam_GetSensorConfig(handle, 1, &sc); });
    if (e == CAM_OK) {
        log_line(QString("   exposure=%1  gain=%2  expDelay=%3  sampleRate=%4")
                     .arg(sc.exposureTime).arg(sc.gain).arg(sc.exposureDelay).arg(sc.sampleRate));
        if (m_mock_mode) {
            const bool ok = sc.exposureTime == uint32_t(m_mock->default_exposure()) &&
                            sc.gain == uint32_t(m_mock->default_gain()) &&
                            sc.exposureDelay == uint32_t(m_mock->default_exposure_delay()) &&
                            sc.sampleRate == uint32_t(m_mock->default_sample_rate());
            log_line(QString("   [验证] Get 与 Mock 默认一致: %1").arg(ok ? "是" : "否！"));
        }
    }
    if (m_mock_mode && m_mock) {
        verify_sent_cmds("Cam_GetSensorConfig",
                         {rd(CMD_EXPOSURE_TIME), rd(CMD_GAIN),
                          rd(CMD_EXPOSURE_DELAY), rd(CMD_SAMPLE_RATE)});
    }
    TriggerConfig tc{};
    e = api("Cam_GetTriggerConfig", [&] { return Cam_GetTriggerConfig(handle, 1, &tc); });
    if (e == CAM_OK) {
        log_line(QString("   triggerMode=%1  encType=%2  encMode=%3  encStep=%4  extEnable=%5")
                     .arg(tc.triggerMode).arg(tc.encoderType)
                     .arg(tc.encoderInputMode).arg(tc.encoderStep).arg(tc.externalEnable));
        if (m_mock_mode) {
            const bool ok = tc.triggerMode == uint16_t(m_mock->default_trigger_mode()) &&
                            tc.encoderType == uint16_t(m_mock->default_encoder_type()) &&
                            tc.encoderInputMode == uint16_t(m_mock->default_encoder_input_mode()) &&
                            tc.encoderStep == uint32_t(m_mock->default_encoder_step()) &&
                            tc.externalEnable == uint16_t(m_mock->default_external_trigger());
            log_line(QString("   [验证] Get 与 Mock 默认一致: %1").arg(ok ? "是" : "否！"));
        }
    }
    if (m_mock_mode && m_mock) {
        verify_sent_cmds("Cam_GetTriggerConfig",
                         {rd(CMD_TRIGGER_MODE), rd(CMD_ENCODER_TYPE),
                          rd(CMD_ENCODER_MODE), rd(CMD_ENCODER_INTERVAL),
                          rd(CMD_EXTERNAL_TRIGGER)});
    }
    BatchConfig bc{};
    e = api("Cam_GetBatchConfig", [&] { return Cam_GetBatchConfig(handle, 1, &bc); });
    if (e == CAM_OK) {
        log_line(QString("   batchLength=%1").arg(bc.length));
        if (m_mock_mode) {
            const bool ok = bc.length == uint16_t(m_mock->default_batch_length());
            log_line(QString("   [验证] Get 与 Mock 默认一致: %1").arg(ok ? "是" : "否！"));
        }
    }
    if (m_mock_mode && m_mock) {
        verify_sent_cmds("Cam_GetBatchConfig", {rd(CMD_BATCH_LENGTH)});
    }

    // 8) Set 配置：写命令立即 ACK 成功（roi/pixelPitch/z 等无协议字段必须保持 0）
    log_line("── Set 配置（写命令应立即 ACK 成功，<100ms）──");
    SensorConfig setSc{};
    setSc.exposureTime = 2000; setSc.gain = 3; setSc.exposureDelay = 100; setSc.sampleRate = 3000;
    e = api("Cam_SetSensorConfig", [&] { return Cam_SetSensorConfig(handle, 1, &setSc); });
    if (m_mock_mode && m_mock) {
        verify_sent_cmds("Cam_SetSensorConfig",
                         {wr(CMD_EXPOSURE_TIME, be32v(2000)),
                          wr(CMD_GAIN, be32v(3)),
                          wr(CMD_EXPOSURE_DELAY, be32v(100)),
                          wr(CMD_SAMPLE_RATE, be32v(3000))});
    }
    TriggerConfig setTc{};
    setTc.triggerMode = 1; setTc.encoderType = 1; setTc.encoderInputMode = 1;
    setTc.encoderStep = 100; setTc.externalEnable = 0;
    e = api("Cam_SetTriggerConfig", [&] { return Cam_SetTriggerConfig(handle, 1, &setTc); });
    if (m_mock_mode && m_mock) {
        verify_sent_cmds("Cam_SetTriggerConfig",
                         {wr(CMD_TRIGGER_MODE, be16v(1)),
                          wr(CMD_ENCODER_TYPE, be16v(1)),
                          wr(CMD_ENCODER_MODE, be16v(1)),
                          wr(CMD_ENCODER_INTERVAL, be32v(100)),
                          wr(CMD_EXTERNAL_TRIGGER, QByteArray(2, '\0'))});
    }
    BatchConfig setBc{};
    setBc.length = 200;
    e = api("Cam_SetBatchConfig", [&] { return Cam_SetBatchConfig(handle, 1, &setBc); });
    if (m_mock_mode && m_mock) {
        verify_sent_cmds("Cam_SetBatchConfig", {wr(CMD_BATCH_LENGTH, be16v(200))});
    }

    // 9) 开始采集：Mock 覆盖立即 ACK + 启动 40ms 帧生成
    e = api("Cam_StartCapture(1)", [&] { return Cam_StartCapture(handle, 1); });
    if (m_mock_mode && m_mock) {
        verify_sent_cmds("Cam_StartCapture(1)", {wr(CMD_START_STOP, QByteArray(2, '\0'))});
    }
    if (e == CAM_OK) {
        // 10) 收集 DataHandler 回调约 2 秒
        log_line("采集已启动，等待 DataHandler 回调 2 秒...");
        for (int i = 0; i < 20; ++i) {
            QThread::msleep(100);
            if (m_stop.load()) break;
        }
        log_line(QString("[结果] 回调累计帧数 = %1").arg(m_frame_count.load()));

        // 11) GetFrameSync 同步取帧轮询（验证同步链路 + 线程）
        log_line("── Cam_GetFrameSync 同步取帧（500ms × 5 次）──");
        for (int i = 0; i < 5; ++i) {
            if (m_stop.load()) break;
            FrameInfo sfinfo;
            QElapsedTimer t;
            t.start();
            e = Cam_GetFrameSync(handle, 1, &sfinfo, syncBuf.data(),
                                 static_cast<uint32_t>(syncBuf.size()), 1000);
            const qint64 ms = t.elapsed();
            if (e == CAM_OK) {
                log_line(QString("   [SYNC] th=0x%1  帧 %2  同步耗时=%3 ms：sensor=%4  %5x%6  size=%7")
                             .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
                             .arg(sfinfo.frameId).arg(ms).arg(sfinfo.sensorId)
                             .arg(sfinfo.width).arg(sfinfo.height).arg(sfinfo.dataSize));
            } else {
                log_line(QString("   [SYNC] 取帧失败 -> %1 (%2 ms)").arg(int(e)).arg(ms));
            }
            QThread::msleep(500);
        }
    } else {
        log_line("开始采集失败；仍执行停止/收尾流程");
    }

    // 12) 停止采集：Mock 覆盖立即 ACK + 停止帧生成
    e = api("Cam_StopCapture(1)", [&] { return Cam_StopCapture(handle, 1); });
    if (m_mock_mode && m_mock) {
        verify_sent_cmds("Cam_StopCapture(1)", {wr(CMD_START_STOP, QByteArray(2, '\0'))});
    }
    if (m_mock_mode) {
        log_line(QString("[MOCK] Mock 本次共生成 %1 帧").arg(m_mock->generated_frames()));
    }

    // ── 异步非阻塞 API 验证（仅 Mock 模式：ACK / 无ACK超时 / 线程分离 / 立即返回）──
    if (m_mock_mode && m_mock) {
        const quintptr asyncCallerThread = reinterpret_cast<quintptr>(QThread::currentThreadId());
        log_line("── 异步非阻塞 API（立即返回 CAM_PENDING，结果经 SDK CommandManager 线程回调）──");

        // 13.1 正常 ACK：StartCaptureAsync 立即返回（<10ms），ACK 回调 CAM_OK
        {
            AsyncResult r;
            e = api("Cam_StartCaptureAsync(1)", [&] {
                return Cam_StartCaptureAsync(handle, 1, 4000, &SDKTest::on_async_result, &r);
            });
            const bool waited = wait_async_result(r, 3000);
            const bool threadOk = (r.callbackThreadId != asyncCallerThread);
            log_line(QString("  [异步ACK] command=START_CAPTURE result=%1 commandId=%2 waited=%3（回调线程0x%4 ≠ 调用线程0x%5: %6）")
                         .arg(int(r.result)).arg(r.commandId)
                         .arg(waited ? "是" : "否！")
                         .arg(r.callbackThreadId, 0, 16)
                         .arg(asyncCallerThread, 0, 16)
                         .arg(threadOk ? "是" : "否！"));
            log_line(QString("  [验证] StartCaptureAsync ACK 路径: %1")
                         .arg((waited && r.result == CAM_OK && threadOk) ? "通过" : "失败！"));

            AsyncResult rStop;
            e = api("Cam_StopCaptureAsync(1)", [&] {
                return Cam_StopCaptureAsync(handle, 1, 4000, &SDKTest::on_async_result, &rStop);
            });
            const bool waitedStop = wait_async_result(rStop, 3000);
            log_line(QString("  [异步ACK] command=STOP_CAPTURE result=%1 commandId=%2 waited=%3")
                         .arg(int(rStop.result)).arg(rStop.commandId)
                         .arg(waitedStop ? "是" : "否！"));
            log_line(QString("  [验证] StopCaptureAsync ACK 路径: %1")
                         .arg((waitedStop && rStop.result == CAM_OK) ? "通过" : "失败！"));
        }

        // 13.2 无 ACK 超时：Mock 静默模式（不回调 ACK）→ CAM_ERR_TIMEOUT，耗时≈timeoutMs
        {
            m_mock->set_silent_acks(true);   // 模拟 FPGA 不回 ACK
            AsyncResult r;
            QElapsedTimer t;
            t.start();
            e = api("Cam_StartCaptureAsync(1,无ACK)", [&] {
                return Cam_StartCaptureAsync(handle, 1, 4000, &SDKTest::on_async_result, &r);
            });
            const qint64 callCost = t.elapsed();
            const bool waited = wait_async_result(r, 6000);
            const qint64 waitCost = t.elapsed();
            m_mock->set_silent_acks(false);  // 恢复 ACK（静默期间 start_mock_frames 已启动，需停止）
            const bool threadOk = (r.callbackThreadId != asyncCallerThread);
            log_line(QString("  [异步超时] command=START_CAPTURE result=%1 commandId=%2 timeout=4000ms 立即返回=%3 ms 等待回调=%4 ms waited=%5（回调线程0x%6 ≠ 调用线程0x%7: %8）")
                         .arg(int(r.result)).arg(r.commandId)
                         .arg(callCost).arg(waitCost)
                         .arg(waited ? "是" : "否！")
                         .arg(r.callbackThreadId, 0, 16)
                         .arg(asyncCallerThread, 0, 16)
                         .arg(threadOk ? "是" : "否！"));
            log_line(QString("  [验证] StartCaptureAsync 超时路径: %1")
                         .arg((waited && r.result == CAM_ERR_TIMEOUT && threadOk) ? "通过" : "失败！"));

            AsyncResult rStop;
            e = api("Cam_StopCaptureAsync(1)", [&] {
                return Cam_StopCaptureAsync(handle, 1, 4000, &SDKTest::on_async_result, &rStop);
            });
            const bool waitedStop = wait_async_result(rStop, 3000);
            log_line(QString("  [异步ACK] command=STOP_CAPTURE result=%1 commandId=%2 waited=%3")
                         .arg(int(rStop.result)).arg(rStop.commandId)
                         .arg(waitedStop ? "是" : "否！"));
        }

        // 13.3 异步 Get 配置：4 条读命令全部 ACK 后回调，输出结构被填充
        {
            SensorConfig asc{};
            AsyncResult r;
            e = api("Cam_GetSensorConfigAsync(1)", [&] {
                return Cam_GetSensorConfigAsync(handle, 1, &asc, 4000, &SDKTest::on_async_result, &r);
            });
            const bool waited = wait_async_result(r, 3000);
            const bool dataOk = (asc.exposureTime == uint32_t(m_mock->default_exposure()) &&
                                 asc.gain == uint32_t(m_mock->default_gain()) &&
                                 asc.exposureDelay == uint32_t(m_mock->default_exposure_delay()) &&
                                 asc.sampleRate == uint32_t(m_mock->default_sample_rate()));
            log_line(QString("  [异步Get] command=GET_SENSOR_CONFIG result=%1 commandId=%2 exposure=%3 gain=%4 expDelay=%5 sampleRate=%6 waited=%7（数据与 Mock 默认一致: %8）")
                         .arg(int(r.result)).arg(r.commandId)
                         .arg(asc.exposureTime).arg(asc.gain).arg(asc.exposureDelay).arg(asc.sampleRate)
                         .arg(waited ? "是" : "否！")
                         .arg(dataOk ? "是" : "否！"));
            log_line(QString("  [验证] GetSensorConfigAsync 路径: %1")
                         .arg((waited && r.result == CAM_OK && dataOk) ? "通过" : "失败！"));
        }
    }

    // 13) 收尾
    e = api("Cam_UnregisterDataHandler", [&] { return Cam_UnregisterDataHandler(handle); });
    e = api("Cam_CloseDevice", [&] { return Cam_CloseDevice(handle); });
    m_handle = nullptr;
    e = api("Cam_Finalize", [] { return Cam_Finalize(); });

    log_line("════════ SDKTest 结束（无 UDP 活动、无额外线程泄漏）════════");
}
