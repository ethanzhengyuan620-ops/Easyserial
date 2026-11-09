# -*- coding: utf-8 -*-
"""通用自定义控件:Toggle 开关 / Pill 按钮组 / 防滚轮 SpinBox。"""
from PyQt6.QtCore import Qt, pyqtSignal, QSize
from PyQt6.QtGui import QPainter, QColor
from PyQt6.QtWidgets import (QCheckBox, QWidget, QHBoxLayout, QPushButton,
                             QButtonGroup, QSpinBox, QComboBox)
from . import theme


class ToggleSwitch(QCheckBox):
    """与左侧面板一致的青色胶囊 Toggle 开关。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setFixedSize(46, 24)

    def sizeHint(self):
        return QSize(46, 24)

    def hitButton(self, pos):
        return self.contentsRect().contains(pos)

    def paintEvent(self, _ev):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(Qt.PenStyle.NoPen)
        track = QColor(theme.ACCENT) if self.isChecked() else QColor(theme.BORDER)
        p.setBrush(track)
        p.drawRoundedRect(0, 2, 46, 20, 10, 10)
        p.setBrush(QColor("#FFFFFF"))
        x = 26 if self.isChecked() else 4
        p.drawEllipse(x, 4, 16, 16)
        p.end()


class PillGroup(QWidget):
    """互斥 Pill 按钮组(与"显示模式"风格一致)。"""
    changed = pyqtSignal(int)

    def __init__(self, items, parent=None):
        super().__init__(parent)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(4)
        self._group = QButtonGroup(self)
        self._group.setExclusive(True)
        for i, text in enumerate(items):
            btn = QPushButton(text)
            btn.setObjectName("pill")
            btn.setCheckable(True)
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
            self._group.addButton(btn, i)
            lay.addButton = None
            lay.addWidget(btn)
        self._group.button(0).setChecked(True)
        self._group.idClicked.connect(self.changed.emit)

    def current(self) -> int:
        return self._group.checkedId()

    def set_current(self, idx: int):
        btn = self._group.button(idx)
        if btn:
            btn.setChecked(True)


class NoWheelSpinBox(QSpinBox):
    """禁止滚轮误改数值的 SpinBox。"""

    def wheelEvent(self, ev):
        ev.ignore()


class NoWheelComboBox(QComboBox):
    def wheelEvent(self, ev):
        ev.ignore()


def hline(parent=None):
    from PyQt6.QtWidgets import QFrame
    f = QFrame(parent)
    f.setObjectName("hline")
    f.setFrameShape(QFrame.Shape.NoFrame)
    return f
