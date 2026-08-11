"""
CameraSDK Python 封装 — 基于 ctypes 调用 CameraSDK.dll。
支持 Mock 模式（无 FPGA 硬件）和真实设备模式。
"""

import ctypes
from ctypes import (
    c_void_p, c_char_p, c_int32, c_uint8, c_uint16, c_uint32, c_uint64,
    c_float, c_bool, POINTER, Structure, byref, cast, sizeof, WINFUNCTYPE,
    windll
)
c_char = ctypes.c_char
import os
import threading
import queue
from enum import IntEnum

# =========================================================================
# DLL 加载
# =========================================================================
_DLL_DIR = os.path.dirname(os.path.abspath(__file__))
# 从 build/sdk/Release 加载（连同所有依赖 DLL）
_DLL_PATH = os.path.normpath(os.path.join(_DLL_DIR, "..", "build", "sdk", "Release", "CameraSDK.dll"))
# 把 DLL 所在目录加入 PATH，确保依赖 DLL 能被找到
os.environ["PATH"] = os.path.dirname(_DLL_PATH) + os.pathsep + os.environ.get("PATH", "")

_cam = ctypes.WinDLL(_DLL_PATH)

# =========================================================================
# 枚举
# =========================================================================
class CamError(IntEnum):
    OK = 0
    ERR_CRC_FAILED = 1
    ERR_INVALID_PARAM = 2
    ERR_UNKNOWN = 3
    ERR_NOT_INITIALIZED = 4
    ERR_INVALID_HANDLE = 5
    ERR_INVALID_FUNCTION = 6
    ERR_DEVICE_BUSY = 7
    ERR_INVALID_LENGTH = 8
    ERR_NO_DEVICE = 9
    ERR_DISCONNECTED = 10
    ERR_TIMEOUT = 11
    ERR_BUFFER_TOO_SMALL = 12
    ERR_NOT_SUPPORTED = 13
    PENDING = 100

class PixelFormat(IntEnum):
    MONO8 = 0x01
    CENTERLINE_F32 = 0x10
    POINTCLOUD_XYZ = 0x20

class DeviceRunState(IntEnum):
    IDLE = 0
    READY = 1
    RUNNING = 2
    STOPPING = 3
    ERROR = 4

# =========================================================================
# 结构体
# =========================================================================
class EthernetConfig(Structure):
    _pack_ = 1
    _fields_ = [
        ("deviceIp", c_uint32),
        ("pcIp", c_uint32),
        ("subnetMask", c_uint32),
        ("gateway", c_uint32),
        ("mac", c_uint8 * 6),
    ]

class DeviceInformation(Structure):
    _pack_ = 1
    _fields_ = [
        ("deviceSN", c_uint32),
        ("fpgaVersion", c_char * 8),
        ("network", EthernetConfig),
        ("status", c_uint32),
    ]

class SensorInformation(Structure):
    _pack_ = 1
    _fields_ = [
        ("sensorId", c_uint16),
        ("sensorSN", c_uint32),
        ("firmwareVersion", c_char * 8),
        ("model", c_char * 8),
    ]

class SensorConfig(Structure):
    _pack_ = 1
    _fields_ = [
        ("exposureTime", c_uint32),
        ("gain", c_uint32),
        ("exposureDelay", c_uint32),
        ("roiStart", c_uint16),
        ("roiWidth", c_uint16),
        ("pixelPitch", c_float),
        ("zMin", c_uint16),
        ("zMax", c_uint16),
        ("sampleRate", c_uint32),
    ]

class TriggerConfig(Structure):
    _pack_ = 1
    _fields_ = [
        ("triggerMode", c_uint16),
        ("encoderType", c_uint16),
        ("encoderInputMode", c_uint16),
        ("encoderStep", c_uint32),
        ("externalEnable", c_uint16),
        ("reserved", c_uint16),
    ]

class BatchConfig(Structure):
    _pack_ = 1
    _fields_ = [
        ("length", c_uint16),
    ]

class CalibrationData(Structure):
    _pack_ = 1
    _fields_ = [
        ("data", c_uint8 * 128),
    ]

class FrameInfo(Structure):
    _pack_ = 1
    _fields_ = [
        ("frameId", c_uint32),
        ("sensorId", c_uint16),
        ("batchId", c_uint32),
        ("lineIndex", c_uint32),
        ("encoder", c_uint64),
        ("timestamp", c_uint64),
        ("width", c_uint16),
        ("height", c_uint16),
        ("dataSize", c_uint32),
        ("dataType", c_uint16),
    ]

