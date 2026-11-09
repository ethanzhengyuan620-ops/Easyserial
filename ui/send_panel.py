# -*- coding: utf-8 -*-
"""数据发送功能区(新增)。

布局:
  ┌──────────────────────────────────────────────────────┐
  │ 发送格式 [ASCII][HEX]   行尾 [无][\\r\\n][\\r][\\n]        │
  │ ┌──────────────────────────────────────────────────┐ │
  │ │ 多行输入框(等宽字体)                              │ │
  │ └──────────────────────────────────────────────────┘ │
  │ [定时发送⬤] [500 ms]                  [   发送   ]    │
  │ [AT] [版本] [复位] [读寄存器] [心跳] [自定义]  ← 右键编辑 │
  └──────────────────────────────────────────────────────┘
"""
from PyQt6.QtCore import Qt, QTimer, pyqtSignal
from PyQt6.QtGui import QFont
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                             QPlainTextEdit, QPushButton, QDialog, QLineEdit,
                             QCheckBox, QDialogButtonBox, QFormLayout, QMenu)

from core.color_engine import parse_hex
from .widgets import PillGroup, ToggleSwitch, NoWheelSpinBox

LINE_ENDINGS = [b"", b"\r\n", b"\r", b"\n"]


class SendInput(QPlainTextEdit):
    """发送输入框:支持 Ctrl+滚轮 独立缩放字号。"""
    zoom_request = pyqtSignal(int)

    def wheelEvent(self, ev):
        if ev.modifiers() & Qt.KeyboardModifier.ControlModifier:
            self.zoom_request.emit(1 if ev.angleDelta().y() > 0 else -1)
            ev.accept()
            return
        super().wheelEvent(ev)


class QuickCmdDialog(QDialog):
    """右键编辑快捷指令。"""

    def __init__(self, cmd, parent=None):
        super().__init__(parent)
        self.setWindowTitle("编辑快捷指令")
        self.setMinimumWidth(360)
        form = QFormLayout(self)
        self.name_edit = QLineEdit(cmd.get("name", ""))
        self.data_edit = QLineEdit(cmd.get("data", ""))
        self.hex_chk = QCheckBox("HEX 格式")
        self.hex_chk.setChecked(cmd.get("hex", False))
        form.addRow("名称", self.name_edit)
        form.addRow("内容", self.data_edit)
        form.addRow("", self.hex_chk)
        btns = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok |
                                QDialogButtonBox.StandardButton.Cancel)
        btns.accepted.connect(self.accept)
        btns.rejected.connect(self.reject)
        form.addRow(btns)

    def result_cmd(self):
        return {"name": self.name_edit.text().strip() or "未命名",
                "data": self.data_edit.text(),
                "hex": self.hex_chk.isChecked()}


