# -*- coding: utf-8 -*-
"""主窗口:左侧配置面板 + 右侧(接收区 / 分隔线 / 发送区)+ 状态栏。"""
import os
import time
from datetime import datetime

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QKeySequence, QShortcut
from PyQt6.QtWidgets import (QMainWindow, QWidget, QHBoxLayout, QVBoxLayout,
                             QLabel, QPushButton, QFrame, QSplitter,
                             QFileDialog, QMessageBox, QStatusBar)

from config.settings import Settings
from core.serial_worker import SerialWorker
from core.auto_reply import find_auto_reply
from . import theme
from .serial_panel import SerialPanel
from .receive_view import ReceiveView
from .send_panel import SendPanel
from .search_bar import SearchBar
from .rule_editor import RuleEditor
from .tools_dialog import ToolsDialog
from .appearance import AppearanceDialog


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.s = Settings()
        self.setWindowTitle("Easy串口助手")
        self.resize(1280, 800)

        self.worker = None
        self._connected = False
        self.rx_total = 0
        self.tx_total = 0
        self._rec_file = None
        self._last_auto_reply = 0.0
        self._tools_window = None
        self._closing = False

        # ================= 布局 =================
        central = QWidget()
        self.setCentralWidget(central)
        h = QHBoxLayout(central)
        h.setContentsMargins(0, 0, 0, 0)
        h.setSpacing(0)

        # ---- 左侧面板 ----
        self.panel = SerialPanel(self.s)
        h.addWidget(self.panel)

        vline = QFrame()
        vline.setStyleSheet(f"background:{theme.BORDER};")
        vline.setFixedWidth(1)
        h.addWidget(vline)

        # ---- 右侧:接收区 + 发送区(纵向 Splitter,发送区约 25%) ----
        self.receive = ReceiveView(self.s)
        self.send_panel = SendPanel(self.s)

        recv_wrap = QWidget()
        rv = QVBoxLayout(recv_wrap)
        rv.setContentsMargins(0, 0, 0, 0)
        rv.addWidget(self.receive)

        self.vsplit = QSplitter(Qt.Orientation.Vertical)
        self.vsplit.addWidget(recv_wrap)
        self.vsplit.addWidget(self.send_panel)
        self.vsplit.setHandleWidth(2)
        self.vsplit.setSizes([600, 200])      # 约 75% / 25%
        self.vsplit.setCollapsible(0, False)
        h.addWidget(self.vsplit, 1)

        # ---- 右上角悬浮字体工具栏 ----
        self._build_font_bar(recv_wrap)

        # ---- 悬浮搜索栏 ----
        self.search = SearchBar(self.receive.current_edit, recv_wrap)
        QShortcut(QKeySequence("Ctrl+F"), self, self._show_search)

        # ================= 状态栏 =================
        sb = QStatusBar()
        self.setStatusBar(sb)
        self.status_lab = QLabel("● 未连接")
        self.status_lab.setStyleSheet(f"color:{theme.TEXT_DIM};padding-left:8px;")
        self.tx_lab = QLabel("TX: 0 bytes")
        self.rx_lab = QLabel("RX: 0 bytes")
        for lab in (self.tx_lab, self.rx_lab):
            lab.setStyleSheet("padding: 0 10px;")
        sb.addWidget(self.status_lab)
        sb.addPermanentWidget(self.tx_lab)
        sb.addPermanentWidget(self.rx_lab)

        # ================= 信号连线 =================
        self.panel.connect_toggled.connect(self._toggle_connect)
        self.panel.mode_changed.connect(self._on_mode)
        self.panel.autoscroll_changed.connect(
            lambda v: setattr(self.receive, "autoscroll", v))
        self.panel.ts_changed.connect(
            lambda v: setattr(self.receive, "show_ts", v))
        self.panel.interval_changed.connect(
            lambda v: setattr(self.receive, "ts_interval", v))
        self.panel.clear_clicked.connect(self._clear_log)
        self.panel.rules_clicked.connect(self._edit_rules)
        self.panel.tools_clicked.connect(self._open_tools)
        self.panel.pin_toggled.connect(self._toggle_pin)
        self.panel.record_toggled.connect(self._toggle_record)

        self.send_panel.send_bytes.connect(self._send)
        self.send_panel.hint.connect(
            lambda m: self.statusBar().showMessage(m, 4000))
        self.send_panel.set_send_enabled(False)
        # Ctrl+滚轮缩放已下放到各窗格内部处理(receive_view / send_panel)

        if self.s.get("pin"):
            self._toggle_pin(True)

    # ================= 字体工具栏 =================
    def _build_font_bar(self, parent):
        self.font_bar = QFrame(parent)
        self.font_bar.setStyleSheet(
            f"QFrame{{background:{theme.CARD_BG};border:1px solid {theme.BORDER};"
            f"border-radius:6px;}}")
        lay = QHBoxLayout(self.font_bar)
        lay.setContentsMargins(4, 3, 4, 3)
        lay.setSpacing(3)
        dec = QPushButton("A-")
        self.size_lab = QLabel(str(self.s.get("font_size")))
        self.size_lab.setStyleSheet("padding:0 4px;font-weight:bold;border:none;")
        inc = QPushButton("A+")
        gear = QPushButton("⚙")
        for b in (dec, inc, gear):
            b.setObjectName("tiny")
            b.setFixedSize(30, 24)
            b.setCursor(Qt.CursorShape.PointingHandCursor)
        dec.clicked.connect(lambda: self._zoom(-1))
        inc.clicked.connect(lambda: self._zoom(+1))
        gear.clicked.connect(self._open_appearance)
        for w in (dec, self.size_lab, inc, gear):
            lay.addWidget(w)
        self._recv_wrap = parent
        parent.installEventFilter(self)

    def eventFilter(self, obj, ev):
        if obj is self._recv_wrap and ev.type() in (ev.Type.Resize, ev.Type.Show):
            self._reposition_overlays()
        return super().eventFilter(obj, ev)

    def _show_search(self):
        self._reposition_overlays()   # 确保打开前已按内容定好尺寸与位置
        self.search.show_bar()

    def _reposition_overlays(self):
        w = self._recv_wrap.width()
        self.font_bar.adjustSize()
        self.font_bar.move(w - self.font_bar.width() - 18, 8)
        self.font_bar.raise_()
        self.search.adjustSize()      # 先按内容定宽,否则各控件会挤成一团
        self.search.move(w - self.search.width() - 18,
                         self.font_bar.height() + 14)
        self.search.raise_()

    def _zoom(self, step):
        """A−/A+ = 基准字号:所有窗格(接收三模式 + 发送框)统一重置。
        单窗格的临时缩放用 Ctrl+滚轮(滚到哪个窗格改哪个)。"""
        size = max(6, min(36, self.s.get("font_size") + step))
        self.s.set("font_size", size)
        self.size_lab.setText(str(size))
        self.receive.apply_appearance()
        self.send_panel.apply_appearance()

    def _open_appearance(self):
        dlg = AppearanceDialog(self.s, self)
        dlg.changed.connect(self.receive.apply_appearance)
        dlg.changed.connect(self.send_panel.apply_appearance)
        dlg.exec()

    def _open_tools(self):
        if self._tools_window is None:
            self._tools_window = ToolsDialog(self.s)
            self._tools_window.destroyed.connect(
                lambda: setattr(self, "_tools_window", None))
        if self._tools_window.isMinimized():
            self._tools_window.showNormal()
        else:
            self._tools_window.show()
        self._tools_window.raise_()
        self._tools_window.activateWindow()

    def _request_serial_stop(self):
        worker = self.worker
        if worker is None:
            return
        self.send_panel.stop_timer()
        self.panel.conn_btn.setEnabled(False)
        self.panel.conn_btn.setText("正在断开…")
        worker.stop(wait=False)

    # ================= 串口连接 =================
    def _toggle_connect(self):
        if self.worker:
            self.status_lab.setText("● 正在断开串口…")
            self.status_lab.setStyleSheet(
                f"color:{theme.WARN};padding-left:8px;")
            self._request_serial_stop()
            return
        p = self.panel.params()
        if not p["port"]:
            QMessageBox.warning(self, "提示", "请选择串口端口")
            return
        for k in ("port", "baud", "databits", "parity", "stopbits"):
            self.s.set(k, p[k], flush=False)
        self.s.save()
        self.worker = SerialWorker(p["port"], p["baud"], p["databits"],
                                   p["parity"], p["stopbits"],
                                   frame_gap=self.s.get("frame_gap"),
                                   split_nl=(self.s.get("mode") == 0))
        self.worker.frame_received.connect(self._on_frame)
        self.worker.opened.connect(self._on_worker_opened)
        self.worker.error.connect(self._on_error)
        self.worker.closed.connect(self._on_worker_closed)
        self.worker.finished.connect(self._on_worker_finished)
        self._connected = False
        self.send_panel.set_send_enabled(False)
        self.status_lab.setText(f"● 正在连接   {p['port']}…")
        self.status_lab.setStyleSheet(
            f"color:{theme.WARN};padding-left:8px;")
        self.worker.start()

    def _on_worker_opened(self):
        if self.sender() is self.worker:
            self._set_connected(True)

    def _on_worker_closed(self):
        # ``closed`` is emitted just before QThread.run() returns.  Do not
        # clear the last reference yet; QThread.finished performs that cleanup.
        if self.sender() is not self.worker:
            return
        self.send_panel.stop_timer()
        self._set_connected(False)

    def _on_worker_finished(self):
        worker = self.sender()
        if worker is not self.worker:
            return
        self.worker = None
        worker.deleteLater()
        self.send_panel.stop_timer()
        self._set_connected(False)
        self.panel.conn_btn.setEnabled(True)
        if self._closing:
            QTimer.singleShot(0, self.close)

    def _set_connected(self, on):
        self._connected = on
        self.send_panel.set_send_enabled(on)
        self.panel.set_connected(on)
        p = self.panel.params()
        if on:
            self.status_lab.setText(f"● 已连接   {p['port']} @ {p['baud']} bps")
            self.status_lab.setStyleSheet(
                f"color:{theme.ACCENT};padding-left:8px;")
            self.setWindowTitle(f"Easy串口助手 — {p['port']} @ {p['baud']}")
        else:
            self.status_lab.setText("● 未连接")
            self.status_lab.setStyleSheet(
                f"color:{theme.TEXT_DIM};padding-left:8px;")
            self.setWindowTitle("Easy串口助手")

    def _on_error(self, msg):
        self.statusBar().showMessage(msg, 5000)
        if self.sender() is self.worker and self.worker:
            self._request_serial_stop()
        self.send_panel.stop_timer()
        self._set_connected(False)

    # ================= 收发数据 =================
    def _on_frame(self, data: bytes, ts: datetime):
        self.rx_total += len(data)
        self.rx_lab.setText(f"RX: {self.rx_total:,} bytes")
        self.receive.append_frame(data, ts)
        if self._rec_file:
            try:
                text = data.decode("utf-8", errors="replace").rstrip("\r\n")
                self._rec_file.write(
                    f"[{ts.strftime('%H:%M:%S')}.{ts.microsecond // 1000:03d}] "
                    f"{text}\n")
            except Exception:
                pass
        self._try_auto_reply(data)

    def _try_auto_reply(self, data: bytes):
        if not self._connected or not self.s.get("auto_reply_enabled", False):
            return
        now = time.monotonic()
        cooldown = max(0, int(self.s.get("auto_reply_cooldown", 100))) / 1000.0
        if now - self._last_auto_reply < cooldown:
            return
        payload, rule_name, error = find_auto_reply(
            data, self.s.get("auto_reply_rules", []))
        if error:
            self.statusBar().showMessage(f"自动回复配置错误：{error}", 5000)
        if payload is None:
            return
        self._last_auto_reply = now
        self._send(payload, source=f"自动回复「{rule_name}」")

    def _send(self, data: bytes, source="手动发送"):
        if not self.worker or not self._connected:
            message = ("串口正在连接,请稍候" if self.worker
                       else "未连接串口,无法发送")
            self.statusBar().showMessage(message, 3000)
            self.send_panel.stop_timer()
            return
        n = self.worker.write(data)
        if n < 0:
            self.statusBar().showMessage("发送失败:串口未打开或写入异常", 5000)
            self.send_panel.stop_timer()
            return
        if n == 0:
            self.statusBar().showMessage("发送失败:串口未写入任何数据", 5000)
            self.send_panel.stop_timer()
            return

        sent = data[:n]
        self.tx_total += n
        self.tx_lab.setText(f"TX: {self.tx_total:,} bytes")
        self.receive.append_tx(sent, datetime.now())
        if n < len(data):
            self.statusBar().showMessage(
                f"发送不完整:计划 {len(data)} bytes,实际 {n} bytes", 5000)
            self.send_panel.stop_timer()
        else:
            self.statusBar().showMessage(f"{source}：已发送 {n} bytes", 1800)

    # ================= 其他操作 =================
    def _on_mode(self, mode):
        self.receive.set_mode(mode)
        if self.worker:
            self.worker.set_newline_split(mode == 0)   # 仅 ASCII 按 \n 分行
        if self.search.isVisible():
            self.search.refresh()

    def _clear_log(self):
        self.receive.clear_all()
        self.rx_total = 0
        self.tx_total = 0
        self.rx_lab.setText("RX: 0 bytes")
        self.tx_lab.setText("TX: 0 bytes")

    def _edit_rules(self):
        dlg = RuleEditor(self.s.get("rules"), self)
        if dlg.exec():
            rules = dlg.rules()
            self.s.set("rules", rules)
            self.receive.set_rules(rules)

    def _toggle_pin(self, on):
        self.s.set("pin", on)
        self.setWindowFlag(Qt.WindowType.WindowStaysOnTopHint, on)
        self.show()

    def _toggle_record(self, on):
        if on:
            folder = QFileDialog.getExistingDirectory(self, "选择日志保存目录")
            if not folder:
                self.panel.rec_btn.setChecked(False)
                return
            name = datetime.now().strftime("easyserial_%Y%m%d_%H%M%S.log")
            try:
                self._rec_file = open(os.path.join(folder, name), "a",
                                      encoding="utf-8")
                self.statusBar().showMessage(f"正在录制 → {name}", 4000)
            except Exception as e:
                QMessageBox.warning(self, "录制失败", str(e))
                self.panel.rec_btn.setChecked(False)
        else:
            if self._rec_file:
                self._rec_file.close()
                self._rec_file = None

    def closeEvent(self, ev):
        if self.worker is not None:
            # Never destroy a QThread wrapper while its run() method is alive.
            self._closing = True
            ev.ignore()
            self.status_lab.setText("● 正在关闭串口…")
            self._request_serial_stop()
            return
        if self._tools_window:
            self._tools_window.close()
        if self._rec_file:
            self._rec_file.close()
            self._rec_file = None
        self.s.save()
        super().closeEvent(ev)