# =========================================================================
# 回调函数类型
# =========================================================================
DataHandlerType = WINFUNCTYPE(None, POINTER(FrameInfo), c_void_p, c_void_p)
CommandResultCallbackType = WINFUNCTYPE(None, c_int32, c_uint16, c_void_p)
AsyncFrameCallbackType = WINFUNCTYPE(None, c_int32, POINTER(FrameInfo), c_void_p, c_void_p)

# =========================================================================
# 函数签名
# =========================================================================
def _declare(name, restype, *argtypes):
    func = getattr(_cam, name)
    func.restype = restype
    func.argtypes = list(argtypes)
    return func

# 生命周期
Cam_Initialize = _declare("Cam_Initialize", c_int32)
Cam_Finalize = _declare("Cam_Finalize", c_int32)
Cam_GetSdkVersion = _declare("Cam_GetSdkVersion", c_char_p)

# 设备发现
Cam_EnumDevices = _declare("Cam_EnumDevices", c_int32, POINTER(DeviceInformation), c_uint32, POINTER(c_uint32), c_uint32)

# 打开/关闭
Cam_OpenNetworkDevice = _declare("Cam_OpenNetworkDevice", c_int32, c_char_p, c_uint16, c_uint16, POINTER(c_void_p))
Cam_OpenNetworkDeviceMock = _declare("Cam_OpenNetworkDeviceMock", c_int32, c_char_p, c_uint16, c_uint16, POINTER(c_void_p))
Cam_CloseDevice = _declare("Cam_CloseDevice", c_int32, c_void_p)

# 设备信息
Cam_GetDeviceInformation = _declare("Cam_GetDeviceInformation", c_int32, c_void_p, POINTER(DeviceInformation))
Cam_GetRunState = _declare("Cam_GetRunState", c_int32, c_void_p, POINTER(c_int32))

# Sensor 信息
Cam_GetSensorCount = _declare("Cam_GetSensorCount", c_int32, c_void_p, POINTER(c_uint32))
Cam_GetSensorInformation = _declare("Cam_GetSensorInformation", c_int32, c_void_p, c_uint16, POINTER(SensorInformation))

# Sensor 配置
Cam_SetSensorConfig = _declare("Cam_SetSensorConfig", c_int32, c_void_p, c_uint16, POINTER(SensorConfig))
Cam_GetSensorConfig = _declare("Cam_GetSensorConfig", c_int32, c_void_p, c_uint16, POINTER(SensorConfig))

# 触发配置
Cam_SetTriggerConfig = _declare("Cam_SetTriggerConfig", c_int32, c_void_p, c_uint16, POINTER(TriggerConfig))
Cam_GetTriggerConfig = _declare("Cam_GetTriggerConfig", c_int32, c_void_p, c_uint16, POINTER(TriggerConfig))

# 批处理配置
Cam_SetBatchConfig = _declare("Cam_SetBatchConfig", c_int32, c_void_p, c_uint16, POINTER(BatchConfig))
Cam_GetBatchConfig = _declare("Cam_GetBatchConfig", c_int32, c_void_p, c_uint16, POINTER(BatchConfig))

# 标定
Cam_WriteCalibration = _declare("Cam_WriteCalibration", c_int32, c_void_p, c_uint16, POINTER(CalibrationData))
Cam_ReadCalibration = _declare("Cam_ReadCalibration", c_int32, c_void_p, c_uint16, POINTER(CalibrationData))

# 采集控制
Cam_StartCapture = _declare("Cam_StartCapture", c_int32, c_void_p, c_uint16)
Cam_StopCapture = _declare("Cam_StopCapture", c_int32, c_void_p, c_uint16)
Cam_StartBatch = _declare("Cam_StartBatch", c_int32, c_void_p, c_uint16, c_uint32)
Cam_StopBatch = _declare("Cam_StopBatch", c_int32, c_void_p, c_uint16)
Cam_ResetBuffer = _declare("Cam_ResetBuffer", c_int32, c_void_p, c_uint16)

# 数据接收
Cam_RegisterDataHandler = _declare("Cam_RegisterDataHandler", c_int32, c_void_p, DataHandlerType, c_void_p)
Cam_UnregisterDataHandler = _declare("Cam_UnregisterDataHandler", c_int32, c_void_p)
Cam_GetFrameSync = _declare("Cam_GetFrameSync", c_int32, c_void_p, c_uint16, POINTER(FrameInfo), c_void_p, c_uint32, c_uint32)

