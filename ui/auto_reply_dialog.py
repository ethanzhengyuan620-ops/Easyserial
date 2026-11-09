# -*- coding: utf-8 -*-
"""自动回复规则配置：接收格式/匹配方式 → 自定义回复。"""
from copy import deepcopy

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (QWidget, QDialog, QVBoxLayout, QHBoxLayout,
                             QFormLayout, QLabel, QLineEdit, QPlainTextEdit,
                             QComboBox, QCheckBox, QPushButton, QListWidget,
                             QListWidgetItem, QDialogButtonBox, QMessageBox)

from core.auto_reply import validate_rule
from .widgets import NoWheelSpinBox

ROLE = Qt.ItemDataRole.UserRole
MATCH_LABELS = {"exact": "完全匹配", "contains": "包含", "regex": "正则"}
EOL_LABELS = ["无", "\\r\\n", "\\r", "\\n"]


def _set_combo_data(combo, value):
    index = combo.findData(value)
    if index >= 0:
        combo.setCurrentIndex(index)


class AutoReplyRuleDialog(QDialog):
    def __init__(self, rule=None, parent=None):
        super().__init__(parent)
        rule = rule or {}
        self.setWindowTitle("自动回复规则")
        self.setMinimumWidth(500)
        root = QVBoxLayout(self)
        form = QFormLayout()

        self.name_edit = QLineEdit(rule.get("name", ""))
        self.name_edit.setPlaceholderText("例如：PING → PONG")
        form.addRow("规则名称", self.name_edit)

        self.enabled_chk = QCheckBox("启用这条规则")
        self.enabled_chk.setChecked(rule.get("enabled", True))
        form.addRow("", self.enabled_chk)

        self.rx_format = QComboBox()
        self.rx_format.addItem("ASCII 文本", "ASCII")
        self.rx_format.addItem("HEX 字节", "HEX")
        _set_combo_data(self.rx_format, rule.get("rx_format", "ASCII"))
        form.addRow("接收格式", self.rx_format)

        self.match_combo = QComboBox()
        for value, label in MATCH_LABELS.items():
            self.match_combo.addItem(label, value)
        _set_combo_data(self.match_combo, rule.get("match", "exact"))
        form.addRow("匹配方式", self.match_combo)

        self.case_chk = QCheckBox("区分大小写")
        self.case_chk.setChecked(rule.get("case_sensitive", True))
        form.addRow("", self.case_chk)

        self.pattern_edit = QLineEdit(rule.get("pattern", ""))
        self.pattern_edit.setPlaceholderText(
            "ASCII 如 PING；HEX 如 01 03；正则按所选格式的显示文本匹配")
        form.addRow("接收匹配", self.pattern_edit)

        self.tx_format = QComboBox()
        self.tx_format.addItem("ASCII 文本", "ASCII")
        self.tx_format.addItem("HEX 字节（不追加行尾）", "HEX")
        _set_combo_data(self.tx_format, rule.get("tx_format", "ASCII"))
        form.addRow("回复格式", self.tx_format)

        self.reply_edit = QPlainTextEdit(rule.get("reply", ""))
        self.reply_edit.setPlaceholderText("填写回复内容")
        self.reply_edit.setFixedHeight(80)
        form.addRow("回复内容", self.reply_edit)

        self.eol_combo = QComboBox()
        for index, label in enumerate(EOL_LABELS):
            self.eol_combo.addItem(label, index)
        _set_combo_data(self.eol_combo, int(rule.get("line_ending", 0)))
        form.addRow("ASCII 行尾", self.eol_combo)
        root.addLayout(form)

        self.tip = QLabel("HEX 匹配按原始字节进行；HEX 回复始终不会追加 0D 0A。")
        self.tip.setObjectName("dim")
        self.tip.setWordWrap(True)
        root.addWidget(self.tip)

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok |
                                   QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self._accept)
        buttons.rejected.connect(self.reject)
        root.addWidget(buttons)

        self.tx_format.currentIndexChanged.connect(self._sync_controls)
        self.rx_format.currentIndexChanged.connect(self._sync_controls)
        self.match_combo.currentIndexChanged.connect(self._sync_controls)
        self._sync_controls()

    def _sync_controls(self):
        is_hex_reply = self.tx_format.currentData() == "HEX"
        self.eol_combo.setEnabled(not is_hex_reply)
        case_relevant = (self.rx_format.currentData() == "ASCII" or
                         self.match_combo.currentData() == "regex")
        self.case_chk.setEnabled(case_relevant)

    def result_rule(self):
        return {
            "name": self.name_edit.text().strip(),
            "enabled": self.enabled_chk.isChecked(),
            "rx_format": self.rx_format.currentData(),
            "match": self.match_combo.currentData(),
            "case_sensitive": self.case_chk.isChecked(),
            "pattern": self.pattern_edit.text(),
            "tx_format": self.tx_format.currentData(),
            "reply": self.reply_edit.toPlainText(),
            "line_ending": self.eol_combo.currentData(),
        }

    def _accept(self):
        error = validate_rule(self.result_rule())
        if error:
            QMessageBox.warning(self, "规则无法保存", error)
            return
        self.accept()


