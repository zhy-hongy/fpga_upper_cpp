#include "sdk_test.h"

#include <functional>

#include <QByteArray>
#include <QElapsedTimer>

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
    BatchConfig bc{};
    e = api("Cam_GetBatchConfig", [&] { return Cam_GetBatchConfig(handle, 1, &bc); });
    if (e == CAM_OK) {
        log_line(QString("   batchLength=%1").arg(bc.length));
        if (m_mock_mode) {
            const bool ok = bc.length == uint16_t(m_mock->default_batch_length());
            log_line(QString("   [验证] Get 与 Mock 默认一致: %1").arg(ok ? "是" : "否！"));
        }
    }

    // 8) Set 配置：写命令立即 ACK 成功（roi/pixelPitch/z 等无协议字段必须保持 0）
    log_line("── Set 配置（写命令应立即 ACK 成功，<100ms）──");
    SensorConfig setSc{};
    setSc.exposureTime = 2000; setSc.gain = 3; setSc.exposureDelay = 100; setSc.sampleRate = 3000;
    e = api("Cam_SetSensorConfig", [&] { return Cam_SetSensorConfig(handle, 1, &setSc); });
    TriggerConfig setTc{};
    setTc.triggerMode = 1; setTc.encoderType = 1; setTc.encoderInputMode = 1;
    setTc.encoderStep = 100; setTc.externalEnable = 0;
    e = api("Cam_SetTriggerConfig", [&] { return Cam_SetTriggerConfig(handle, 1, &setTc); });
    BatchConfig setBc{};
    setBc.length = 200;
    e = api("Cam_SetBatchConfig", [&] { return Cam_SetBatchConfig(handle, 1, &setBc); });

    // 9) 开始采集：Mock 覆盖立即 ACK + 启动 40ms 帧生成
    e = api("Cam_StartCapture(1)", [&] { return Cam_StartCapture(handle, 1); });
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
    if (m_mock_mode) {
        log_line(QString("[MOCK] Mock 本次共生成 %1 帧").arg(m_mock->generated_frames()));
    }

    // 13) 收尾
    e = api("Cam_UnregisterDataHandler", [&] { return Cam_UnregisterDataHandler(handle); });
    e = api("Cam_CloseDevice", [&] { return Cam_CloseDevice(handle); });
    m_handle = nullptr;
    e = api("Cam_Finalize", [] { return Cam_Finalize(); });

    log_line("════════ SDKTest 结束（无 UDP 活动、无额外线程泄漏）════════");
}
