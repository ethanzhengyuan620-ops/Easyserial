# -*- coding: utf-8 -*-
"""左侧配置面板:连接设置 / 显示模式 / 工具选项 / 操作按钮。"""
from PyQt6.QtCore import Qt, QTimer, pyqtSignal
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
                             QLabel, QPushButton, QComboBox)
from serial.tools import list_ports

from . import theme
from .widgets import PillGroup, ToggleSwitch, NoWheelSpinBox, NoWheelComboBox, hline

BAUDS = ["1200", "2400", "4800", "9600", "14400", "19200", "38400", "57600",
         "115200", "230400", "460800", "921600", "1000000", "2000000"]


class SerialPanel(QWidget):
    connect_toggled = pyqtSignal()          # 点击 连接/断开
    mode_changed = pyqtSignal(int)
    autoscroll_changed = pyqtSignal(bool)
    ts_changed = pyqtSignal(bool)
    interval_changed = pyqtSignal(int)
    clear_clicked = pyqtSignal()
    rules_clicked = pyqtSignal()
    tools_clicked = pyqtSignal()
    pin_toggled = pyqtSignal(bool)
    record_toggled = pyqtSignal(bool)

    def __init__(self, settings, parent=None):
        super().__init__(parent)
        self.s = settings
        self.setFixedWidth(240)
        root = QVBoxLayout(self)
        root.setContentsMargins(12, 10, 12, 10)
        root.setSpacing(8)

        # ====== 连接设置 ======
        head = QHBoxLayout()
        t = QLabel("≋  连接设置")
        t.setObjectName("sectionTitle")
        self.badge = QLabel("未连接")
        self.badge.setStyleSheet(
            f"color:{theme.TEXT_DIM};border:1px solid {theme.BORDER};"
            "border-radius:9px;padding:2px 10px;font-size:11px;")
        head.addWidget(t)
        head.addStretch()
        head.addWidget(self.badge)
        root.addLayout(head)

        root.addWidget(self._dim("端口"))
        self.port_combo = NoWheelComboBox()
        self.port_combo.setEditable(True)
        root.addWidget(self.port_combo)

        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(4)
        grid.addWidget(self._dim("波特率"), 0, 0)
        grid.addWidget(self._dim("数据位"), 0, 1)
        self.baud_combo = NoWheelComboBox()
        self.baud_combo.setEditable(True)
        self.baud_combo.addItems(BAUDS)
        self.baud_combo.setCurrentText(str(self.s.get("baud")))
        self.data_combo = NoWheelComboBox()
        self.data_combo.addItems(["5", "6", "7", "8"])
        self.data_combo.setCurrentText(str(self.s.get("databits")))
        grid.addWidget(self.baud_combo, 1, 0)
        grid.addWidget(self.data_combo, 1, 1)
        grid.addWidget(self._dim("校验位"), 2, 0)
        grid.addWidget(self._dim("停止位"), 2, 1)
        self.parity_combo = NoWheelComboBox()
        self.parity_combo.addItems(["None", "Odd", "Even", "Mark", "Space"])
        self.parity_combo.setCurrentText(self.s.get("parity"))
        self.stop_combo = NoWheelComboBox()
        self.stop_combo.addItems(["1", "1.5", "2"])
        self.stop_combo.setCurrentText(str(self.s.get("stopbits")))
        grid.addWidget(self.parity_combo, 3, 0)
        grid.addWidget(self.stop_combo, 3, 1)
        root.addLayout(grid)

        self.conn_btn = QPushButton("连接串口")
        self.conn_btn.setObjectName("accent")
        self.conn_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.conn_btn.setMinimumHeight(38)
        self.conn_btn.clicked.connect(self.connect_toggled.emit)
        root.addWidget(self.conn_btn)
        root.addWidget(hline())

        # ====== 显示模式 ======
        t2 = QLabel("≡  显示模式")
        t2.setObjectName("sectionTitle")
        root.addWidget(t2)
        self.mode_pills = PillGroup(["ASCII 文本", "HEX 双栏", "纯 HEX"])
        self.mode_pills.set_current(self.s.get("mode"))
        self.mode_pills.changed.connect(self._on_mode)
        root.addWidget(self.mode_pills)
        root.addWidget(hline())

        # ====== 工具选项 ======
        t3 = QLabel("✧  工具选项")
        t3.setObjectName("sectionTitle")
        root.addWidget(t3)

        r1 = QHBoxLayout()
        r1.addWidget(QLabel("自动滚动"))
        r1.addStretch()
        self.scroll_toggle = ToggleSwitch()
        self.scroll_toggle.setChecked(self.s.get("autoscroll"))
        self.scroll_toggle.toggled.connect(self._on_autoscroll)
        r1.addWidget(self.scroll_toggle)
        root.addLayout(r1)

        r2 = QHBoxLayout()
        r2.addWidget(QLabel("显示时间戳"))
        r2.addStretch()
        self.ts_toggle = ToggleSwitch()
        self.ts_toggle.setChecked(self.s.get("show_ts"))
        self.ts_toggle.toggled.connect(self._on_ts)
        r2.addWidget(self.ts_toggle)
        root.addLayout(r2)

        root.addWidget(self._dim("时间戳间隔"))
        self.interval_spin = NoWheelSpinBox()
        self.interval_spin.setRange(5, 60000)
        self.interval_spin.setValue(self.s.get("ts_interval"))
        self.interval_spin.setSuffix(" ms")
        self.interval_spin.valueChanged.connect(self._on_interval)
        root.addWidget(self.interval_spin)

        root.addSpacing(6)

        # ====== 操作按钮 ======
        self.clear_btn = self._action_btn("🗑  清空日志", root)
        self.clear_btn.clicked.connect(self.clear_clicked.emit)
        self.rules_btn = self._action_btn("🎨  配色规则", root)
        self.rules_btn.clicked.connect(self.rules_clicked.emit)
        self.tools_btn = self._action_btn("🔧  工具箱", root)
        self.tools_btn.clicked.connect(self.tools_clicked.emit)
        self.pin_btn = self._action_btn("📌  置顶窗口", root, checkable=True)
        self.pin_btn.setChecked(self.s.get("pin"))
        self.pin_btn.toggled.connect(self.pin_toggled.emit)
        self.rec_btn = self._action_btn("⏺  录制日志", root, checkable=True)
        self.rec_btn.toggled.connect(self.record_toggled.emit)
        root.addStretch()

        # ====== 端口热插拔检测(500ms 轮询) ======
        self._known_ports = []
        self._port_timer = QTimer(self)
        self._port_timer.setInterval(500)
        self._port_timer.timeout.connect(self._refresh_ports)
        self._port_timer.start()
        self._refresh_ports()
        if self.s.get("port"):
            self.port_combo.setCurrentText(self.s.get("port"))

    # ---------- helpers ----------
    @staticmethod
    def _dim(text):
        lab = QLabel(text)
        lab.setObjectName("dim")
        return lab

    @staticmethod
    def _action_btn(text, layout, checkable=False):
        btn = QPushButton(text)
        btn.setCursor(Qt.CursorShape.PointingHandCursor)
        btn.setCheckable(checkable)
        btn.setStyleSheet("text-align:left;padding-left:12px;")
        layout.addWidget(btn)
        return btn

    def _refresh_ports(self):
        ports = [p.device for p in list_ports.comports()]
        if ports == self._known_ports:
            return
        self._known_ports = ports
        cur = self.port_combo.currentText()
        self.port_combo.blockSignals(True)
        self.port_combo.clear()
        self.port_combo.addItems(ports)
        if cur in ports or not ports:
            self.port_combo.setCurrentText(cur)
        self.port_combo.blockSignals(False)

    # ---------- 信号转发 + 持久化 ----------
    def _on_mode(self, i):
        self.s.set("mode", i)
        self.mode_changed.emit(i)

    def _on_autoscroll(self, v):
        self.s.set("autoscroll", v)
        self.autoscroll_changed.emit(v)

    def _on_ts(self, v):
        self.s.set("show_ts", v)
        self.ts_changed.emit(v)

    def _on_interval(self, v):
        self.s.set("ts_interval", v)
        self.interval_changed.emit(v)

    # ---------- 连接状态 ----------
    def params(self) -> dict:
        return {
            "port": self.port_combo.currentText().strip(),
            "baud": int(self.baud_combo.currentText() or 115200),
            "databits": int(self.data_combo.currentText()),
            "parity": self.parity_combo.currentText(),
            "stopbits": self.stop_combo.currentText(),
        }

    def set_connected(self, on: bool):
        if on:
            self.conn_btn.setText("断开串口")
            self.conn_btn.setObjectName("danger")
            self.badge.setText("已连接")
            self.badge.setStyleSheet(
                f"color:{theme.ACCENT};border:1px solid {theme.ACCENT};"
                "border-radius:9px;padding:2px 10px;font-size:11px;")
        else:
            self.conn_btn.setText("连接串口")
            self.conn_btn.setObjectName("accent")
            self.badge.setText("未连接")
            self.badge.setStyleSheet(
                f"color:{theme.TEXT_DIM};border:1px solid {theme.BORDER};"
                "border-radius:9px;padding:2px 10px;font-size:11px;")
        # 重新应用样式
        self.conn_btn.style().unpolish(self.conn_btn)
        self.conn_btn.style().polish(self.conn_btn)
        for w in (self.port_combo, self.baud_combo, self.data_combo,
                  self.parity_combo, self.stop_combo):
            w.setEnabled(not on)
