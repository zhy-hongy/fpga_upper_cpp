// sdk_standalone_test/main.cpp
//
// 独立 SDK 模式验证程序：只使用 PhotonCameraSDK.dll 的公开 C API（photon_camera.h），
// 不 include 任何 SDK 内部头文件、不调用 Cam_AttachProtocolHandler（无宿主注入）。
// 纯 C++17，不链接 Qt（Qt 运行时由 DLL 内部自带）。
//
// 用法:
//   sdk_standalone_test --mock                         # DLL 内置 Mock 设备，无网络
//   sdk_standalone_test --udp                          # 测试 UDP 通讯（需先启动 udp_simulator.py）
//   sdk_standalone_test --udp 127.0.0.1 5000 60000     # 自定义 IP 和端口
//   sdk_standalone_test [ip] [ctrlPort] [dataPort]     # 真实 FPGA

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "photon_camera.h"

// ── 数据回调上下文 ──
static std::atomic<int> g_frameCount{0};
static std::mutex g_frameMutex;
static FrameInfo g_lastFrame{};

static void CAM_CALL on_data(const FrameInfo* info, const void* /*pData*/, void* /*pUser*/) {
    if (!info) return;
    std::lock_guard<std::mutex> lock(g_frameMutex);
    g_lastFrame = *info;
    g_frameCount.fetch_add(1);
}

// ── 异步 ACK 回调上下文 ──
struct AsyncResult {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    CamError result = CAM_ERR_UNKNOWN;
    uint16_t commandId = 0;
};

static void CAM_CALL on_async(CamError result, uint16_t commandId, void* userData) {
    auto* r = static_cast<AsyncResult*>(userData);
    std::lock_guard<std::mutex> lock(r->mutex);
    r->result = result;
    r->commandId = commandId;
    r->done = true;
    r->cv.notify_all();
}

static bool wait_async(AsyncResult& r, int timeoutMs) {
    std::unique_lock<std::mutex> lock(r.mutex);
    return r.cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return r.done; });
}

