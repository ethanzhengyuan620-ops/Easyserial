# -*- coding: utf-8 -*-
"""Easy串口助手 程序入口。"""
from datetime import datetime
import os
import sys
import threading
import traceback

from PyQt6.QtWidgets import QApplication

from ui import theme


def _crash_log_path():
    """Return a writable diagnostic log path in both source and EXE builds."""
    if getattr(sys, "frozen", False):
        base = os.path.join(
            os.environ.get("APPDATA", os.path.expanduser("~")),
            "SerialAssistant",
        )
    else:
        base = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(base, exist_ok=True)
    return os.path.join(base, "crash.log")


def _install_exception_handler():
    """Keep an exception in a Qt signal handler from aborting the whole process."""
    def record(exc_type, exc_value, exc_tb):
        report = (
            f"\n[{datetime.now():%Y-%m-%d %H:%M:%S.%f}] "
            f"Unhandled exception\n"
            + "".join(traceback.format_exception(exc_type, exc_value, exc_tb))
        )
        try:
            with open(_crash_log_path(), "a", encoding="utf-8") as stream:
                stream.write(report)
                stream.flush()
        except Exception:
            pass
        try:
            sys.stderr.write(report)
            sys.stderr.flush()
        except Exception:
            pass

    sys.excepthook = record

    def record_thread(args):
        record(args.exc_type, args.exc_value, args.exc_traceback)

    threading.excepthook = record_thread


def main():
    _install_exception_handler()
    app = QApplication(sys.argv)
    app.setApplicationName("EasySerial")
    app.setStyleSheet(theme.QSS)
    from ui.main_window import MainWindow

    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
