"""CameraSDK Python 非 GUI 快速测试"""
import sys, os, time, threading
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from camera_sdk import CameraDevice, CamError, PixelFormat, err_name

print("=== Python CameraSDK DLL 测试 ===")
print()

# 1. 初始化
err = CameraDevice.initialize()
print(f"Initialize: {err.name}")

# 2. 打开 Mock 设备
dev = CameraDevice()
err = dev.open_mock()
print(f"OpenMock: {err.name}")
print(f"SDK 版本: {dev.get_version()}")

# 3. 注册数据回调
err = dev.register_data_handler()
print(f"RegisterDataHandler: {err.name}")

# 4. 异步开始采集
frame_count = [0]

def on_start_result(err, cmd_id):
    print(f"  StartCapture ACK: {err.name} cmdId={cmd_id}")

err = dev.start_capture_async(1, 3000, on_start_result)
print(f"StartCaptureAsync: {err.name}")

# 5. 等待帧到达
print("等待帧数据...")
time.sleep(2.0)

# 6. 轮询帧
for _ in range(10):
    frame = dev.poll_frame(timeout=0.1)
    if frame:
        frame_count[0] += 1
        print(f"  帧 #{frame['frameId']}: sensor={frame['sensorId']} "
              f"{frame['width']}x{frame['height']} "
              f"type={frame['dataType'].name} size={frame['dataSize']}")
    # check cmd results
    cmd = dev.poll_cmd_result(timeout=0)
    if cmd:
        print(f"  CMD: {cmd[0].name}")

print(f"共收到 {frame_count[0]} 帧")

# 7. 异步停止采集
err = dev.stop_capture_async(1, 3000)
print(f"StopCaptureAsync: {err.name}")
time.sleep(0.5)
cmd = dev.poll_cmd_result(timeout=0)
if cmd:
    print(f"  StopCapture ACK: {cmd[0].name}")

# 8. 异步取单帧测试
print()
print("--- 异步取单帧测试 ---")
dev.clear_frames()

def on_frame_result(err, frame):
    if err == CamError.OK and frame:
        print(f"  AsyncFrame OK: id={frame['frameId']} type={frame['dataType'].name} size={frame['dataSize']}")
    else:
        print(f"  AsyncFrame: {err.name}")

err = dev.get_image_async(1, 3000, on_frame_result)
print(f"GetImageAsync: {err.name}")
time.sleep(1.0)
af = dev.poll_async_frame_result(timeout=0.5)
if af:
    print(f"  Polled: {af[0].name}")

# 9. 批处理测试
print()
print("--- 批处理测试 ---")
err = dev.set_batch_config(1, 5)
print(f"SetBatchConfig length=5: {err.name}")

err2, length = dev.get_batch_config(1)
print(f"GetBatchConfig: {err2.name} length={length}")

err = dev.receive_batch_data(1, 5000)
print(f"ReceiveBatchData: {err.name}")
count = dev.get_batch_point_count(1)
width = dev.get_batch_data_width(1)
print(f"BatchPointCount={count} DataWidth={width}")
err, data = dev.get_batch_data(1)
print(f"GetBatchData: {err.name} size={len(data) if data else 0}")

# 10. 清理
dev.unregister_data_handler()
dev.close()
CameraDevice.finalize()
print()
print("=== 测试完成 ===")