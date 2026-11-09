# -*- coding: utf-8 -*-
"""日志显示区(核心组件)。

性能要点(解决"数据多了卡死"):
  1. 全部使用 QPlainTextEdit(比 QTextEdit 快一个数量级);
  2. 收帧只进缓冲队列,30ms 定时批量刷一次 UI(beginEditBlock 合并重绘);
  3. 纯HEX 每帧一个独立段落,交给 Qt 按控件宽度原生折行 —— 缩放窗口即时
     重排、无横向滚动条;单帧长度受分帧/MAX_FRAME 限制,不会因超长段落卡顿;
  4. setMaximumBlockCount 原生自动裁剪旧行,O(1) 删除,无手动 cursor 操作;
  5. 字体只设在控件级别(QTextCharFormat 只存颜色),改字号全文瞬间生效,零重格式化。

字号缩放(v2.0.1 修复):
  - 根因:theme.py 全局 QSS 写了 font-size:13px,Qt 中样式表字体优先级
    高于 setFont() → 之前 setFont 被压住,页面字号永远不变。
    修复 = 字体同时写进【控件级 QSS】(控件级 > 应用级,必定生效),
    并保留 setFont 以保证 fontMetrics / _recalc_bpl 计算准确。
  - Ctrl+滚轮 = 分窗格缩放:鼠标悬停在哪个窗格,只改那个窗格的字号
    (HEX 双栏左右两栏必须同字号才能逐行对齐,故视为一个整体);
  - 右上角 A−/A+ = 基准字号:一键把所有窗格统一重置到该字号。

HEX双栏(定宽列,需保持左右逐行对齐):
  - 左栏字节数/行 按栏宽自动计算(8 的倍数);窗口缩放后用历史帧按新宽度
    整体重排(_rebuild_hex,防抖),已显示数据也随之折行,且无横向滚动条;
  - 右栏 ASCII:每个字节恒定映射为 1 个字符(不可见字节显示 '·'),
    且与左栏使用同一等宽字体 → 两栏逐行严格对齐,不再错位;
  - 时间戳行两栏同时插入,行数永远一致,自动裁剪后依然对齐。
"""
from datetime import datetime

from PyQt6.QtCore import Qt, QTimer, pyqtSignal
from PyQt6.QtGui import QFont, QTextCharFormat, QTextCursor, QColor, QFontMetricsF
from PyQt6.QtWidgets import (QWidget, QStackedLayout, QPlainTextEdit, QSplitter,
                             QVBoxLayout)

from core import color_engine

MODE_ASCII, MODE_HEXDUAL, MODE_PUREHEX = 0, 1, 2
FLUSH_MS = 30


class LogEdit(QPlainTextEdit):
    zoom_request = pyqtSignal(int)   # +1 / -1

    def __init__(self, wrap=False, parent=None):
        super().__init__(parent)
        self.setReadOnly(True)
        self.setUndoRedoEnabled(False)
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.WidgetWidth if wrap
                             else QPlainTextEdit.LineWrapMode.NoWrap)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOn)

    def wheelEvent(self, ev):
        if ev.modifiers() & Qt.KeyboardModifier.ControlModifier:
            self.zoom_request.emit(1 if ev.angleDelta().y() > 0 else -1)
            ev.accept()
            return
        super().wheelEvent(ev)