class AutoReplyWidget(QWidget):
    def __init__(self, settings, parent=None):
        super().__init__(parent)
        self.s = settings
        root = QVBoxLayout(self)

        top = QHBoxLayout()
        self.master_chk = QCheckBox("启用自动回复")
        self.master_chk.setChecked(self.s.get("auto_reply_enabled", False))
        top.addWidget(self.master_chk)
        top.addStretch()
        top.addWidget(QLabel("最小回复间隔"))
        self.cooldown_spin = NoWheelSpinBox()
        self.cooldown_spin.setRange(0, 60000)
        self.cooldown_spin.setSuffix(" ms")
        self.cooldown_spin.setValue(self.s.get("auto_reply_cooldown", 100))
        self.cooldown_spin.setToolTip("用于防止设备回显回复内容时形成高速循环")
        top.addWidget(self.cooldown_spin)
        root.addLayout(top)

        root.addWidget(QLabel("按从上到下的顺序匹配，单个接收帧只执行第一条命中规则。"))
        self.listw = QListWidget()
        self.listw.itemDoubleClicked.connect(lambda _: self._edit())
        root.addWidget(self.listw, 1)

        row = QHBoxLayout()
        for text, slot in (("＋ 添加", self._add), ("✏ 编辑", self._edit),
                           ("删除", self._remove), ("↑", lambda: self._move(-1)),
                           ("↓", lambda: self._move(1))):
            button = QPushButton(text)
            button.clicked.connect(slot)
            row.addWidget(button)
        row.addStretch()
        root.addLayout(row)

        for rule in deepcopy(self.s.get("auto_reply_rules", [])):
            self._append(rule)

        self.master_chk.toggled.connect(
            lambda value: self.s.set("auto_reply_enabled", value))
        self.cooldown_spin.valueChanged.connect(
            lambda value: self.s.set("auto_reply_cooldown", value))

    def _append(self, rule, row=None):
        item = QListWidgetItem()
        item.setData(ROLE, rule)
        self._refresh_item(item)
        if row is None:
            self.listw.addItem(item)
        else:
            self.listw.insertItem(row, item)
        return item

    @staticmethod
    def _refresh_item(item):
        rule = item.data(ROLE)
        mark = "●" if rule.get("enabled", True) else "○"
        name = rule.get("name", "").strip() or "未命名规则"
        match = MATCH_LABELS.get(rule.get("match", "exact"), "匹配")
        pattern = str(rule.get("pattern", "")).replace("\n", "\\n")
        reply = str(rule.get("reply", "")).replace("\n", "\\n")
        item.setText(
            f"{mark} {name}  [{rule.get('rx_format', 'ASCII')} {match}] "
            f"{pattern}  →  [{rule.get('tx_format', 'ASCII')}] {reply}")

    def _save_rules(self):
        rules = [deepcopy(self.listw.item(i).data(ROLE))
                 for i in range(self.listw.count())]
        self.s.set("auto_reply_rules", rules)

    def _add(self):
        dialog = AutoReplyRuleDialog(parent=self)
        if dialog.exec():
            item = self._append(dialog.result_rule())
            self.listw.setCurrentItem(item)
            self._save_rules()

    def _edit(self):
        item = self.listw.currentItem()
        if item is None:
            return
        dialog = AutoReplyRuleDialog(deepcopy(item.data(ROLE)), self)
        if dialog.exec():
            item.setData(ROLE, dialog.result_rule())
            self._refresh_item(item)
            self._save_rules()

    def _remove(self):
        row = self.listw.currentRow()
        if row >= 0:
            self.listw.takeItem(row)
            self._save_rules()

    def _move(self, delta):
        row = self.listw.currentRow()
        target = row + delta
        if row < 0 or not 0 <= target < self.listw.count():
            return
        item = self.listw.takeItem(row)
        self.listw.insertItem(target, item)
        self.listw.setCurrentRow(target)
        self._save_rules()