# 按类型取帧
Cam_GetImageSync = _declare("Cam_GetImageSync", c_int32, c_void_p, c_uint16, POINTER(FrameInfo), c_void_p, c_uint32, c_uint32)
Cam_GetCenterlineSync = _declare("Cam_GetCenterlineSync", c_int32, c_void_p, c_uint16, POINTER(FrameInfo), c_void_p, c_uint32, c_uint32)
Cam_GetPointCloudSync = _declare("Cam_GetPointCloudSync", c_int32, c_void_p, c_uint16, POINTER(FrameInfo), c_void_p, c_uint32, c_uint32)

# 批处理数据
Cam_RequestBatchData = _declare("Cam_RequestBatchData", c_int32, c_void_p, c_uint16, c_uint32)
Cam_ReceiveBatchData = _declare("Cam_ReceiveBatchData", c_int32, c_void_p, c_uint16, c_uint32, POINTER(c_void_p))
Cam_FreeBatchData = _declare("Cam_FreeBatchData", c_int32, c_void_p)
Cam_GetBatchPointCount = _declare("Cam_GetBatchPointCount", c_uint32, c_void_p)
Cam_GetBatchDataWidth = _declare("Cam_GetBatchDataWidth", c_uint32, c_void_p)
Cam_GetBatchData = _declare("Cam_GetBatchData", c_int32, c_void_p, c_void_p, c_uint32)
Cam_RequestBatchDataAsync = _declare("Cam_RequestBatchDataAsync", c_int32, c_void_p, c_uint16, c_uint32, CommandResultCallbackType, c_void_p)

# 异步 API
Cam_StartCaptureAsync = _declare("Cam_StartCaptureAsync", c_int32, c_void_p, c_uint16, c_uint32, CommandResultCallbackType, c_void_p)
Cam_StopCaptureAsync = _declare("Cam_StopCaptureAsync", c_int32, c_void_p, c_uint16, c_uint32, CommandResultCallbackType, c_void_p)
Cam_SetSensorConfigAsync = _declare("Cam_SetSensorConfigAsync", c_int32, c_void_p, c_uint16, POINTER(SensorConfig), c_uint32, CommandResultCallbackType, c_void_p)
Cam_GetSensorConfigAsync = _declare("Cam_GetSensorConfigAsync", c_int32, c_void_p, c_uint16, POINTER(SensorConfig), c_uint32, CommandResultCallbackType, c_void_p)
Cam_SetTriggerConfigAsync = _declare("Cam_SetTriggerConfigAsync", c_int32, c_void_p, c_uint16, POINTER(TriggerConfig), c_uint32, CommandResultCallbackType, c_void_p)
Cam_GetTriggerConfigAsync = _declare("Cam_GetTriggerConfigAsync", c_int32, c_void_p, c_uint16, POINTER(TriggerConfig), c_uint32, CommandResultCallbackType, c_void_p)
Cam_SetBatchConfigAsync = _declare("Cam_SetBatchConfigAsync", c_int32, c_void_p, c_uint16, POINTER(BatchConfig), c_uint32, CommandResultCallbackType, c_void_p)
Cam_GetBatchConfigAsync = _declare("Cam_GetBatchConfigAsync", c_int32, c_void_p, c_uint16, POINTER(BatchConfig), c_uint32, CommandResultCallbackType, c_void_p)
Cam_StartBatchAsync = _declare("Cam_StartBatchAsync", c_int32, c_void_p, c_uint16, c_uint32, c_uint32, CommandResultCallbackType, c_void_p)
Cam_StopBatchAsync = _declare("Cam_StopBatchAsync", c_int32, c_void_p, c_uint16, c_uint32, CommandResultCallbackType, c_void_p)
Cam_ResetBufferAsync = _declare("Cam_ResetBufferAsync", c_int32, c_void_p, c_uint16, c_uint32, CommandResultCallbackType, c_void_p)

# 异步帧
Cam_GetImageAsync = _declare("Cam_GetImageAsync", c_int32, c_void_p, c_uint16, c_uint32, AsyncFrameCallbackType, c_void_p)
Cam_GetCenterlineAsync = _declare("Cam_GetCenterlineAsync", c_int32, c_void_p, c_uint16, c_uint32, AsyncFrameCallbackType, c_void_p)
Cam_GetPointCloudAsync = _declare("Cam_GetPointCloudAsync", c_int32, c_void_p, c_uint16, c_uint32, AsyncFrameCallbackType, c_void_p)