static int g_failCount = 0;
static void check(const char* name, bool ok, const std::string& detail = "") {
    std::printf("[%s] %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.empty() ? "" : " - ", detail.c_str());
    if (!ok) g_failCount++;
}

// ── UDP 模式：测试所有 SDK API ──────────────────────────────────────────

static void run_udp_tests(CameraHandle h) {
    std::printf("\n--- UDP 通讯测试：同步 API ---\n\n");

    // ── 基础信息 ──
    {
        DeviceRunState state = STATE_IDLE;
        CamError rs = Cam_GetRunState(h, &state);
        check("GetRunState", rs == CAM_OK && state == STATE_READY,
              "state=" + std::to_string(state));

        uint32_t sensorCount = 0;
        CamError cs = Cam_GetSensorCount(h, &sensorCount);
        check("GetSensorCount", cs == CAM_OK && sensorCount == 4,
              "count=" + std::to_string(sensorCount));

        DeviceInformation devInfo{};
        CamError di = Cam_GetDeviceInformation(h, &devInfo);
        check("GetDeviceInformation", di == CAM_OK,
              "status=" + std::to_string(devInfo.status));
    }

    // ── Sensor 配置：写后读验证 ──
    {
        SensorConfig writeCfg{};
        writeCfg.exposureTime  = 5000;
        writeCfg.gain          = 320;
        writeCfg.exposureDelay = 20;
        writeCfg.sampleRate    = 150;
        // ROI 和物理字段置零（不支持）

        CamError e = Cam_SetSensorConfig(h, 1, &writeCfg);
        check("SetSensorConfig (sensor=1)", e == CAM_OK,
              "exposure=5000 gain=320 delay=20 rate=150 -> " + std::to_string(e));

        SensorConfig readCfg{};
        e = Cam_GetSensorConfig(h, 1, &readCfg);
        check("GetSensorConfig (sensor=1)", e == CAM_OK, "-> " + std::to_string(e));
        check("  exposureTime=5000", readCfg.exposureTime == 5000,
              "got " + std::to_string(readCfg.exposureTime));
        check("  gain=320", readCfg.gain == 320,
              "got " + std::to_string(readCfg.gain));
        check("  exposureDelay=20", readCfg.exposureDelay == 20,
              "got " + std::to_string(readCfg.exposureDelay));
        check("  sampleRate=150", readCfg.sampleRate == 150,
              "got " + std::to_string(readCfg.sampleRate));
    }

    // ── 触发配置：写后读验证 ──
    {
        TriggerConfig writeCfg{};
        writeCfg.triggerMode     = 2;     // 编码器
        writeCfg.encoderType     = 1;     // 差分
        writeCfg.encoderInputMode = 2;    // 2倍频
        writeCfg.encoderStep     = 500;
        writeCfg.externalEnable  = 1;     // 开启

        CamError e = Cam_SetTriggerConfig(h, 1, &writeCfg);
        check("SetTriggerConfig (sensor=1)", e == CAM_OK,
              "mode=2 enc=1 encMode=2 step=500 ext=1 -> " + std::to_string(e));

        TriggerConfig readCfg{};
        e = Cam_GetTriggerConfig(h, 1, &readCfg);
        check("GetTriggerConfig (sensor=1)", e == CAM_OK, "-> " + std::to_string(e));
        check("  triggerMode=2", readCfg.triggerMode == 2,
              "got " + std::to_string(readCfg.triggerMode));
        check("  encoderType=1", readCfg.encoderType == 1,
              "got " + std::to_string(readCfg.encoderType));
        check("  encoderInputMode=2", readCfg.encoderInputMode == 2,
              "got " + std::to_string(readCfg.encoderInputMode));
        check("  encoderStep=500", readCfg.encoderStep == 500,
              "got " + std::to_string(readCfg.encoderStep));
        check("  externalEnable=1", readCfg.externalEnable == 1,
              "got " + std::to_string(readCfg.externalEnable));
    }

    // ── 批处理配置：写后读验证 ──
    {
        BatchConfig writeCfg{};
        writeCfg.length = 32;

        CamError e = Cam_SetBatchConfig(h, 1, &writeCfg);
        check("SetBatchConfig (sensor=1)", e == CAM_OK,
              "length=32 -> " + std::to_string(e));

        BatchConfig readCfg{};
        e = Cam_GetBatchConfig(h, 1, &readCfg);
        check("GetBatchConfig (sensor=1)", e == CAM_OK, "-> " + std::to_string(e));
        check("  length=32", readCfg.length == 32,
              "got " + std::to_string(readCfg.length));
    }

    // ── 采集控制 ──
    {
        CamError e = Cam_StartCapture(h, 1);
        check("StartCapture (sensor=1)", e == CAM_OK, "-> " + std::to_string(e));

        e = Cam_StopCapture(h, 1);
        check("StopCapture (sensor=1)", e == CAM_OK, "-> " + std::to_string(e));

        // 广播模式 (sensorId=0)
        e = Cam_StartCapture(h, 0);
        check("StartCapture (broadcast)", e == CAM_OK, "-> " + std::to_string(e));

        e = Cam_StopCapture(h, 0);
        check("StopCapture (broadcast)", e == CAM_OK, "-> " + std::to_string(e));
    }

    // ── 批处理控制 ──
    {
        CamError e = Cam_StartBatch(h, 1, 42);
        check("StartBatch (sensor=1, batchId=42)", e == CAM_OK, "-> " + std::to_string(e));

        e = Cam_StopBatch(h, 1);
        check("StopBatch (sensor=1)", e == CAM_OK, "-> " + std::to_string(e));
    }

    // ── 复位缓冲区 ──
    {
        CamError e = Cam_ResetBuffer(h, 1);
        check("ResetBuffer (sensor=1)", e == CAM_OK, "-> " + std::to_string(e));
    }

    // ── 不支持的功能 ──
    {
        SensorInformation info{};
        CamError e = Cam_GetSensorInformation(h, 1, &info);
        check("GetSensorInformation -> NOT_SUPPORTED", e == CAM_ERR_NOT_SUPPORTED,
              "-> " + std::to_string(e));

        CalibrationData cal{};
        e = Cam_WriteCalibration(h, 1, &cal);
        check("WriteCalibration -> NOT_SUPPORTED", e == CAM_ERR_NOT_SUPPORTED,
              "-> " + std::to_string(e));

        e = Cam_ReadCalibration(h, 1, &cal);
        check("ReadCalibration -> NOT_SUPPORTED", e == CAM_ERR_NOT_SUPPORTED,
              "-> " + std::to_string(e));

        SensorConfig badCfg{};
        badCfg.roiWidth = 16;
        e = Cam_SetSensorConfig(h, 1, &badCfg);
        check("SetSensorConfig(roiWidth!=0) -> NOT_SUPPORTED", e == CAM_ERR_NOT_SUPPORTED,
              "-> " + std::to_string(e));
    }

    // ── 参数校验 ──
    {
        CamError e = Cam_SetSensorConfig(h, 1, nullptr);
        check("SetSensorConfig(null) -> INVALID_PARAM", e == CAM_ERR_INVALID_PARAM,
              "-> " + std::to_string(e));

        e = Cam_GetSensorConfig(h, 1, nullptr);
        check("GetSensorConfig(null) -> INVALID_PARAM", e == CAM_ERR_INVALID_PARAM,
              "-> " + std::to_string(e));

        e = Cam_GetFrameSync(h, 1, nullptr, nullptr, 0, 100);
        check("GetFrameSync(null) -> INVALID_PARAM", e == CAM_ERR_INVALID_PARAM,
              "-> " + std::to_string(e));
    }

    // ── 异步 API 测试 ──
    std::printf("\n--- UDP 通讯测试：异步 API ---\n\n");

    {
        AsyncResult ar;
        CamError e = Cam_SetSensorConfigAsync(h, 1, nullptr, 3000, on_async, &ar);
        check("Async SetSensorConfig(null) -> INVALID_PARAM", e == CAM_ERR_INVALID_PARAM,
              "-> " + std::to_string(e));
    }

    {
        AsyncResult ar;
        SensorConfig cfg{};
        cfg.exposureTime = 2000;
        cfg.gain         = 100;
        cfg.sampleRate   = 60;

        CamError e = Cam_SetSensorConfigAsync(h, 1, &cfg, 3000, on_async, &ar);
        check("Async SetSensorConfig returns CAM_PENDING", e == CAM_PENDING,
              "-> " + std::to_string(e));

        bool got = wait_async(ar, 5000);
        check("Async ACK received", got);
        check("Async result=CAM_OK", ar.result == CAM_OK,
              "result=" + std::to_string(ar.result));
    }

    {
        AsyncResult ar;
        SensorConfig cfg{};
        CamError e = Cam_GetSensorConfigAsync(h, 1, &cfg, 3000, on_async, &ar);
        check("Async GetSensorConfig returns CAM_PENDING", e == CAM_PENDING,
              "-> " + std::to_string(e));

        bool got = wait_async(ar, 5000);
        check("Async GetSensorConfig ACK received", got);
        check("Async GetSensorConfig result=CAM_OK", ar.result == CAM_OK,
              "result=" + std::to_string(ar.result));
        check("  exposureTime=2000", cfg.exposureTime == 2000,
              "got " + std::to_string(cfg.exposureTime));
        check("  gain=100", cfg.gain == 100,
              "got " + std::to_string(cfg.gain));
        check("  sampleRate=60", cfg.sampleRate == 60,
              "got " + std::to_string(cfg.sampleRate));
    }

    {
        AsyncResult ar;
        CamError e = Cam_StartCaptureAsync(h, 1, 3000, on_async, &ar);
        check("Async StartCapture returns CAM_PENDING", e == CAM_PENDING,
              "-> " + std::to_string(e));
        bool got = wait_async(ar, 5000);
        check("Async StartCapture ACK received", got);
        check("Async StartCapture result=CAM_OK", ar.result == CAM_OK,
              "result=" + std::to_string(ar.result));
    }

    {
        AsyncResult ar;
        CamError e = Cam_StopCaptureAsync(h, 1, 3000, on_async, &ar);
        check("Async StopCapture returns CAM_PENDING", e == CAM_PENDING,
              "-> " + std::to_string(e));
        bool got = wait_async(ar, 5000);
        check("Async StopCapture ACK received", got);
        check("Async StopCapture result=CAM_OK", ar.result == CAM_OK,
              "result=" + std::to_string(ar.result));
    }
}

// ── Mock 模式测试 ────────────────────────────────────────────────────────

static void run_mock_tests(CameraHandle h) {
    std::printf("\n--- Mock 模式测试 ---\n\n");

    // StartCaptureAsync
    AsyncResult ack;
    CamError e = Cam_StartCaptureAsync(h, 1, 4000, on_async, &ack);
    check("StartCaptureAsync returns CAM_PENDING", e == CAM_PENDING,
          "-> ret=" + std::to_string(e));

    bool gotAck = wait_async(ack, 6000);
    check("StartCaptureAsync ACK received", gotAck);
    if (gotAck) {
        std::printf("  ACK: result=%d commandId=%u\n", ack.result, ack.commandId);
        check("StartCaptureAsync ACK result=CAM_OK", ack.result == CAM_OK);
        check("StartCaptureAsync commandId != 0", ack.commandId != 0);
    }

    // 收帧
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
        std::lock_guard<std::mutex> lock(g_frameMutex);
        std::printf("  frames received=%d, last: frameId=%u sensorId=%u width=%u height=%u size=%u\n",
                    g_frameCount.load(), g_lastFrame.frameId, g_lastFrame.sensorId,
                    g_lastFrame.width, g_lastFrame.height, g_lastFrame.dataSize);
    }
    check("frames received > 0", g_frameCount.load() > 0);
    {
        std::lock_guard<std::mutex> lock(g_frameMutex);
        check("frame sensorId=1", g_lastFrame.sensorId == 1);
        check("frame width=1280", g_lastFrame.width == 1280);
        check("frame height=1024", g_lastFrame.height == 1024);
        check("frame dataSize=1310720", g_lastFrame.dataSize == 1280u * 1024u);
    }

    // 未实现协议 → NOT_SUPPORTED 且不发送任何 UDP 命令
    uint32_t before = 0;
    check("GetMockSentCommandCount(initial)", Cam_GetMockSentCommandCount(h, &before) == CAM_OK);
    std::printf("  mock sent-cmd count before NOT_SUPPORTED call: %u\n", before);

    SensorConfig sc{};
    sc.roiWidth = 16;
    CamError ns = Cam_SetSensorConfig(h, 1, &sc);
    check("SetSensorConfig(roiWidth!=0) -> NOT_SUPPORTED", ns == CAM_ERR_NOT_SUPPORTED,
          "-> err=" + std::to_string(ns));

    CalibrationData cal{};
    CamError calErr = Cam_ReadCalibration(h, 1, &cal);
    check("ReadCalibration -> NOT_SUPPORTED", calErr == CAM_ERR_NOT_SUPPORTED,
          "-> err=" + std::to_string(calErr));

    uint32_t after = 0;
    check("GetMockSentCommandCount(after)", Cam_GetMockSentCommandCount(h, &after) == CAM_OK);
    check("NOT_SUPPORTED 未发送任何 UDP 命令 (count 不变)", after == before,
          "before=" + std::to_string(before) + " after=" + std::to_string(after));

    // StopCaptureAsync
    AsyncResult stop;
    e = Cam_StopCaptureAsync(h, 1, 4000, on_async, &stop);
    check("StopCaptureAsync returns CAM_PENDING", e == CAM_PENDING,
          "-> ret=" + std::to_string(e));
    if (wait_async(stop, 6000)) {
        check("StopCaptureAsync ACK result=CAM_OK", stop.result == CAM_OK);
    } else {
        check("StopCaptureAsync ACK received", false);
    }
}

