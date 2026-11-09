# -*- coding: utf-8 -*-
"""串口读取线程。读循环在子线程,写操作线程安全(主线程可直接调用 write)。"""
import threading
import time
from datetime import datetime

import serial
from PyQt6.QtCore import QThread, pyqtSignal

from .frame_parser import FrameParser

PARITY_MAP = {
    "None": serial.PARITY_NONE, "Odd": serial.PARITY_ODD,
    "Even": serial.PARITY_EVEN, "Mark": serial.PARITY_MARK,
    "Space": serial.PARITY_SPACE,
}
STOPBITS_MAP = {
    "1": serial.STOPBITS_ONE, "1.5": serial.STOPBITS_ONE_POINT_FIVE,
    "2": serial.STOPBITS_TWO,
}
FRAME_GAP_MS = 20   # 串口空闲超过此毫秒数即分帧(HEX/二进制无 \n 分隔,按空闲时间分帧)
POLL = 0.003        # 空闲轮询间隔,决定分帧的时间分辨率


class SerialWorker(QThread):
    frame_received = pyqtSignal(bytes, object)   # (帧数据, 首字节时间 datetime)
    opened = pyqtSignal()
    error = pyqtSignal(str)
    closed = pyqtSignal()

    def __init__(self, port, baud, databits, parity, stopbits,
                 frame_gap=FRAME_GAP_MS, split_nl=True, parent=None):
        super().__init__(parent)
        self._port = port
        self._baud = baud
        self._databits = databits
        self._parity = PARITY_MAP.get(parity, serial.PARITY_NONE)
        self._stopbits = STOPBITS_MAP.get(str(stopbits), serial.STOPBITS_ONE)
        self._frame_gap = max(5, int(frame_gap)) / 1000.0
        self._split_nl = bool(split_nl)   # 仅 ASCII 模式按 \n 分帧;读循环每次取最新值
        self._running = False
        self._stop_requested = False
        self._ser = None
        self._wlock = threading.Lock()

    def set_newline_split(self, on: bool):
        """运行时切换是否按 \n 分帧(bool 赋值原子,线程安全)。"""
        self._split_nl = bool(on)

    # ---------- 线程主循环 ----------
    def run(self):
        parser = FrameParser()
        try:
            self._ser = serial.Serial(
                port=self._port, baudrate=self._baud, bytesize=self._databits,
                parity=self._parity, stopbits=self._stopbits, timeout=0.05,
            )
        except Exception as e:
            self.error.emit(f"打开串口失败: {e}")
            # 打开失败也必须通知主界面清理 worker；否则会留下一个已经
            # 结束但仍被当作“占用串口”的幽灵线程，阻止端口再次连接。
            self.closed.emit()
            return

        # serial.Serial() 返回前也可能收到断开请求，不能在这里重新启动线程。
        if self._stop_requested:
            with self._wlock:
                try:
                    self._ser.close()
                except Exception:
                    pass
                self._ser = None
            self.closed.emit()
            return

        self._running = True
        self.opened.emit()
        last_rx = time.monotonic()
        try:
            while self._running and not self._stop_requested:
                try:
                    n = self._ser.in_waiting
                except Exception as e:
                    self.error.emit(f"串口读取错误: {e}")
                    break
                if n:
                    try:
                        data = self._ser.read(n)
                    except Exception as e:
                        self.error.emit(f"串口读取错误: {e}")
                        break
                    last_rx = time.monotonic()
                    now = datetime.now()
                    for frame, ts in parser.feed(data, now, self._split_nl):
                        self.frame_received.emit(frame, ts)
                else:
                    # 空闲超过帧间隔 → 把积压字节当作一帧输出。
                    # 二进制/HEX 数据没有 \n 分隔,靠总线空闲时间分帧
                    # 适用于依赖帧间空闲时间判定边界的二进制协议。
                    if time.monotonic() - last_rx >= self._frame_gap:
                        for frame, ts in parser.timeout_flush():
                            self.frame_received.emit(frame, ts)
                        last_rx = time.monotonic()
                    time.sleep(POLL)
        finally:
            with self._wlock:
                try:
                    self._ser.close()
                except Exception:
                    pass
                self._ser = None
            self.closed.emit()

    # ---------- 主线程调用 ----------
    def write(self, data: bytes) -> int:
        """线程安全写入,返回实际写入字节数;失败返回 -1。"""
        with self._wlock:
            if self._ser is None or not self._ser.is_open:
                return -1
            try:
                return self._ser.write(data)
            except Exception as e:
                self.error.emit(f"发送失败: {e}")
                return -1

    def stop(self, wait=True, timeout_ms=1500):
        """Request a cooperative stop without destroying a running QThread.

        ``cancel_read`` only wakes a pending Windows serial read; the worker
        thread remains the sole owner that closes ``self._ser`` in ``finally``.
        Callers that need an asynchronous disconnect can pass ``wait=False``
        and release the worker only after QThread.finished is delivered.
        """
        self._stop_requested = True
        self._running = False
        serial_port = self._ser
        if serial_port is not None:
            try:
                cancel_read = getattr(serial_port, "cancel_read", None)
                if cancel_read:
                    cancel_read()
            except Exception:
                pass
        if wait and self.isRunning():
            return self.wait(max(0, int(timeout_ms)))
        return not self.isRunning()
