# -*- coding: utf-8 -*-
"""工具箱:Modbus CRC16 / SHA256 / 时间戳转换(新增)。"""
import hashlib
import time
from datetime import datetime, timezone

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QTabWidget,
                             QWidget, QLabel, QLineEdit, QPushButton,
                             QPlainTextEdit, QFileDialog, QRadioButton,
                             QButtonGroup, QGroupBox, QFormLayout,
                             QComboBox, QCheckBox, QApplication)

from core.color_engine import parse_hex
from .auto_reply_dialog import AutoReplyWidget


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


class ToolsDialog(QDialog):
    def __init__(self, settings=None, parent=None):
        super().__init__(parent)
        self.setWindowFlags(Qt.WindowType.Window |
                            Qt.WindowType.WindowMinMaxButtonsHint |
                            Qt.WindowType.WindowCloseButtonHint)
        self.setWindowModality(Qt.WindowModality.NonModal)
        self.setWindowTitle("工具箱")
        self.resize(680, 540)
        lay = QVBoxLayout(self)
        tabs = QTabWidget()
        tabs.addTab(self._ascii_hex_tab(), "ASCII ↔ HEX")
        tabs.addTab(self._crc_tab(), "Modbus CRC16")
        tabs.addTab(self._sha_tab(), "SHA256")
        tabs.addTab(self._ts_tab(), "时间戳转换")
        if settings is not None:
            tabs.addTab(AutoReplyWidget(settings), "自动回复")
        lay.addWidget(tabs)

    # ================= ASCII / HEX =================
    def _ascii_hex_tab(self):
        widget = QWidget()
        layout = QVBoxLayout(widget)
        options = QHBoxLayout()
        options.addWidget(QLabel("文本编码"))
        self.convert_encoding = QComboBox()
        self.convert_encoding.addItem("ASCII", "ascii")
        self.convert_encoding.addItem("UTF-8", "utf-8")
        self.convert_encoding.addItem("GBK", "gbk")
        options.addWidget(self.convert_encoding)
        self.convert_spaces = QCheckBox("HEX 字节间加空格")
        self.convert_spaces.setChecked(True)
        options.addWidget(self.convert_spaces)
        self.convert_escapes = QCheckBox(r"识别/显示 \r \n \t \0 \xNN")
        self.convert_escapes.setChecked(True)
        options.addWidget(self.convert_escapes)
        options.addStretch()
        layout.addLayout(options)

        ascii_head = QHBoxLayout()
        ascii_head.addWidget(QLabel("ASCII / 文本"))
        ascii_head.addStretch()
        copy_ascii = QPushButton("复制文本")
        copy_ascii.clicked.connect(
            lambda: self._copy_converter(self.ascii_text, "文本"))
        ascii_head.addWidget(copy_ascii)
        layout.addLayout(ascii_head)
        self.ascii_text = QPlainTextEdit()
        self.ascii_text.setPlaceholderText(
            r"输入 ASCII 文本；支持 \r、\n、\t、\0、\xNN 转义")
        layout.addWidget(self.ascii_text, 1)

        actions = QHBoxLayout()
        to_hex = QPushButton("ASCII → HEX")
        to_hex.setObjectName("accent")
        to_hex.clicked.connect(self._ascii_to_hex)
        to_ascii = QPushButton("HEX → ASCII")
        to_ascii.clicked.connect(self._hex_to_ascii)
        clear = QPushButton("清空")
        clear.clicked.connect(self._clear_converter)
        actions.addStretch()
        actions.addWidget(to_hex)
        actions.addWidget(to_ascii)
        actions.addWidget(clear)
        actions.addStretch()
        layout.addLayout(actions)

        hex_head = QHBoxLayout()
        hex_head.addWidget(QLabel("HEX"))
        hex_head.addStretch()
        copy_hex = QPushButton("复制 HEX")
        copy_hex.clicked.connect(
            lambda: self._copy_converter(self.hex_text, "HEX"))
        hex_head.addWidget(copy_hex)
        layout.addLayout(hex_head)
        self.hex_text = QPlainTextEdit()
        self.hex_text.setPlaceholderText(
            "支持 48 65 6C 6C 6F、48656C6C6F、0x48,0x65 等格式")
        layout.addWidget(self.hex_text, 1)

        self.convert_status = QLabel(
            "选择编码后，可从任意一侧向另一侧转换。")
        self.convert_status.setObjectName("dim")
        layout.addWidget(self.convert_status)
        return widget

    def _converter_encoding(self):
        return self.convert_encoding.currentData()

    def _set_converter_status(self, text, ok=True):
        color = "#00C853" if ok else "#FF5252"
        self.convert_status.setText(text)
        self.convert_status.setStyleSheet(f"color:{color};")

    def _ascii_to_hex(self):
        try:
            text = self.ascii_text.toPlainText()
            if self.convert_escapes.isChecked():
                data = self._encode_converter_escapes(
                    text, self._converter_encoding())
            else:
                data = text.encode(
                    self._converter_encoding(), errors="strict")
        except UnicodeEncodeError as exc:
            self._set_converter_status(
                f"文本不能用 {self.convert_encoding.currentText()} 编码：{exc}",
                False)
            return
        separator = " " if self.convert_spaces.isChecked() else ""
        self.hex_text.setPlainText(separator.join(f"{byte:02X}" for byte in data))
        self._set_converter_status(f"ASCII → HEX：{len(data)} 字节")

    def _hex_to_ascii(self):
        data = parse_hex(self.hex_text.toPlainText())
        if data is None:
            self._set_converter_status(
                "HEX 格式错误：只能包含成对的十六进制字符和常用分隔符。",
                False)
            return
        try:
            text = data.decode(self._converter_encoding(), errors="strict")
        except UnicodeDecodeError as exc:
            self._set_converter_status(
                f"HEX 不能按 {self.convert_encoding.currentText()} 解码：{exc}",
                False)
            return
        if self.convert_escapes.isChecked():
            text = self._display_converter_escapes(text)
        self.ascii_text.setPlainText(text)
        self._set_converter_status(f"HEX → ASCII：{len(data)} 字节")

    @staticmethod
    def _encode_converter_escapes(text, encoding):
        output = bytearray()
        escapes = {"r": 0x0D, "n": 0x0A, "t": 0x09,
                   "0": 0x00, "\\": 0x5C}
        index = 0
        while index < len(text):
            if text[index] != "\\" or index + 1 >= len(text):
                output.extend(text[index].encode(encoding, errors="strict"))
                index += 1
                continue
            code = text[index + 1]
            if code in escapes:
                output.append(escapes[code])
                index += 2
            elif (code == "x" and index + 3 < len(text)
                  and all(ch in "0123456789abcdefABCDEF"
                          for ch in text[index + 2:index + 4])):
                output.append(int(text[index + 2:index + 4], 16))
                index += 4
            else:
                output.extend("\\".encode(encoding))
                index += 1
        return bytes(output)

    @staticmethod
    def _display_converter_escapes(text):
        result = []
        escapes = {"\\": r"\\", "\r": r"\r", "\n": r"\n",
                   "\t": r"\t", "\0": r"\0"}
        for char in text:
            if char in escapes:
                result.append(escapes[char])
            elif ord(char) < 32 or ord(char) == 127:
                result.append(rf"\x{ord(char):02X}")
            else:
                result.append(char)
        return "".join(result)

    def _copy_converter(self, editor, name):
        QApplication.clipboard().setText(editor.toPlainText())
        self._set_converter_status(f"已复制{name}")

    def _clear_converter(self):
        self.ascii_text.clear()
        self.hex_text.clear()
        self._set_converter_status("已清空")

    # ================= CRC16 =================
    def _crc_tab(self):
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.addWidget(QLabel("十六进制输入(如 01 03 00 00 00 0A):"))
        self.crc_in = QLineEdit()
        lay.addWidget(self.crc_in)
        btn = QPushButton("计算 CRC")
        btn.setObjectName("accent")
        btn.clicked.connect(self._calc_crc)
        lay.addWidget(btn)
        self.crc_out = QPlainTextEdit()
        self.crc_out.setReadOnly(True)
        lay.addWidget(self.crc_out)
        return w

    def _calc_crc(self):
        data = parse_hex(self.crc_in.text())
        if data is None or not data:
            self.crc_out.setPlainText("输入非法:请输入偶数个十六进制字符")
            return
        crc = crc16_modbus(data)
        lo, hi = crc & 0xFF, (crc >> 8) & 0xFF
        full = data + bytes([lo, hi])
        self.crc_out.setPlainText(
            f"CRC16   : 0x{crc:04X}\n"
            f"低字节  : 0x{lo:02X}\n"
            f"高字节  : 0x{hi:02X}\n"
            f"完整帧  : {' '.join(f'{b:02X}' for b in full)}")

    # ================= SHA256 =================
    def _sha_tab(self):
        w = QWidget()
        lay = QVBoxLayout(w)
        row = QHBoxLayout()
        self.sha_text_rb = QRadioButton("文本")
        self.sha_hex_rb = QRadioButton("十六进制")
        self.sha_text_rb.setChecked(True)
        g = QButtonGroup(w)
        g.addButton(self.sha_text_rb)
        g.addButton(self.sha_hex_rb)
        row.addWidget(self.sha_text_rb)
        row.addWidget(self.sha_hex_rb)
        row.addStretch()
        file_btn = QPushButton("📄 计算文件…")
        file_btn.clicked.connect(self._sha_file)
        row.addWidget(file_btn)
        lay.addLayout(row)
        self.sha_in = QPlainTextEdit()
        self.sha_in.setPlaceholderText("输入内容…")
        lay.addWidget(self.sha_in)
        btn = QPushButton("计算 SHA256")
        btn.setObjectName("accent")
        btn.clicked.connect(self._calc_sha)
        lay.addWidget(btn)
        self.sha_out = QLineEdit()
        self.sha_out.setReadOnly(True)
        lay.addWidget(self.sha_out)
        return w

    def _calc_sha(self):
        text = self.sha_in.toPlainText()
        if self.sha_hex_rb.isChecked():
            data = parse_hex(text)
            if data is None:
                self.sha_out.setText("十六进制输入非法")
                return
        else:
            data = text.encode("utf-8")
        self.sha_out.setText(hashlib.sha256(data).hexdigest())

    def _sha_file(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择文件")
        if not path:
            return
        h = hashlib.sha256()
        try:
            with open(path, "rb") as f:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    h.update(chunk)
            self.sha_out.setText(h.hexdigest())
        except Exception as e:
            self.sha_out.setText(f"读取失败: {e}")

    # ================= 时间戳转换(新增) =================
    def _ts_tab(self):
        w = QWidget()
        lay = QVBoxLayout(w)

        # ---- Unix 时间戳 → 日期 ----
        g1 = QGroupBox("Unix 时间戳 → 日期时间")
        f1 = QFormLayout(g1)
        row = QHBoxLayout()
        self.ts_in = QLineEdit()
        self.ts_in.setPlaceholderText("支持秒(10位)/毫秒(13位),自动识别")
        now_btn = QPushButton("当前")
        now_btn.setObjectName("tiny")
        now_btn.clicked.connect(
            lambda: (self.ts_in.setText(str(int(time.time() * 1000))),
                     self._ts_to_date()))
        row.addWidget(self.ts_in)
        row.addWidget(now_btn)
        f1.addRow("时间戳", row)
        b1 = QPushButton("转 换")
        b1.setObjectName("accent")
        b1.clicked.connect(self._ts_to_date)
        f1.addRow(b1)
        self.ts_local = QLineEdit(readOnly=True)
        self.ts_utc = QLineEdit(readOnly=True)
        f1.addRow("本地时间", self.ts_local)
        f1.addRow("UTC 时间", self.ts_utc)
        lay.addWidget(g1)

        # ---- 日期 → Unix 时间戳 ----
        g2 = QGroupBox("日期时间 → Unix 时间戳")
        f2 = QFormLayout(g2)
        row2 = QHBoxLayout()
        self.date_in = QLineEdit()
        self.date_in.setPlaceholderText("YYYY-MM-DD HH:MM:SS[.mmm]")
        now2_btn = QPushButton("当前")
        now2_btn.setObjectName("tiny")
        now2_btn.clicked.connect(
            lambda: (self.date_in.setText(
                datetime.now().strftime("%Y-%m-%d %H:%M:%S")),
                self._date_to_ts()))
        row2.addWidget(self.date_in)
        row2.addWidget(now2_btn)
        f2.addRow("日期(本地)", row2)
        b2 = QPushButton("转 换")
        b2.setObjectName("accent")
        b2.clicked.connect(self._date_to_ts)
        f2.addRow(b2)
        self.date_sec = QLineEdit(readOnly=True)
        self.date_ms = QLineEdit(readOnly=True)
        f2.addRow("秒级", self.date_sec)
        f2.addRow("毫秒级", self.date_ms)
        lay.addWidget(g2)
        lay.addStretch()
        return w

    def _ts_to_date(self):
        s = self.ts_in.text().strip()
        try:
            v = float(s)
            if v > 1e11:          # 毫秒
                v /= 1000.0
            local = datetime.fromtimestamp(v)
            utc = datetime.fromtimestamp(v, tz=timezone.utc)
            self.ts_local.setText(local.strftime("%Y-%m-%d %H:%M:%S.%f")[:-3])
            self.ts_utc.setText(utc.strftime("%Y-%m-%d %H:%M:%S.%f")[:-3])
        except Exception:
            self.ts_local.setText("输入非法")
            self.ts_utc.setText("")

    def _date_to_ts(self):
        s = self.date_in.text().strip()
        dt = None
        for fmt in ("%Y-%m-%d %H:%M:%S.%f", "%Y-%m-%d %H:%M:%S",
                    "%Y-%m-%d %H:%M", "%Y-%m-%d", "%Y/%m/%d %H:%M:%S"):
            try:
                dt = datetime.strptime(s, fmt)
                break
            except ValueError:
                continue
        if dt is None:
            self.date_sec.setText("输入非法,格式如 2026-06-12 08:30:00")
            self.date_ms.setText("")
            return
        ts = dt.timestamp()
        self.date_sec.setText(str(int(ts)))
        self.date_ms.setText(str(int(ts * 1000)))