// ── main ─────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    bool mock = false;
    bool udp  = true;
    std::string ip = "127.0.0.1";
    int controlPort = 5000;
    int dataPort = 60000;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--mock") == 0) {
            mock = true;
        } else if (std::strcmp(argv[i], "--udp") == 0) {
            udp = true;
            ip = "127.0.0.1";  // 默认 localhost
            // 如果后面跟了 ip controlPort dataPort
            if (i + 3 < argc) {
                ip = argv[i + 1];
                controlPort = std::atoi(argv[i + 2]);
                dataPort = std::atoi(argv[i + 3]);
                i += 3;
            }
        } else if (i + 2 < argc) {
            ip = argv[i];
            controlPort = std::atoi(argv[i + 1]);
            dataPort = std::atoi(argv[i + 2]);
            i += 2;
        }
    }

    const char* mode = mock ? "mock" : (udp ? "udp" : "real");
    std::printf("=== sdk_standalone_test (%s) ip=%s ctrl=%d data=%d ===\n",
                mode, ip.c_str(), controlPort, dataPort);

    // ── 初始化 ──
    check("Initialize", Cam_Initialize() == CAM_OK);
    std::printf("SDK version: %s\n", Cam_GetSdkVersion());

    // ── 打开设备 ──
    CameraHandle h = nullptr;
    CamError e;
    if (mock) {
        e = Cam_OpenNetworkDeviceMock(ip.c_str(), static_cast<uint16_t>(controlPort),
                                      static_cast<uint16_t>(dataPort), &h);
    } else {
        e = Cam_OpenNetworkDevice(ip.c_str(), static_cast<uint16_t>(controlPort),
                                  static_cast<uint16_t>(dataPort), &h);
    }
    check("OpenNetworkDevice", e == CAM_OK && h != nullptr,
          "-> err=" + std::to_string(e));

    // ── 注册数据回调 ──
    check("RegisterDataHandler", Cam_RegisterDataHandler(h, on_data, nullptr) == CAM_OK);

    // ── 按模式运行测试 ──
    if (mock) {
        run_mock_tests(h);
    } else if (udp) {
        run_udp_tests(h);
    } else {
        // 真实模式：简单验证
        std::printf("\n--- 真实模式：基本验证 ---\n\n");
        AsyncResult ack;
        e = Cam_StartCaptureAsync(h, 1, 4000, on_async, &ack);
        check("StartCaptureAsync returns CAM_PENDING", e == CAM_PENDING);
        if (wait_async(ack, 6000)) {
            check("StartCaptureAsync result=CAM_OK", ack.result == CAM_OK);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::printf("  frames received=%d\n", g_frameCount.load());

        AsyncResult stop;
        Cam_StopCaptureAsync(h, 1, 4000, on_async, &stop);
        wait_async(stop, 6000);
    }

    // ── 清理 ──
    check("CloseDevice", Cam_CloseDevice(h) == CAM_OK);
    check("Finalize", Cam_Finalize() == CAM_OK);

    // ── 结果 ──
    std::printf("\n=== sdk_standalone_test result: %s (%d failures) ===\n",
                g_failCount == 0 ? "PASS" : "FAIL", g_failCount);
    return g_failCount == 0 ? 0 : 1;
}