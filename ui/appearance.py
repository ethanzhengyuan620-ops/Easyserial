# -*- coding: utf-8 -*-
"""外观设置面板(⚙):等宽字体选择 + 5 项颜色配置,即时生效。"""
from PyQt6.QtCore import pyqtSignal
from PyQt6.QtGui import QColor, QFontDatabase
from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel,
                             QComboBox, QPushButton, QColorDialog)

COLOR_ROWS = [
    ("📝 文字颜色", "col_text"),
    ("🎨 背景颜色", "col_bg"),
    ("🕐 时间戳颜色", "col_ts"),
    ("🔢 HEX字节颜色", "col_hex"),
    ("🔤 ASCII字符色", "col_ascii"),
]


class AppearanceDialog(QDialog):
    changed = pyqtSignal()

    def __init__(self, settings, parent=None):
        super().__init__(parent)
        self.s = settings
        self.setWindowTitle("外观设置")
        self.setMinimumWidth(300)
        lay = QVBoxLayout(self)

        row = QHBoxLayout()
        row.addWidget(QLabel("字体"))
        self.font_combo = QComboBox()
        mono = [f for f in QFontDatabase.families()
                if QFontDatabase.isFixedPitch(f)]
        self.font_combo.addItems(mono or QFontDatabase.families())
        cur = self.s.get("font_family")
        idx = self.font_combo.findText(cur)
        if idx >= 0:
            self.font_combo.setCurrentIndex(idx)
        self.font_combo.currentTextChanged.connect(self._on_font)
        row.addWidget(self.font_combo, 1)
        lay.addLayout(row)

        self._swatches = {}
        for label, key in COLOR_ROWS:
            r = QHBoxLayout()
            r.addWidget(QLabel(label))
            r.addStretch()
            btn = QPushButton()
            btn.setFixedSize(46, 24)
            self._swatches[key] = btn
            self._paint(key)
            btn.clicked.connect(lambda _, k=key: self._pick(k))
            r.addWidget(btn)
            lay.addLayout(r)

    def _paint(self, key):
        self._swatches[key].setStyleSheet(
            f"background:{self.s.get(key)};border:1px solid #555;"
            "border-radius:5px;")

    def _pick(self, key):
        c = QColorDialog.getColor(QColor(self.s.get(key)), self, "选择颜色")
        if c.isValid():
            self.s.set(key, c.name())
            self._paint(key)
            self.changed.emit()

    def _on_font(self, family):
        self.s.set("font_family", family)
        self.changed.emit()
