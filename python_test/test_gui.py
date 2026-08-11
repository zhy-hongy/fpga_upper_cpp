"""
CameraSDK Python GUI 测试工具 — 使用 tkinter + 非阻塞异步 API。
用法: python test_gui.py
"""

import sys
import os
import threading
import time
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
from collections import deque

# 确保能找到 camera_sdk 模块
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from camera_sdk import (
    CameraDevice, CamError, PixelFormat, DeviceRunState,
    make_sensor_config, make_batch_config, err_name
)

# =========================================================================
# 日志工具
# =========================================================================
class Log:
    """线程安全的日志缓冲区"""
    def __init__(self, max_lines=1000):
        self._lines = deque(maxlen=max_lines)
        self._lock = threading.Lock()

    def add(self, msg):
        ts = time.strftime("%H:%M:%S")
        with self._lock:
            self._lines.append(f"[{ts}] {msg}")

    def drain(self):
        with self._lock:
            lines = list(self._lines)
            self._lines.clear()
        return lines


# =========================================================================
# GUI 主窗口
# =========================================================================
class CameraTestApp:
    def __init__(self):
        self.device = CameraDevice()
        self.log = Log()
        self._frame_count = 0
        self._async_pending = 0  # 异步命令/in-flight 计数
        self._running = False

        self._build_ui()
        self._poll()  # 启动轮询

    # ── UI 构建 ────────────────────────────────────────────────────
    def _build_ui(self):
        self.root = tk.Tk()
        self.root.title("CameraSDK Python 测试工具 (非阻塞异步模式)")
        self.root.geometry("900x700")
        self.root.minsize(700, 500)

        # 顶部控制栏
        ctrl_frame = ttk.Frame(self.root, padding=5)
        ctrl_frame.pack(fill=tk.X)

        row1 = ttk.Frame(ctrl_frame)
        row1.pack(fill=tk.X, pady=2)
        ttk.Label(row1, text="IP:").pack(side=tk.LEFT, padx=2)
        self.ip_var = tk.StringVar(value="127.0.0.1")
        ttk.Entry(row1, textvariable=self.ip_var, width=14).pack(side=tk.LEFT, padx=2)
        ttk.Label(row1, text="CtrlPort:").pack(side=tk.LEFT, padx=2)
        self.ctrl_port_var = tk.IntVar(value=5000)
        ttk.Entry(row1, textvariable=self.ctrl_port_var, width=6).pack(side=tk.LEFT, padx=2)
        ttk.Label(row1, text="DataPort:").pack(side=tk.LEFT, padx=2)
        self.data_port_var = tk.IntVar(value=60000)
        ttk.Entry(row1, textvariable=self.data_port_var, width=6).pack(side=tk.LEFT, padx=2)

        ttk.Label(row1, text="Sensor:").pack(side=tk.LEFT, padx=(10, 2))
        self.sensor_var = tk.IntVar(value=1)
        ttk.Spinbox(row1, textvariable=self.sensor_var, from_=1, to=4, width=4).pack(side=tk.LEFT, padx=2)

        self.btn_open = ttk.Button(row1, text="打开 Mock", command=self._on_open, width=10)
        self.btn_open.pack(side=tk.LEFT, padx=5)
        self.btn_close = ttk.Button(row1, text="关闭", command=self._on_close, state=tk.DISABLED, width=6)
        self.btn_close.pack(side=tk.LEFT, padx=2)
        self.status_var = tk.StringVar(value="未连接")
        ttk.Label(row1, textvariable=self.status_var, foreground="red").pack(side=tk.LEFT, padx=10)

        # 数据回调
        row2 = ttk.Frame(ctrl_frame)
        row2.pack(fill=tk.X, pady=2)
        self.btn_reg_handler = ttk.Button(row2, text="注册数据回调", command=self._on_register_handler, state=tk.DISABLED)
        self.btn_reg_handler.pack(side=tk.LEFT, padx=2)
        self.btn_unreg_handler = ttk.Button(row2, text="注销回调", command=self._on_unregister_handler, state=tk.DISABLED)
        self.btn_unreg_handler.pack(side=tk.LEFT, padx=2)
        self.frame_count_var = tk.StringVar(value="帧数: 0")
        ttk.Label(row2, textvariable=self.frame_count_var, font=("", 10, "bold")).pack(side=tk.LEFT, padx=10)

        # 中间区域：左右分栏
        main = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
        main.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # 左栏：控制面板
        left = ttk.Frame(main)
        main.add(left, weight=0)

        # 采集控制
        cap_group = ttk.LabelFrame(left, text="采集控制 (异步)", padding=5)
        cap_group.pack(fill=tk.X, pady=3)

        btn_row = ttk.Frame(cap_group)
        btn_row.pack(fill=tk.X, pady=2)
        self.btn_start = ttk.Button(btn_row, text="开始采集", command=lambda: self._on_start_capture(), state=tk.DISABLED)
        self.btn_start.pack(side=tk.LEFT, padx=2)
        self.btn_stop = ttk.Button(btn_row, text="停止采集", command=lambda: self._on_stop_capture(), state=tk.DISABLED)
        self.btn_stop.pack(side=tk.LEFT, padx=2)

        # 异步取帧
        frame_group = ttk.LabelFrame(left, text="异步取单帧 (非阻塞)", padding=5)
        frame_group.pack(fill=tk.X, pady=3)
        self.btn_get_image = ttk.Button(frame_group, text="取图像 (MONO8)", command=lambda: self._on_get_async("image"), state=tk.DISABLED)
        self.btn_get_image.pack(fill=tk.X, pady=1)
        self.btn_get_centerline = ttk.Button(frame_group, text="取中心线 (F32)", command=lambda: self._on_get_async("centerline"), state=tk.DISABLED)
        self.btn_get_centerline.pack(fill=tk.X, pady=1)
        self.btn_get_pointcloud = ttk.Button(frame_group, text="取点云 (XYZ)", command=lambda: self._on_get_async("pointcloud"), state=tk.DISABLED)
        self.btn_get_pointcloud.pack(fill=tk.X, pady=1)

        # 批处理
        batch_group = ttk.LabelFrame(left, text="批处理 (阻塞)", padding=5)
        batch_group.pack(fill=tk.X, pady=3)

        ttk.Label(batch_group, text="行数:").pack(side=tk.LEFT, padx=2)
        self.batch_len_var = tk.IntVar(value=5)
        ttk.Spinbox(batch_group, textvariable=self.batch_len_var, from_=1, to=100, width=5).pack(side=tk.LEFT, padx=2)
        self.btn_set_batch = ttk.Button(batch_group, text="设置", command=self._on_set_batch, state=tk.DISABLED)
        self.btn_set_batch.pack(side=tk.LEFT, padx=2)

        btn_row2 = ttk.Frame(batch_group)
        btn_row2.pack(fill=tk.X, pady=2)
        self.btn_start_batch = ttk.Button(btn_row2, text="开始批处理", command=self._on_start_batch_async, state=tk.DISABLED)
        self.btn_start_batch.pack(side=tk.LEFT, padx=2)
        self.btn_receive_batch = ttk.Button(btn_row2, text="阻塞接收", command=self._on_receive_batch, state=tk.DISABLED)
        self.btn_receive_batch.pack(side=tk.LEFT, padx=2)
        self.btn_stop_batch = ttk.Button(btn_row2, text="停止批处理", command=self._on_stop_batch_async, state=tk.DISABLED)
        self.btn_stop_batch.pack(side=tk.LEFT, padx=2)

        # 右栏：日志
        right = ttk.Frame(main)
        main.add(right, weight=1)
        self.log_text = scrolledtext.ScrolledText(right, wrap=tk.WORD, font=("Consolas", 9))
        self.log_text.pack(fill=tk.BOTH, expand=True)

        # 底部
        bottom = ttk.Frame(self.root, padding=3)
        bottom.pack(fill=tk.X, side=tk.BOTTOM)
        self.pending_var = tk.StringVar(value="待处理: 0")
        ttk.Label(bottom, textvariable=self.pending_var).pack(side=tk.LEFT, padx=5)
        ttk.Button(bottom, text="清空日志", command=self._on_clear_log).pack(side=tk.RIGHT, padx=5)

        self.root.protocol("WM_DELETE_WINDOW", self._on_exit)

    # ── 日志 ────────────────────────────────────────────────────────
    def _log(self, msg):
        self.log.add(msg)

    def _update_log(self):
        for line in self.log.drain():
            self.log_text.insert(tk.END, line + "\n")
        self.log_text.see(tk.END)

    # ── 轮询（主线程安全更新 GUI）──────────────────────────────────
    def _poll(self):
        """定时轮询回调队列，更新 GUI"""
        try:
            # 1. 处理数据帧
            frame = self.device.poll_frame(timeout=0)
            if frame:
                self._frame_count += 1
                self.frame_count_var.set(f"帧数: {self._frame_count}")
                self._log(f"帧 #{frame['frameId']} sensor={frame['sensorId']} "
                          f"{frame['width']}x{frame['height']} "
                          f"type={frame['dataType'].name} size={frame['dataSize']}")

            # 2. 处理异步命令结果
            cmd = self.device.poll_cmd_result(timeout=0)
            if cmd:
                err, cmd_id = cmd
                self._async_pending = max(0, self._async_pending - 1)
                self._log(f"CMD ACK: id={cmd_id} result={err.name}")

            # 3. 处理异步帧结果
            async_frame = self.device.poll_async_frame_result(timeout=0)
            if async_frame:
                err, frame = async_frame
                self._async_pending = max(0, self._async_pending - 1)
                if err == CamError.OK and frame:
                    self._log(f"AsyncFrame: id={frame['frameId']} type={frame['dataType'].name} "
                              f"{frame['width']}x{frame['height']} size={frame['dataSize']}")
                else:
                    self._log(f"AsyncFrame: {err.name}")

            self._update_log()
            self.pending_var.set(f"待处理: {self._async_pending}")
        except Exception as e:
            self._log(f"ERR: {e}")

        self.root.after(50, self._poll)  # 50ms 轮询

    # ── 按钮状态管理 ───────────────────────────────────────────────
    def _set_connected(self, connected):
        state = tk.NORMAL if connected else tk.DISABLED
        for btn in [self.btn_close, self.btn_reg_handler, self.btn_unreg_handler,
                     self.btn_start, self.btn_stop, self.btn_get_image,
                     self.btn_get_centerline, self.btn_get_pointcloud,
                     self.btn_set_batch, self.btn_start_batch,
                     self.btn_receive_batch, self.btn_stop_batch]:
            btn.configure(state=state)
        self.btn_open.configure(state=tk.DISABLED if connected else tk.NORMAL)
        self.status_var.set("已连接 (Mock)" if connected else "未连接")
        if not connected:
            self._frame_count = 0
            self.frame_count_var.set("帧数: 0")

    # ── 事件处理 ───────────────────────────────────────────────────
    def _on_open(self):
        self._log("正在打开 Mock 设备...")
        err = CameraDevice.initialize()
        self._log(f"Initialize: {err.name}")
        if err != CamError.OK:
            messagebox.showerror("错误", f"初始化失败: {err.name}")
            return

        err = self.device.open_mock(
            self.ip_var.get(),
            self.ctrl_port_var.get(),
            self.data_port_var.get()
        )
        self._log(f"OpenMock: {err.name}")
        if err == CamError.OK:
            self._log(f"SDK 版本: {self.device.get_version()}")
            self._set_connected(True)
        else:
            messagebox.showerror("错误", f"打开设备失败: {err.name}")

    def _on_close(self):
        self._log("正在关闭设备...")
        self.device.unregister_data_handler()
        err = self.device.close()
        self._log(f"Close: {err.name}")
        CameraDevice.finalize()
        self._set_connected(False)

    def _on_register_handler(self):
        err = self.device.register_data_handler(on_frame=None)
        self._log(f"RegisterDataHandler: {err.name}")

    def _on_unregister_handler(self):
        err = self.device.unregister_data_handler()
        self._log(f"UnregisterDataHandler: {err.name}")

    def _on_start_capture(self):
        self.device.clear_frames()
        self._frame_count = 0
        self.frame_count_var.set("帧数: 0")

        def on_result(err, cmd_id):
            self._log(f"StartCapture ACK: {err.name} cmdId={cmd_id}")

        self._async_pending += 1
        err = self.device.start_capture_async(self.sensor_var.get(), 3000, on_result)
        self._log(f"StartCaptureAsync: {err.name}")

    def _on_stop_capture(self):
        def on_result(err, cmd_id):
            self._log(f"StopCapture ACK: {err.name} cmdId={cmd_id}")

        self._async_pending += 1
        err = self.device.stop_capture_async(self.sensor_var.get(), 3000, on_result)
        self._log(f"StopCaptureAsync: {err.name}")

    def _on_get_async(self, frame_type):
        """异步获取单帧"""
        sid = self.sensor_var.get()

        def on_result(err, frame):
            self._log(f"Async {frame_type}: {err.name}")

        self._async_pending += 1
        if frame_type == "image":
            err = self.device.get_image_async(sid, 3000, on_result)
        elif frame_type == "centerline":
            err = self.device.get_centerline_async(sid, 3000, on_result)
        else:
            err = self.device.get_pointcloud_async(sid, 3000, on_result)
        self._log(f"Get{frame_type.capitalize()}Async: {err.name}")

    def _on_set_batch(self):
        err = self.device.set_batch_config(self.sensor_var.get(), self.batch_len_var.get())
        self._log(f"SetBatchConfig length={self.batch_len_var.get()}: {err.name}")

    def _on_start_batch_async(self):
        def on_result(err, cmd_id):
            self._log(f"StartBatch ACK: {err.name} cmdId={cmd_id}")

        self._async_pending += 1
        err = self.device.start_batch_async(self.sensor_var.get(), 0, 3000, on_result)
        self._log(f"StartBatchAsync: {err.name}")

    def _on_stop_batch_async(self):
        def on_result(err, cmd_id):
            self._log(f"StopBatch ACK: {err.name} cmdId={cmd_id}")

        self._async_pending += 1
        err = self.device.stop_batch_async(self.sensor_var.get(), 3000, on_result)
        self._log(f"StopBatchAsync: {err.name}")

    def _on_receive_batch(self):
        """阻塞接收批处理（在后台线程执行，避免冻结 GUI）"""
        sid = self.sensor_var.get()
        self._log(f"正在阻塞接收批处理数据 (sensor={sid})...")

        def _worker():
            err, batch = self.device.receive_batch_data(sid, 5000)
            if err != CamError.OK or batch is None:
                self._log(f"ReceiveBatchData: {err.name}")
                return
            try:
                count = batch.point_count()
                width = batch.data_width()
                self._log(f"ReceiveBatchData: {err.name} count={count} width={width}")
                err2, data = batch.get_data()
                self._log(f"GetBatchData: {err2.name} size={len(data) if data else 0}")
            finally:
                batch.free()

        threading.Thread(target=_worker, daemon=True).start()

    def _on_clear_log(self):
        self.log_text.delete("1.0", tk.END)

    def _on_exit(self):
        if self.device.is_open:
            self.device.unregister_data_handler()
            self.device.close()
        CameraDevice.finalize()
        self.root.destroy()

    def run(self):
        self.root.mainloop()


if __name__ == "__main__":
    app = CameraTestApp()
    app.run()