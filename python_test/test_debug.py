"""Debug test for camera_sdk wrapper"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
print("Importing camera_sdk...")
from camera_sdk import CameraDevice, CamError, PixelFormat, err_name
print("OK")

print("Initialize...")
err = CameraDevice.initialize()
print(f"Initialize: {err.name}")

print("OpenMock...")
dev = CameraDevice()
err = dev.open_mock()
print(f"OpenMock: {err.name}")

print("RegisterDataHandler...")
err = dev.register_data_handler()
print(f"RegisterDataHandler: {err.name}")

print("StartCaptureAsync...")
err = dev.start_capture_async(1, 3000)
print(f"StartCaptureAsync: {err.name}")

time.sleep(1)

print("Polling frames...")
for _ in range(3):
    f = dev.poll_frame(timeout=0.2)
    if f:
        print(f"  Frame: id={f['frameId']} sensor={f['sensorId']}")

print("StopCaptureAsync...")
err = dev.stop_capture_async(1, 3000)
print(f"StopCaptureAsync: {err.name}")

time.sleep(0.5)
cmd = dev.poll_cmd_result(timeout=0)
if cmd:
    print(f"  CMD: {cmd[0].name}")

print("Close...")
dev.unregister_data_handler()
dev.close()
CameraDevice.finalize()
print("DONE")