# =========================================================================
# 高层 Python 封装类
# =========================================================================

class BatchData:
    """批处理数据对象句柄封装，支持 with 语句自动释放。"""

    def __init__(self, handle):
        self._handle = handle

    @property
    def handle(self):
        return self._handle

    @property
    def is_valid(self):
        return self._handle.value is not None

    def point_count(self):
        if not self.is_valid:
            return 0
        return Cam_GetBatchPointCount(self._handle)

    def data_width(self):
        if not self.is_valid:
            return 0
        return Cam_GetBatchDataWidth(self._handle)

    def get_data(self, buf_size=0):
        """获取批处理原始数据。buf_size=0 时自动按 point_count * width * 12 计算。"""
        if not self.is_valid:
            return CamError.ERR_INVALID_PARAM, None
        if buf_size == 0:
            buf_size = self.point_count() * self.data_width() * 12
        if buf_size == 0:
            return CamError.ERR_NO_DEVICE, None
        buf = ctypes.create_string_buffer(buf_size)
        err = CamError(Cam_GetBatchData(self._handle, buf, buf_size))
        return err, buf.raw if err == CamError.OK else None

    def free(self):
        if self.is_valid:
            Cam_FreeBatchData(self._handle)
            self._handle = c_void_p(None)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.free()
        return False


