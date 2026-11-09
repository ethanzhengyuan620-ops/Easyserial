# -*- coding: utf-8 -*-
"""VSCode 风格悬浮搜索栏(Ctrl+F)。实时高亮 + 防抖 + 计数 + 导航。"""
import re

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QColor, QTextCursor, QKeySequence, QShortcut
from PyQt6.QtWidgets import (QFrame, QHBoxLayout, QLineEdit, QPushButton,
                             QLabel, QTextEdit)

from . import theme

MAX_MATCHES = 5000


class SearchBar(QFrame):
    def __init__(self, get_edit, parent=None):
        """get_edit: 返回当前模式下目标 QPlainTextEdit 的回调。"""
        super().__init__(parent)
        self._get_edit = get_edit
        self._matches = []
        self._index = -1
        self.setStyleSheet(
            f"SearchBar{{background:{theme.CARD_BG};border:1px solid {theme.BORDER};"
            f"border-radius:8px;}}")
        self.setFixedHeight(40)

        lay = QHBoxLayout(self)
        lay.setContentsMargins(8, 4, 8, 4)
        lay.setSpacing(4)
        self.edit = QLineEdit()
        self.edit.setPlaceholderText("查找…")
        self.edit.setFixedWidth(180)
        self.case_btn = self._opt("Aa", "区分大小写")
        self.word_btn = self._opt("W", "全词匹配")
        self.re_btn = self._opt(".*", "正则模式")
        self.count_lab = QLabel("0 / 0")
        self.count_lab.setObjectName("dim")
        self.prev_btn = self._nav("↑")
        self.next_btn = self._nav("↓")
        self.close_btn = self._nav("✕")
        for w in (self.edit, self.case_btn, self.word_btn, self.re_btn,
                  self.count_lab, self.prev_btn, self.next_btn, self.close_btn):
            lay.addWidget(w)

        self._debounce = QTimer(self)
        self._debounce.setSingleShot(True)
        self._debounce.setInterval(150)
        self._debounce.timeout.connect(self.refresh)

        self.edit.textChanged.connect(self._debounce.start)
        for b in (self.case_btn, self.word_btn, self.re_btn):
            b.toggled.connect(lambda _: self.refresh())
        self.prev_btn.clicked.connect(lambda: self._goto(-1))
        self.next_btn.clicked.connect(lambda: self._goto(+1))
        self.close_btn.clicked.connect(self.hide_bar)
        self.edit.returnPressed.connect(lambda: self._goto(+1))
        self.hide()

    def _opt(self, text, tip):
        b = QPushButton(text)
        b.setObjectName("tiny")
        b.setCheckable(True)
        b.setToolTip(tip)
        b.setFixedWidth(32)
        return b

    def _nav(self, text):
        b = QPushButton(text)
        b.setObjectName("tiny")
        b.setFixedWidth(28)
        return b

    # ---------- 显示/隐藏 ----------
    def show_bar(self):
        self.show()
        self.raise_()
        self.edit.setFocus()
        self.edit.selectAll()
        self.refresh()

    def hide_bar(self):
        self.hide()
        ed = self._get_edit()
        if ed:
            ed.setExtraSelections([])
        self._matches = []
        self._index = -1

    def keyPressEvent(self, ev):
        if ev.key() == Qt.Key.Key_Escape:
            self.hide_bar()
            return
        if ev.key() == Qt.Key.Key_Return and \
                ev.modifiers() & Qt.KeyboardModifier.ShiftModifier:
            self._goto(-1)
            return
        super().keyPressEvent(ev)

    # ---------- 搜索 ----------
    def refresh(self):
        if not self.isVisible():
            return
        ed = self._get_edit()
        pattern = self.edit.text()
        self._matches = []
        self._index = -1
        if ed is None or not pattern:
            if ed:
                ed.setExtraSelections([])
            self.count_lab.setText("0 / 0")
            return

        if not self.re_btn.isChecked():
            pattern = re.escape(pattern)
        if self.word_btn.isChecked():
            pattern = r"\b" + pattern + r"\b"
        flags = 0 if self.case_btn.isChecked() else re.IGNORECASE
        try:
            cre = re.compile(pattern, flags)
        except re.error:
            self.count_lab.setText("正则非法")
            ed.setExtraSelections([])
            return

        text = ed.toPlainText()
        for m in cre.finditer(text):
            if m.start() == m.end():
                break
            self._matches.append((m.start(), m.end()))
            if len(self._matches) >= MAX_MATCHES:
                break
        if self._matches:
            self._index = 0
        self._paint(ed)

    def _paint(self, ed):
        sels = []
        for i, (a, b) in enumerate(self._matches):
            sel = QTextEdit.ExtraSelection()
            cur = QTextCursor(ed.document())
            cur.setPosition(a)
            cur.setPosition(b, QTextCursor.MoveMode.KeepAnchor)
            sel.cursor = cur
            sel.format.setBackground(
                QColor("#B7791F") if i == self._index else QColor("#5A4A12"))
            sel.format.setForeground(QColor("#FFFFFF"))
            sels.append(sel)
        ed.setExtraSelections(sels)
        n = len(self._matches)
        self.count_lab.setText(f"{self._index + 1 if n else 0} / {n}")
        if 0 <= self._index < n:
            a, b = self._matches[self._index]
            cur = QTextCursor(ed.document())
            cur.setPosition(a)
            ed.setTextCursor(cur)
            ed.ensureCursorVisible()

    def _goto(self, step):
        if not self._matches:
            return
        self._index = (self._index + step) % len(self._matches)
        ed = self._get_edit()
        if ed:
            self._paint(ed)