class ReceiveView(QWidget):

    def __init__(self, settings, parent=None):
        super().__init__(parent)
        self.s = settings

        # ---- 三种模式的控件 ----
        self.ascii_edit = LogEdit(wrap=True)
        self.hex_left = LogEdit()
        self.hex_right = LogEdit()
        self.pure_edit = LogEdit(wrap=True)   # 纯HEX 交给 Qt 按宽度自动折行
        # HEX 双栏:每行字节数随宽度自适应 → 内容永不超宽,关掉横向滚动条
        self.hex_left.setHorizontalScrollBarPolicy(
            Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.hex_right.setHorizontalScrollBarPolicy(
            Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        self.hex_split = QSplitter(Qt.Orientation.Horizontal)
        self.hex_split.addWidget(self.hex_left)
        self.hex_split.addWidget(self.hex_right)
        self.hex_split.setSizes([620, 280])
        self.hex_split.setHandleWidth(2)

        self._stack = QStackedLayout(self)
        self._stack.setContentsMargins(0, 0, 0, 0)
        self._stack.addWidget(self.ascii_edit)
        self._stack.addWidget(self.hex_split)
        self._stack.addWidget(self.pure_edit)

        # ---- 分窗格字号:滚轮滚到哪,哪个窗格变(HEX双栏为一个整体) ----
        base = self.s.get("font_size")
        self.pane_sizes = {"ascii": base, "hex": base, "pure": base}
        self.ascii_edit.zoom_request.connect(lambda d: self.zoom_pane("ascii", d))
        self.hex_left.zoom_request.connect(lambda d: self.zoom_pane("hex", d))
        self.hex_right.zoom_request.connect(lambda d: self.zoom_pane("hex", d))
        self.pure_edit.zoom_request.connect(lambda d: self.zoom_pane("pure", d))

        # ---- HEX双栏滚动同步(防循环标志位) ----
        self._syncing = False
        self.hex_left.verticalScrollBar().valueChanged.connect(
            lambda v: self._sync_scroll(self.hex_right, v))
        self.hex_right.verticalScrollBar().valueChanged.connect(
            lambda v: self._sync_scroll(self.hex_left, v))

        # ---- 状态 ----
        self._pending = []           # [(bytes|str, datetime, "RX"|"TX"|"NOTE")]
        self._frames = []            # 原始收发帧历史(供 HEX 重排/模式切换)
        self._max_frames = 5000
        self._last_ts = None
        self._pure_started = False   # 纯HEX:是否已写入过内容(决定是否补换行)
        self._hex_bpl = 16           # HEX双栏 每行字节数(随宽度自适应)
        self._rules = color_engine.compile_rules(self.s.get("rules"))

        self._timer = QTimer(self)
        self._timer.setInterval(FLUSH_MS)
        self._timer.timeout.connect(self._flush)

        # 窗口/字号变化后按新宽度重排 HEX 双栏(防抖,拖动停止后才重排)
        self._rebuild_timer = QTimer(self)
        self._rebuild_timer.setSingleShot(True)
        self._rebuild_timer.setInterval(120)
        self._rebuild_timer.timeout.connect(self._rebuild_hex)

        # ---- 配置 ----
        self.mode = self.s.get("mode")
        self.autoscroll = self.s.get("autoscroll")
        self.show_ts = self.s.get("show_ts")
        self.ts_interval = self.s.get("ts_interval")
        self._stack.setCurrentIndex(self.mode)

        self.apply_appearance()
        self.set_max_lines(self.s.get("max_lines"))

    # ================= 公共接口 =================
    def append_frame(self, data: bytes, ts: datetime):
        self._pending.append((data, ts, "RX"))
        if not self._timer.isActive():
            self._timer.start()

    def append_tx(self, data: bytes, ts: datetime = None):
        """回显已由串口接受写入的字节，并明确标记为 TX。"""
        self._pending.append((data, ts or datetime.now(), "TX"))
        if not self._timer.isActive():
            self._timer.start()

    def append_note(self, text: str, ts: datetime = None):
        """在所有显示模式中插入协议解析摘要，不把文字伪装成串口字节。"""
        self._pending.append((str(text), ts or datetime.now(), "NOTE"))
        if not self._timer.isActive():
            self._timer.start()

    def set_mode(self, mode: int):
        self.mode = mode
        self._stack.setCurrentIndex(mode)
        self._recalc_bpl()
        # 切到 HEX 模式时,用历史帧按当前宽度重排,避免切过去是空白
        if mode in (MODE_HEXDUAL, MODE_PUREHEX):
            self._rebuild_hex()

    def set_rules(self, rules):
        self._rules = color_engine.compile_rules(rules)

    def clear_all(self):
        for e in self._edits():
            e.clear()
        self._pending.clear()
        self._frames.clear()
        self._last_ts = None
        self._pure_started = False

    def set_max_lines(self, n: int):
        n = max(200, n)
        self._max_frames = n
        for e in self._edits():
            e.setMaximumBlockCount(n)

    def current_edit(self) -> QPlainTextEdit:
        """搜索栏的目标控件。"""
        return (self.ascii_edit, self.hex_left, self.pure_edit)[self.mode]

    def apply_appearance(self):
        """外观设置 / A± 基准字号:所有窗格统一重置到 settings 的 font_size。"""
        base = self.s.get("font_size")
        for k in self.pane_sizes:
            self.pane_sizes[k] = base
        for e in self._edits():
            self._style_edit(e, base)
        self._recalc_bpl()

    def zoom_pane(self, key: str, step: int):
        """Ctrl+滚轮:只缩放鼠标所在的窗格(hex = 左右双栏整体)。"""
        size = max(6, min(36, self.pane_sizes[key] + step))
        if size == self.pane_sizes[key]:
            return
        self.pane_sizes[key] = size
        edits = {"ascii": (self.ascii_edit,),
                 "hex": (self.hex_left, self.hex_right),   # 同字号才能对齐
                 "pure": (self.pure_edit,)}[key]
        for e in edits:
            self._style_edit(e, size)
        if key == "hex":
            self._recalc_bpl()          # 字号变 → 每行字节数重算
            self._rebuild_timer.start()  # 并按新字号重排已显示数据
        # 纯HEX 由 Qt 原生折行,改字号会自动重排,无需处理

    def _style_edit(self, e: QPlainTextEdit, size: int):
        """字体必须同时写进控件级 QSS:
        全局 QSS 的 font-size:13px 会覆盖 setFont()(Qt 规则:样式表字体
        优先于 setFont),控件级 QSS 优先级又高于全局 QSS → 必定生效。
        保留 setFont 是为了 fontMetrics()/_recalc_bpl 立即拿到正确字宽。"""
        font = QFont(self.s.get("font_family"), size)
        font.setStyleHint(QFont.StyleHint.Monospace)
        e.setFont(font)
        e.setStyleSheet(
            f"QPlainTextEdit{{background:{self.s.get('col_bg')};"
            f"color:{self.s.get('col_text')};border:none;"
            f"font-family:'{self.s.get('font_family')}';"
            f"font-size:{size}pt;}}")

    # ================= 内部实现 =================
    def _edits(self):
        return (self.ascii_edit, self.hex_left, self.hex_right, self.pure_edit)

    def _sync_scroll(self, other, val):
        if self._syncing:
            return
        self._syncing = True
        other.verticalScrollBar().setValue(val)
        self._syncing = False

    def resizeEvent(self, ev):
        super().resizeEvent(ev)
        old = self._hex_bpl
        self._recalc_bpl()
        # HEX 双栏是定宽列,需按新宽度重排已显示数据(防抖,拖动停止后才重排)。
        # 纯HEX / ASCII 由 Qt 原生折行,缩放时自动适配,无需重排。
        if self.mode == MODE_HEXDUAL and self._hex_bpl != old:
            self._rebuild_timer.start()

    def _recalc_bpl(self):
        """根据栏宽自动计算 HEX 双栏每行字节数(8 的倍数)→ 随宽度自动换行。"""
        fm = QFontMetricsF(self.hex_left.font())
        cw = max(fm.horizontalAdvance("A"), 1.0)
        w = max(self.hex_left.viewport().width(), 100)
        bpl = int((w - 24) / (cw * 3))           # 每字节占 "XX " 3 字符
        self._hex_bpl = max(8, (bpl // 8) * 8)

    def _need_ts(self, ts: datetime) -> bool:
        if not self.show_ts:
            return False
        # HEX 模式:一帧即一个数据包(按总线空闲分帧),逐帧打时间戳,
        # 不按 ts_interval 节流 —— 否则相邻很近的帧会换行却没有时间戳。
        if self.mode in (MODE_HEXDUAL, MODE_PUREHEX):
            self._last_ts = ts
            return True
        if self._last_ts is None or \
                (ts - self._last_ts).total_seconds() * 1000.0 >= self.ts_interval:
            self._last_ts = ts
            return True
        return False

    @staticmethod
    def _fmt_ts(ts: datetime) -> str:
        return f"[{ts.strftime('%H:%M:%S')}.{ts.microsecond // 1000:03d}]"

    def _fmt(self, color: str) -> QTextCharFormat:
        f = QTextCharFormat()
        f.setForeground(QColor(color))
        return f

    # ---------- 批量刷新 ----------
    def _flush(self):
        if not self._pending:
            self._timer.stop()
            return
        pend, self._pending = self._pending, []

        # 留存原始帧,供 HEX 缩放/换模式时按新宽度重排
        self._frames.extend(pend)
        if len(self._frames) > self._max_frames:
            del self._frames[:len(self._frames) - self._max_frames]

        if self.mode == MODE_ASCII:
            self._flush_ascii(pend)
        elif self.mode == MODE_HEXDUAL:
            self._flush_hexdual(pend)
        else:
            self._flush_purehex(pend)

        if self.autoscroll:
            edits = ((self.hex_left,) if self.mode == MODE_HEXDUAL
                     else (self.current_edit(),))
            for e in edits:
                sb = e.verticalScrollBar()
                sb.setValue(sb.maximum())

    def _flush_ascii(self, pend):
        cur = QTextCursor(self.ascii_edit.document())
        cur.movePosition(QTextCursor.MoveOperation.End)
        cur.beginEditBlock()
        ts_fmt = self._fmt(self.s.get("col_ts"))
        tx_fmt = self._fmt(self.s.get("col_ascii"))
        for data, ts, direction in pend:
            if direction == "NOTE":
                cur.insertText(f"{self._fmt_ts(ts)}  {data}\n", tx_fmt)
                continue
            if self._need_ts(ts):
                cur.insertText(self._fmt_ts(ts) + "\n", ts_fmt)
            text = data.decode("utf-8", errors="replace").rstrip("\r\n")
            if direction == "TX":
                cur.insertText("TX → ", tx_fmt)
                cur.insertText(text, tx_fmt)
            else:
                for seg, color in color_engine.split_segments(text, self._rules):
                    cur.insertText(seg, self._fmt(color or self.s.get("col_text")))
            cur.insertText("\n", self._fmt(self.s.get("col_text")))
        cur.endEditBlock()

    def _flush_hexdual(self, pend):
        lcur = QTextCursor(self.hex_left.document())
        rcur = QTextCursor(self.hex_right.document())
        lcur.movePosition(QTextCursor.MoveOperation.End)
        rcur.movePosition(QTextCursor.MoveOperation.End)
        lcur.beginEditBlock()
        rcur.beginEditBlock()
        ts_fmt = self._fmt(self.s.get("col_ts"))
        hex_fmt = self._fmt(self.s.get("col_hex"))
        asc_fmt = self._fmt(self.s.get("col_ascii"))
        bpl = self._hex_bpl
        for data, ts, direction in pend:
            if direction == "NOTE":
                lcur.insertText(f"{self._fmt_ts(ts)}  {data}\n", asc_fmt)
                rcur.insertText("\n", asc_fmt)
                continue
            need_ts = self._need_ts(ts)
            if need_ts or direction == "TX":
                # 两栏同步插入帧头 → 行数永远一致；TX 即使关闭时间戳也会标识。
                t = self._fmt_ts(ts) if need_ts else ""
                if direction == "TX":
                    t += ("  " if t else "") + "TX →"
                lcur.insertText(t + "\n", ts_fmt)
                rcur.insertText(t + "\n", ts_fmt)
            for i in range(0, len(data), bpl):
                chunk = data[i:i + bpl]
                lcur.insertText(" ".join(f"{b:02X}" for b in chunk) + "\n", hex_fmt)
                # 每字节恒定 1 字符,不可见字节用 '·' 占位 → 与左栏严格对齐
                rcur.insertText(
                    "".join(chr(b) if 0x20 <= b < 0x7F else "·" for b in chunk)
                    + "\n", asc_fmt)
        lcur.endEditBlock()
        rcur.endEditBlock()

    def _flush_purehex(self, pend):
        cur = QTextCursor(self.pure_edit.document())
        cur.movePosition(QTextCursor.MoveOperation.End)
        cur.beginEditBlock()
        ts_fmt = self._fmt(self.s.get("col_ts"))
        hex_fmt = self._fmt(self.s.get("col_hex"))
        for data, ts, direction in pend:
            if direction == "NOTE":
                if self._pure_started:
                    cur.insertText("\n", hex_fmt)
                self._pure_started = True
                cur.insertText(f"{self._fmt_ts(ts)}  {data}",
                               self._fmt(self.s.get("col_ascii")))
                continue
            # 每帧 = 一个独立段落,交给 Qt 按控件宽度自动折行(缩放即时重排,
            # 无横向滚动条);一帧长度受分帧/MAX_FRAME 限制,不会因超长段落卡顿。
            if self._pure_started:
                cur.insertText("\n", hex_fmt)
            self._pure_started = True
            if self._need_ts(ts):
                cur.insertText(self._fmt_ts(ts) + " ", ts_fmt)
            if direction == "TX":
                cur.insertText("TX → ", self._fmt(self.s.get("col_ascii")))
            cur.insertText(" ".join(f"{b:02X}" for b in data), hex_fmt)
        cur.endEditBlock()

    def _rebuild_hex(self):
        """按当前宽度/字号,用历史帧重排当前 HEX 视图(ASCII 不走此路径)。"""
        if self.mode == MODE_HEXDUAL:
            self.hex_left.clear()
            self.hex_right.clear()
            self._last_ts = None
            if self._frames:
                self._flush_hexdual(list(self._frames))
            tails = (self.hex_left, self.hex_right)
        elif self.mode == MODE_PUREHEX:
            self.pure_edit.clear()
            self._last_ts = None
            self._pure_started = False
            if self._frames:
                self._flush_purehex(list(self._frames))
            tails = (self.pure_edit,)
        else:
            return
        if self.autoscroll:
            for e in tails:
                sb = e.verticalScrollBar()
                sb.setValue(sb.maximum())