class CameraDevice:
    """CameraSDK 设备封装，提供 Pythonic 的接口。"""

    def __init__(self):
        self._handle = c_void_p(None)
        self._data_handler = None  # 保持引用，防止 GC
        self._frame_queue = queue.Queue()
        self._cmd_queue = queue.Queue()
        self._async_frame_queue = queue.Queue()
        # 保持所有待处理回调的引用，防止被 Python GC 回收后 DLL 访问野指针崩溃
        self._pending_callbacks = []

    # ── 生命周期 ──────────────────────────────────────────────────
    @staticmethod
    def initialize():
        return CamError(Cam_Initialize())

    @staticmethod
    def finalize():
        return CamError(Cam_Finalize())

    @staticmethod
    def get_version():
        return Cam_GetSdkVersion().decode() if Cam_GetSdkVersion() else "unknown"

    # ── 打开/关闭 ──────────────────────────────────────────────────
    def open_mock(self, ip="127.0.0.1", control_port=5000, data_port=60000):
        """打开 Mock 设备（无需真实 FPGA）"""
        p = c_void_p()
        err = Cam_OpenNetworkDeviceMock(ip.encode(), control_port, data_port, byref(p))
        if err == CamError.OK:
            self._handle = p
        return CamError(err)

    def open_network(self, ip, control_port=5000, data_port=60000):
        """打开网络设备（连接真实 FPGA）"""
        p = c_void_p()
        err = Cam_OpenNetworkDevice(ip.encode(), control_port, data_port, byref(p))
        if err == CamError.OK:
            self._handle = p
        return CamError(err)

    def close(self):
        err = CamError(Cam_CloseDevice(self._handle))
        self._handle = c_void_p(None)
        return err

    @property
    def handle(self):
        return self._handle

    @property
    def is_open(self):
        return self._handle.value is not None

    # ── 数据回调 ──────────────────────────────────────────────────
    def _make_data_handler(self, on_frame):
        """创建 DataHandler 回调"""
        @DataHandlerType
        def _handler(p_info, p_data, p_user):
            info = p_info.contents
            # 拷贝数据
            data_size = info.dataSize
            data = ctypes.string_at(p_data, data_size) if p_data and data_size > 0 else b""
            # 构造字典
            frame = {
                "frameId": info.frameId,
                "sensorId": info.sensorId,
                "batchId": info.batchId,
                "lineIndex": info.lineIndex,
                "encoder": info.encoder,
                "timestamp": info.timestamp,
                "width": info.width,
                "height": info.height,
                "dataSize": info.dataSize,
                "dataType": PixelFormat(info.dataType),
                "data": data,
            }
            self._frame_queue.put(frame)
            if on_frame:
                on_frame(frame)
        return _handler

    def register_data_handler(self, on_frame=None):
        """注册数据回调，on_frame 在 SDK 回调线程中执行"""
        self._data_handler = self._make_data_handler(on_frame)
        return CamError(Cam_RegisterDataHandler(self._handle, self._data_handler, None))

    def unregister_data_handler(self):
        return CamError(Cam_UnregisterDataHandler(self._handle))

    def poll_frame(self, timeout=0.1):
        """从帧队列中取一帧（非阻塞，主线程安全）"""
        try:
            return self._frame_queue.get(timeout=timeout)
        except queue.Empty:
            return None

    def clear_frames(self):
        while not self._frame_queue.empty():
            self._frame_queue.get_nowait()

    # ── 异步命令 ──────────────────────────────────────────────────
    def _make_cmd_callback(self, on_result=None):
        """创建 CommandResultCallback（保持引用防止 GC）"""
        @CommandResultCallbackType
        def _callback(result, command_id, p_user):
            res = (CamError(result), command_id)
            self._cmd_queue.put(res)
            if on_result:
                on_result(CamError(result), command_id)
        self._pending_callbacks.append(_callback)
        return _callback

    def _make_async_frame_callback(self, on_result=None):
        """创建 AsyncFrameCallback（保持引用防止 GC）"""
        @AsyncFrameCallbackType
        def _callback(result, p_info, p_data, p_user):
            if result == CamError.OK and p_info and p_data:
                info = p_info.contents
                data = ctypes.string_at(p_data, info.dataSize)
                frame = {
                    "frameId": info.frameId,
                    "sensorId": info.sensorId,
                    "width": info.width,
                    "height": info.height,
                    "dataSize": info.dataSize,
                    "dataType": PixelFormat(info.dataType),
                    "data": data,
                }
            else:
                frame = None
            res = (CamError(result), frame)
            self._async_frame_queue.put(res)
            if on_result:
                on_result(CamError(result), frame)
        self._pending_callbacks.append(_callback)
        return _callback

    def _cleanup_callbacks(self, max_keep=50):
        """清理旧的回调引用（保留最近 max_keep 个）"""
        if len(self._pending_callbacks) > max_keep:
            self._pending_callbacks = self._pending_callbacks[-max_keep:]

    def poll_cmd_result(self, timeout=0.0):
        """从命令结果队列中取结果"""
        try:
            return self._cmd_queue.get(timeout=timeout)
        except queue.Empty:
            return None

    def poll_async_frame_result(self, timeout=0.0):
        """从异步帧结果队列中取结果"""
        try:
            return self._async_frame_queue.get(timeout=timeout)
        except queue.Empty:
            return None

    # ── 控制命令（异步）────────────────────────────────────────────
    def start_capture_async(self, sensor_id=1, timeout_ms=3000, on_result=None):
        cb = self._make_cmd_callback(on_result)
        return CamError(Cam_StartCaptureAsync(self._handle, sensor_id, timeout_ms, cb, None))

    def stop_capture_async(self, sensor_id=1, timeout_ms=3000, on_result=None):
        cb = self._make_cmd_callback(on_result)
        return CamError(Cam_StopCaptureAsync(self._handle, sensor_id, timeout_ms, cb, None))

    def set_sensor_config_async(self, sensor_id, config, timeout_ms=3000, on_result=None):
        cb = self._make_cmd_callback(on_result)
        return CamError(Cam_SetSensorConfigAsync(self._handle, sensor_id, byref(config), timeout_ms, cb, None))

    def get_sensor_config_async(self, sensor_id, config, timeout_ms=3000, on_result=None):
        cb = self._make_cmd_callback(on_result)
        return CamError(Cam_GetSensorConfigAsync(self._handle, sensor_id, byref(config), timeout_ms, cb, None))

    def set_batch_config_async(self, sensor_id, config, timeout_ms=3000, on_result=None):
        cb = self._make_cmd_callback(on_result)
        return CamError(Cam_SetBatchConfigAsync(self._handle, sensor_id, byref(config), timeout_ms, cb, None))

    def start_batch_async(self, sensor_id=1, batch_id=0, timeout_ms=3000, on_result=None):
        cb = self._make_cmd_callback(on_result)
        return CamError(Cam_StartBatchAsync(self._handle, sensor_id, batch_id, timeout_ms, cb, None))

    def stop_batch_async(self, sensor_id=1, timeout_ms=3000, on_result=None):
        cb = self._make_cmd_callback(on_result)
        return CamError(Cam_StopBatchAsync(self._handle, sensor_id, timeout_ms, cb, None))

    # ── 异步帧获取 ────────────────────────────────────────────────
    def get_image_async(self, sensor_id=1, timeout_ms=3000, on_result=None):
        cb = self._make_async_frame_callback(on_result)
        return CamError(Cam_GetImageAsync(self._handle, sensor_id, timeout_ms, cb, None))

    def get_centerline_async(self, sensor_id=1, timeout_ms=3000, on_result=None):
        cb = self._make_async_frame_callback(on_result)
        return CamError(Cam_GetCenterlineAsync(self._handle, sensor_id, timeout_ms, cb, None))

    def get_pointcloud_async(self, sensor_id=1, timeout_ms=3000, on_result=None):
        cb = self._make_async_frame_callback(on_result)
        return CamError(Cam_GetPointCloudAsync(self._handle, sensor_id, timeout_ms, cb, None))

    # ── 同步 API ──────────────────────────────────────────────────
    def set_sensor_config(self, sensor_id, exposure_time=1000, gain=200, sample_rate=100):
        cfg = SensorConfig()
        cfg.exposureTime = exposure_time
        cfg.gain = gain
        cfg.sampleRate = sample_rate
        return CamError(Cam_SetSensorConfig(self._handle, sensor_id, byref(cfg)))

    def set_batch_config(self, sensor_id, length):
        cfg = BatchConfig()
        cfg.length = length
        return CamError(Cam_SetBatchConfig(self._handle, sensor_id, byref(cfg)))

    def get_batch_config(self, sensor_id):
        cfg = BatchConfig()
        err = CamError(Cam_GetBatchConfig(self._handle, sensor_id, byref(cfg)))
        return err, cfg.length

    def start_capture(self, sensor_id=1):
        return CamError(Cam_StartCapture(self._handle, sensor_id))

    def stop_capture(self, sensor_id=1):
        return CamError(Cam_StopCapture(self._handle, sensor_id))

    def start_batch(self, sensor_id=1, batch_id=0):
        return CamError(Cam_StartBatch(self._handle, sensor_id, batch_id))

    def stop_batch(self, sensor_id=1):
        return CamError(Cam_StopBatch(self._handle, sensor_id))

    def receive_batch_data(self, sensor_id=1, timeout_ms=5000):
        """阻塞接收批处理数据。返回 (CamError, BatchData)。"""
        handle = c_void_p()
        err = CamError(Cam_ReceiveBatchData(self._handle, sensor_id, timeout_ms, byref(handle)))
        if err == CamError.OK and handle.value:
            return err, BatchData(handle)
        return err, None

    def enum_devices(self, capacity=8, timeout_ms=1000):
        """枚举设备。返回 (CamError, [DeviceInformation...], total_count)。"""
        if capacity == 0:
            count = c_uint32()
            err = CamError(Cam_EnumDevices(None, 0, byref(count), timeout_ms))
            return err, [], count.value

        arr = (DeviceInformation * capacity)()
        count = c_uint32()
        err = CamError(Cam_EnumDevices(arr, capacity, byref(count), timeout_ms))
        devices = [arr[i] for i in range(min(count.value, capacity))]
        return err, devices, count.value

    def get_sensor_count(self):
        count = c_uint32()
        err = CamError(Cam_GetSensorCount(self._handle, byref(count)))
        return err, count.value

    def get_run_state(self):
        state = c_int32()
        err = CamError(Cam_GetRunState(self._handle, byref(state)))
        return err, DeviceRunState(state.value)


# =========================================================================
# 便捷函数
# =========================================================================
def make_sensor_config(exposure_time=1000, gain=200, sample_rate=100):
    cfg = SensorConfig()
    cfg.exposureTime = exposure_time
    cfg.gain = gain
    cfg.sampleRate = sample_rate
    return cfg

def make_batch_config(length=5):
    cfg = BatchConfig()
    cfg.length = length
    return cfg

def make_trigger_config(mode=1, encoder_type=1, encoder_mode=3, encoder_step=1000, external_enable=0):
    cfg = TriggerConfig()
    cfg.triggerMode = mode
    cfg.encoderType = encoder_type
    cfg.encoderInputMode = encoder_mode
    cfg.encoderStep = encoder_step
    cfg.externalEnable = external_enable
    return cfg

def err_name(err):
    """返回错误码的名称"""
    try:
        return CamError(err).name
    except ValueError:
        return f"UNKNOWN({err})"