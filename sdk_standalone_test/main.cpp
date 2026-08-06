// sdk_standalone_test/main.cpp
//
// 独立 SDK 模式验证程序：只使用 PhotonCameraSDK.dll 的公开 C API（photon_camera.h），
// 不 include 任何 SDK 内部头文件、不调用 Cam_AttachProtocolHandler（无宿主注入）。
// 纯 C++17，不链接 Qt（Qt 运行时由 DLL 内部自带）。
//
// 用法:
//   sdk_standalone_test --mock                         # DLL 内置 Mock 设备，无 FPGA
//   sdk_standalone_test [ip] [ctrlPort] [dataPort]     # 真实 FPGA（默认 192.168.10.10 5000 60000）
//
// 覆盖流程（对应独立 SDK 模式验收）:
//   Initialize → OpenNetworkDevice(FPGA_IP, 5000, 60000)
//   → RegisterDataHandler → StartCaptureAsync → 等待 ACK(命令ID)
//   → 接收帧(frameId/sensorId/width/height/size) → StopCaptureAsync
//   → CloseDevice → Finalize
// Mock 模式额外验证:
//   - 未实现协议(ROI/标定)返回 CAM_ERR_NOT_SUPPORTED，且 mock 已发送命令计数不变
//     （即不发送任何 UDP 命令、不伪造 FPGA 回复）。

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

// ── 数据回调上下文（在 SDK CallbackWorker 线程执行，绝不运行在 UDP/调用线程）──
static std::atomic<int> g_frameCount{0};
static std::mutex g_frameMutex;
static FrameInfo g_lastFrame{};

static void CAM_CALL on_data(const FrameInfo* info, const void* /*pData*/, void* /*pUser*/) {
    if (!info) return;
    std::lock_guard<std::mutex> lock(g_frameMutex);
    g_lastFrame = *info;
    g_frameCount.fetch_add(1);
}

// ── 异步 ACK 回调上下文（在 SDK CommandManager 线程执行）──
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
                detail.empty() ? "" : " ", detail.c_str());
    if (!ok) g_failCount++;
}

int main(int argc, char** argv) {
    bool mock = false;
    std::string ip = "192.168.10.10";
    int controlPort = 5000;
    int dataPort = 60000;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--mock") == 0) {
            mock = true;
        } else if (i + 2 < argc) {
            ip = argv[i];
            controlPort = std::atoi(argv[i + 1]);
            dataPort = std::atoi(argv[i + 2]);
            i += 2;
        }
    }

    std::printf("=== sdk_standalone_test (mock=%d) ip=%s ctrl=%d data=%d ===\n",
                mock ? 1 : 0, ip.c_str(), controlPort, dataPort);

    check("Initialize", Cam_Initialize() == CAM_OK);
    std::printf("SDK version: %s\n", Cam_GetSdkVersion());

    // ── 打开独立设备（SDK 内部自建唯一一套通信）──
    CameraHandle h = nullptr;
    CamError e = mock
        ? Cam_OpenNetworkDeviceMock(ip.c_str(), static_cast<uint16_t>(controlPort),
                                    static_cast<uint16_t>(dataPort), &h)
        : Cam_OpenNetworkDevice(ip.c_str(), static_cast<uint16_t>(controlPort),
                                static_cast<uint16_t>(dataPort), &h);
    check("OpenNetworkDevice", e == CAM_OK && h != nullptr,
          "-> err=" + std::to_string(e));

    check("RegisterDataHandler", Cam_RegisterDataHandler(h, on_data, nullptr) == CAM_OK);

    // ── StartCaptureAsync：立即返回 CAM_PENDING，ACK 经回调通知 ──
    AsyncResult ack;
    e = Cam_StartCaptureAsync(h, 1, 4000, on_async, &ack);
    check("StartCaptureAsync returns CAM_PENDING", e == CAM_PENDING,
          "-> ret=" + std::to_string(e));

    bool gotAck = wait_async(ack, 6000);
    check("StartCaptureAsync ACK received", gotAck);
    if (gotAck) {
        std::printf("  ACK: result=%d commandId=%u\n", ack.result, ack.commandId);
        check("StartCaptureAsync ACK result=CAM_OK", ack.result == CAM_OK);
        check("StartCaptureAsync commandId != 0", ack.commandId != 0);
    }

    // ── 收帧（DataHandler 回调）──
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
        std::lock_guard<std::mutex> lock(g_frameMutex);
        std::printf("  frames received=%d, last: frameId=%u sensorId=%u width=%u height=%u size=%u\n",
                    g_frameCount.load(), g_lastFrame.frameId, g_lastFrame.sensorId,
                    g_lastFrame.width, g_lastFrame.height, g_lastFrame.dataSize);
    }
    if (mock) {
        check("frames received > 0", g_frameCount.load() > 0);
        std::lock_guard<std::mutex> lock(g_frameMutex);
        check("frame sensorId=1", g_lastFrame.sensorId == 1);
        check("frame width=1280", g_lastFrame.width == 1280);
        check("frame height=1024", g_lastFrame.height == 1024);
        check("frame dataSize=1310720", g_lastFrame.dataSize == 1280u * 1024u);
    } else {
        std::printf("  [real] frames received=%d（真实模式需 FPGA 才有帧）\n", g_frameCount.load());
    }

    // ── Mock 模式：未实现协议 → NOT_SUPPORTED 且不发送任何 UDP 命令 ──
    if (mock) {
        uint32_t before = 0;
        check("GetMockSentCommandCount(initial)", Cam_GetMockSentCommandCount(h, &before) == CAM_OK);
        std::printf("  mock sent-cmd count before NOT_SUPPORTED call: %u\n", before);

        SensorConfig sc{};
        sc.roiWidth = 16;   // ROI 未实现
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
    }

    // ── StopCaptureAsync ──
    AsyncResult stop;
    e = Cam_StopCaptureAsync(h, 1, 4000, on_async, &stop);
    check("StopCaptureAsync returns CAM_PENDING", e == CAM_PENDING,
          "-> ret=" + std::to_string(e));
    if (wait_async(stop, 6000)) {
        check("StopCaptureAsync ACK result=CAM_OK", stop.result == CAM_OK);
    } else {
        check("StopCaptureAsync ACK received", false);
    }

    check("CloseDevice", Cam_CloseDevice(h) == CAM_OK);
    check("Finalize", Cam_Finalize() == CAM_OK);

    std::printf("=== sdk_standalone_test result: %s ===\n",
                g_failCount == 0 ? "PASS" : "FAIL");
    return g_failCount == 0 ? 0 : 1;
}