class SendPanel(QWidget):
    send_bytes = pyqtSignal(bytes)      # 主窗口接收后写串口
    hint = pyqtSignal(str)              # 状态栏提示(如 HEX 非法)

    def __init__(self, settings, parent=None):
        super().__init__(parent)
        self.s = settings
        root = QVBoxLayout(self)
        root.setContentsMargins(10, 8, 10, 8)
        root.setSpacing(6)

        # ---- 第一行:发送格式 + 行尾符 ----
        row1 = QHBoxLayout()
        lab1 = QLabel("发送格式")
        lab1.setObjectName("dim")
        self.fmt_pills = PillGroup(["ASCII", "HEX"])
        self.fmt_pills.set_current(1 if self.s.get("send_hex") else 0)
        self.fmt_pills.changed.connect(self._on_format_changed)
        self.eol_label = QLabel("行尾符")
        self.eol_label.setObjectName("dim")
        self.eol_pills = PillGroup(["无", "\\r\\n", "\\r", "\\n"])
        self.eol_pills.set_current(self.s.get("line_ending"))
        self.eol_pills.changed.connect(lambda i: self.s.set("line_ending", i))
        row1.addWidget(lab1)
        row1.addWidget(self.fmt_pills)
        row1.addSpacing(16)
        row1.addWidget(self.eol_label)
        row1.addWidget(self.eol_pills)
        row1.addStretch()
        root.addLayout(row1)
        self._sync_eol_state()

        # ---- 输入框(Ctrl+滚轮独立缩放) ----
        self.input = SendInput()
        self.input.setObjectName("sendInput")
        self.input.setPlaceholderText("输入发送内容...")
        self._font_size = self.s.get("font_size")
        self._apply_input_font()
        self.input.zoom_request.connect(self._zoom_input)
        self.input.setMinimumHeight(48)
        root.addWidget(self.input, 1)

        # ---- 发送按钮行 ----
        row2 = QHBoxLayout()
        lab3 = QLabel("定时发送")
        lab3.setObjectName("dim")
        self.timer_toggle = ToggleSwitch()
        self.interval_spin = NoWheelSpinBox()
        self.interval_spin.setRange(10, 3600000)
        self.interval_spin.setValue(self.s.get("timer_interval"))
        self.interval_spin.setSuffix(" ms")
        self.interval_spin.setFixedWidth(110)
        self.interval_spin.setVisible(False)
        self.send_btn = QPushButton("发  送")
        self.send_btn.setObjectName("accent")
        self.send_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.send_btn.setFixedWidth(140)
        row2.addWidget(lab3)
        row2.addWidget(self.timer_toggle)
        row2.addWidget(self.interval_spin)
        row2.addStretch()
        row2.addWidget(self.send_btn)
        root.addLayout(row2)

        # ---- 快捷发送栏 ----
        row3 = QHBoxLayout()
        row3.setSpacing(6)
        lab4 = QLabel("快捷")
        lab4.setObjectName("dim")
        row3.addWidget(lab4)
        self.quick_btns = []
        for i, cmd in enumerate(self.s.get("quick_cmds")[:6]):
            btn = QPushButton(cmd.get("name", f"快捷{i+1}"))
            btn.setObjectName("quick")
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
            btn.setToolTip("左键发送,右键编辑")
            btn.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
            btn.clicked.connect(lambda _, idx=i: self._send_quick(idx))
            btn.customContextMenuRequested.connect(
                lambda pos, idx=i, b=btn: self._edit_quick(idx, b, pos))
            self.quick_btns.append(btn)
            row3.addWidget(btn)
        row3.addStretch()
        root.addLayout(row3)

        # ---- 定时器 ----
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._do_send)
        self.timer_toggle.toggled.connect(self._on_timer_toggle)
        self.interval_spin.valueChanged.connect(self._on_interval_changed)
        self.send_btn.clicked.connect(self._do_send)

    # ---------- 发送逻辑 ----------
    # ================= 字号 =================
    def _apply_input_font(self):
        """字体写进控件级 QSS(全局 QSS 的 13px 会覆盖 setFont,
        控件级 QSS 优先级最高);#sendInput 的背景/圆角等仍走全局 QSS,
        这里只设字体,互不冲突。"""
        f = QFont(self.s.get("font_family"), self._font_size)
        f.setStyleHint(QFont.StyleHint.Monospace)
        self.input.setFont(f)
        self.input.setStyleSheet(
            f"QPlainTextEdit#sendInput{{"
            f"font-family:'{self.s.get('font_family')}';"
            f"font-size:{self._font_size}pt;}}")

    def _zoom_input(self, step: int):
        self._font_size = max(6, min(36, self._font_size + step))
        self._apply_input_font()

    def apply_appearance(self):
        """A± 基准字号 / 外观设置变更:重置到 settings 的 font_size。"""
        self._font_size = self.s.get("font_size")
        self._apply_input_font()

    def build_payload(self):
        """构造待发送字节;HEX 固定发送原始字节,不追加文本行尾。"""
        text = self.input.toPlainText()
        if self.fmt_pills.current() == 1:                  # HEX
            data = parse_hex(text)
            if data is None:
                return None
            return data
        else:                                              # ASCII
            data = text.encode("utf-8")
            return data + LINE_ENDINGS[self.eol_pills.current()]

    def _do_send(self):
        payload = self.build_payload()
        if payload is None:
            self.hint.emit("HEX 格式非法:请输入偶数个十六进制字符,如 01 03 0A")
            if self._timer.isActive():
                self.timer_toggle.setChecked(False)
            return
        if not payload:
            return
        self.send_bytes.emit(payload)

    def _send_quick(self, idx):
        cmd = self.s.get("quick_cmds")[idx]
        if cmd.get("hex"):
            data = parse_hex(cmd.get("data", ""))
            if data is None:
                self.hint.emit(f"快捷指令「{cmd.get('name')}」HEX 内容非法")
                return
        else:
            data = cmd.get("data", "").encode("utf-8")
        if not data:
            self.hint.emit(f"快捷指令「{cmd.get('name')}」内容为空,右键可编辑")
            return
        # 二进制快捷指令必须保持原始帧,不能混入文本协议的 CR/LF。
        if not cmd.get("hex"):
            data += LINE_ENDINGS[self.eol_pills.current()]
        self.send_bytes.emit(data)

    def _edit_quick(self, idx, btn, pos):
        menu = QMenu(self)
        act = menu.addAction("✏️  编辑指令")
        if menu.exec(btn.mapToGlobal(pos)) == act:
            cmds = self.s.get("quick_cmds")
            dlg = QuickCmdDialog(cmds[idx], self)
            if dlg.exec():
                cmds[idx] = dlg.result_cmd()
                self.s.set("quick_cmds", cmds)
                btn.setText(cmds[idx]["name"])

    # ---------- 定时发送 ----------
    def _on_format_changed(self, index):
        self.s.set("send_hex", index == 1)
        self._sync_eol_state()

    def _sync_eol_state(self):
        is_hex = self.fmt_pills.current() == 1
        self.eol_pills.setEnabled(not is_hex)
        if is_hex:
            self.eol_label.setText("行尾符（HEX 不追加）")
            self.eol_pills.setToolTip("HEX 模式固定发送原始字节，不追加 CR/LF")
        else:
            self.eol_label.setText("行尾符")
            self.eol_pills.setToolTip("")

    def _on_timer_toggle(self, on):
        self.interval_spin.setVisible(on)
        if on:
            self._timer.start(self.interval_spin.value())
            self._do_send()
        else:
            self._timer.stop()

    def _on_interval_changed(self, v):
        self.s.set("timer_interval", v)
        if self._timer.isActive():
            self._timer.start(v)

    def stop_timer(self):
        """断开串口时由主窗口调用。"""
        if self.timer_toggle.isChecked():
            self.timer_toggle.setChecked(False)

    def set_send_enabled(self, enabled: bool):
        """仅限制发送动作；输入内容在未连接时仍可编辑。"""
        if not enabled:
            self.stop_timer()
        self.send_btn.setEnabled(enabled)
        self.timer_toggle.setEnabled(enabled)
        for btn in self.quick_btns:
            btn.setEnabled(enabled)
