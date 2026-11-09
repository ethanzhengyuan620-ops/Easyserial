# -*- coding: utf-8 -*-
"""规则C 编辑器:正则 + 颜色 + 启用开关,拖拽排序,实时校验。"""
import re

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QListWidget,
                             QListWidgetItem, QPushButton, QLineEdit, QLabel,
                             QColorDialog, QCheckBox, QDialogButtonBox,
                             QMessageBox, QAbstractItemView)

ROLE = Qt.ItemDataRole.UserRole


class RuleItemDialog(QDialog):
    def __init__(self, rule, parent=None):
        super().__init__(parent)
        self.setWindowTitle("编辑规则")
        self.setMinimumWidth(380)
        self._color = rule.get("color", "#FF5252")
        lay = QVBoxLayout(self)
        lay.addWidget(QLabel("正则表达式"))
        self.pat_edit = QLineEdit(rule.get("pattern", ""))
        lay.addWidget(self.pat_edit)
        row = QHBoxLayout()
        self.color_btn = QPushButton()
        self.color_btn.setFixedSize(60, 28)
        self._paint_color()
        self.color_btn.clicked.connect(self._pick)
        self.en_chk = QCheckBox("启用")
        self.en_chk.setChecked(rule.get("enabled", True))
        row.addWidget(QLabel("颜色"))
        row.addWidget(self.color_btn)
        row.addStretch()
        row.addWidget(self.en_chk)
        lay.addLayout(row)
        btns = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok |
                                QDialogButtonBox.StandardButton.Cancel)
        btns.accepted.connect(self._accept)
        btns.rejected.connect(self.reject)
        lay.addWidget(btns)

    def _paint_color(self):
        self.color_btn.setStyleSheet(
            f"background:{self._color};border-radius:6px;border:1px solid #555;")

    def _pick(self):
        c = QColorDialog.getColor(QColor(self._color), self, "选择颜色")
        if c.isValid():
            self._color = c.name()
            self._paint_color()

    def _accept(self):
        try:
            re.compile(self.pat_edit.text())
        except re.error as e:
            QMessageBox.warning(self, "正则非法", f"无法保存:{e}")
            return
        if not self.pat_edit.text().strip():
            QMessageBox.warning(self, "提示", "正则表达式不能为空")
            return
        self.accept()

    def result_rule(self):
        return {"pattern": self.pat_edit.text(), "color": self._color,
                "enabled": self.en_chk.isChecked()}


class RuleEditor(QDialog):
    def __init__(self, rules, parent=None):
        super().__init__(parent)
        self.setWindowTitle("配色规则(上方规则优先,可拖拽排序)")
        self.resize(460, 380)
        lay = QVBoxLayout(self)
        self.listw = QListWidget()
        self.listw.setDragDropMode(QAbstractItemView.DragDropMode.InternalMove)
        self.listw.itemDoubleClicked.connect(lambda _: self._edit())
        lay.addWidget(self.listw)
        row = QHBoxLayout()
        for text, slot in (("＋ 添加", self._add), ("✏️ 编辑", self._edit),
                           ("🗑 删除", self._remove)):
            b = QPushButton(text)
            b.clicked.connect(slot)
            row.addWidget(b)
        row.addStretch()
        lay.addLayout(row)
        btns = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok |
                                QDialogButtonBox.StandardButton.Cancel)
        btns.accepted.connect(self.accept)
        btns.rejected.connect(self.reject)
        lay.addWidget(btns)
        for r in rules:
            self._append(r)

    def _append(self, rule):
        item = QListWidgetItem()
        item.setData(ROLE, rule)
        self._refresh_item(item)
        self.listw.addItem(item)

    @staticmethod
    def _refresh_item(item):
        r = item.data(ROLE)
        mark = "●" if r.get("enabled", True) else "○"
        item.setText(f"{mark}  {r['pattern']}")
        item.setForeground(QColor(r["color"]))

    def _add(self):
        dlg = RuleItemDialog({}, self)
        if dlg.exec():
            self._append(dlg.result_rule())

    def _edit(self):
        item = self.listw.currentItem()
        if not item:
            return
        dlg = RuleItemDialog(item.data(ROLE), self)
        if dlg.exec():
            item.setData(ROLE, dlg.result_rule())
            self._refresh_item(item)

    def _remove(self):
        row = self.listw.currentRow()
        if row >= 0:
            self.listw.takeItem(row)

    def rules(self):
        return [self.listw.item(i).data(ROLE) for i in range(self.listw.count())]
