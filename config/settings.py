# -*- coding: utf-8 -*-
"""JSON 配置读写。开发环境存项目根目录,打包后存 %APPDATA%/SerialAssistant。"""
import json
import os
import sys

DEFAULTS = {
    # 串口参数
    "port": "", "baud": 115200, "databits": 8, "parity": "None", "stopbits": "1",
    # 显示
    "mode": 1,                 # 0=ASCII 1=HEX双栏 2=纯HEX
    "autoscroll": True,
    "show_ts": True,
    "ts_interval": 100,        # ms
    "frame_gap": 20,           # ms 串口空闲超过此值即分帧(HEX/二进制按空闲分帧)
    "max_lines": 5000,         # 日志最大行数(自动裁剪)
    # 外观
    "font_family": "Consolas",
    "font_size": 10,
    "col_text": "#E2E8F0",
    "col_bg": "#0D0D0D",
    "col_ts": "#00C853",
    "col_hex": "#E0A86A",
    "col_ascii": "#4FC3F7",
    # 着色规则C: [{pattern, color, enabled}]
    "rules": [
        {"pattern": r"\bERROR\b|\bFAIL\b", "color": "#FF5252", "enabled": True},
        {"pattern": r"\bWARN(ING)?\b", "color": "#FFB300", "enabled": True},
        {"pattern": r"\bOK\b|\bPASS\b", "color": "#00E676", "enabled": True},
    ],
    # 发送区
    "send_hex": False,
    "line_ending": 1,          # 0=无 1=\r\n 2=\r 3=\n
    "timer_send": False,
    "timer_interval": 500,
    "quick_cmds": [
        {"name": "AT", "data": "AT", "hex": False},
        {"name": "版本", "data": "AT+VER?", "hex": False},
        {"name": "复位", "data": "AT+RST", "hex": False},
        {"name": "读寄存器", "data": "01 03 00 00 00 0A C5 CD", "hex": True},
        {"name": "心跳", "data": "55 AA 01 00", "hex": True},
        {"name": "自定义", "data": "", "hex": False},
    ],
    # 自动回复（按列表顺序命中第一条规则）
    "auto_reply_enabled": False,
    "auto_reply_cooldown": 100,  # ms，防止设备回显造成高速循环
    "auto_reply_rules": [],
    # 窗口
    "pin": False,
    "geometry": None,
}

REMOVED_WORKBENCH_KEYS = {
    "modbus_last_mode",
    "modbus_rtu_serial",
    "modbus_master_tasks",
    "modbus_slave_device_id",
    "modbus_slave_registers",
}


def _config_path() -> str:
    if getattr(sys, "frozen", False):  # PyInstaller 打包环境
        base = os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")),
                            "SerialAssistant")
        os.makedirs(base, exist_ok=True)
        return os.path.join(base, "config.json")
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(root, "config.json")


class Settings:
    def __init__(self):
        self._path = _config_path()
        self._data = dict(DEFAULTS)
        self.load()

    def load(self):
        try:
            with open(self._path, "r", encoding="utf-8") as f:
                saved = json.load(f)
            for key in REMOVED_WORKBENCH_KEYS:
                saved.pop(key, None)
            self._data.update(saved)
        except Exception:
            pass

    def save(self):
        try:
            with open(self._path, "w", encoding="utf-8") as f:
                json.dump(self._data, f, ensure_ascii=False, indent=2)
        except Exception:
            pass

    def get(self, key, default=None):
        return self._data.get(key, DEFAULTS.get(key, default))

    def set(self, key, value, flush=True):
        self._data[key] = value
        if flush:
            self.save()
