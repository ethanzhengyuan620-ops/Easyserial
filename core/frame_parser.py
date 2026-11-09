# -*- coding: utf-8 -*-
"""字节流分帧:按 \\n 分帧 + 超时强制 flush + 单帧长度截断。"""
from datetime import datetime

MAX_FRAME = 4096


class FrameParser:
    def __init__(self):
        self._buf = bytearray()
        self._first_time = None

    def feed(self, data: bytes, now: datetime, split_nl: bool = True):
        """喂入字节流,返回 [(frame_bytes, first_byte_time), ...]。
        split_nl=True 时按 \\n 分帧(ASCII 行协议);HEX/二进制模式置 False,
        \\n 仅当普通数据,帧改由空闲时间(timeout_flush)/ 最大长度切分。"""
        frames = []
        for b in data:
            if not self._buf:
                self._first_time = now
            self._buf.append(b)
            if (split_nl and b == 0x0A) or len(self._buf) >= MAX_FRAME:
                frames.append((bytes(self._buf), self._first_time))
                self._buf.clear()
        return frames

    def timeout_flush(self):
        """设备忘发 \\n 时,由外部定时调用强制输出积压数据。"""
        if self._buf:
            frame = (bytes(self._buf), self._first_time)
            self._buf.clear()
            return [frame]
        return []
