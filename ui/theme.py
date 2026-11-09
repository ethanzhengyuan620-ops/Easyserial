# -*- coding: utf-8 -*-
"""全局深色主题:深色背景 + 青色 #00BCD4 强调色,圆角输入框。"""

ACCENT      = "#00BCD4"
ACCENT_DIM  = "#0E7490"
WARN        = "#FFB300"
PANEL_BG    = "#111827"
CARD_BG     = "#1F2937"
BORDER      = "#374151"
INPUT_BG    = "#0F172A"
TEXT        = "#E2E8F0"
TEXT_DIM    = "#64748B"
LOG_BG      = "#0D0D0D"

QSS = f"""
QWidget {{
    background: {PANEL_BG};
    color: {TEXT};
    font-size: 13px;
    font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
}}
QLabel {{ background: transparent; }}
QLabel#dim {{ color: {TEXT_DIM}; font-size: 12px; }}
QLabel#sectionTitle {{ font-weight: bold; font-size: 14px; }}

QFrame#hline {{ background: {BORDER}; max-height: 1px; min-height: 1px; border: none; }}

QLineEdit, QComboBox, QSpinBox, QDateTimeEdit {{
    background: {INPUT_BG};
    border: 1px solid {BORDER};
    border-radius: 8px;
    padding: 6px 10px;
    color: {TEXT};
    selection-background-color: {ACCENT_DIM};
}}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus {{ border: 1px solid {ACCENT}; }}
QComboBox::drop-down {{ border: none; width: 22px; }}
QComboBox::down-arrow {{
    image: none; border-left: 4px solid transparent; border-right: 4px solid transparent;
    border-top: 5px solid {TEXT_DIM}; margin-right: 8px;
}}
QComboBox QAbstractItemView {{
    background: {CARD_BG}; border: 1px solid {BORDER};
    selection-background-color: {ACCENT_DIM}; outline: none;
}}
QSpinBox::up-button, QSpinBox::down-button {{ width: 0; border: none; }}

QPlainTextEdit, QTextEdit {{
    background: {LOG_BG};
    border: none;
    color: {TEXT};
    selection-background-color: {ACCENT_DIM};
}}
QPlainTextEdit#sendInput {{
    background: {INPUT_BG};
    border: 1px solid {BORDER};
    border-radius: 8px;
    padding: 6px;
}}
QPlainTextEdit#sendInput:focus {{ border: 1px solid {ACCENT}; }}

QPushButton {{
    background: {CARD_BG};
    border: 1px solid {BORDER};
    border-radius: 8px;
    padding: 7px 14px;
    color: {TEXT};
}}
QPushButton:hover {{ border-color: {ACCENT}; color: {ACCENT}; }}
QPushButton:pressed {{ background: {INPUT_BG}; }}

QPushButton#accent {{
    background: {ACCENT}; border: none; color: #06262B; font-weight: bold;
}}
QPushButton#accent:hover {{ background: #26C9DD; color: #06262B; }}
QPushButton#accent:disabled {{ background: {BORDER}; color: {TEXT_DIM}; }}

QPushButton#danger {{ background: #991B1B; border: none; color: #FECACA; font-weight: bold; }}
QPushButton#danger:hover {{ background: #B91C1C; color: #FEE2E2; }}

QPushButton#pill {{
    background: {INPUT_BG}; border: 1px solid {BORDER}; border-radius: 6px;
    padding: 5px 8px; color: {TEXT_DIM}; font-size: 12px;
}}
QPushButton#pill:hover {{ color: {ACCENT}; border-color: {ACCENT}; }}
QPushButton#pill:checked {{
    background: {ACCENT}; border-color: {ACCENT}; color: #06262B; font-weight: bold;
}}

QPushButton#quick {{
    background: transparent; border: 1px solid {BORDER}; border-radius: 6px;
    padding: 4px 10px; color: {TEXT_DIM}; font-size: 12px;
}}
QPushButton#quick:hover {{ border-color: {ACCENT}; color: {ACCENT}; }}

QPushButton#tiny {{
    background: {CARD_BG}; border: 1px solid {BORDER}; border-radius: 5px;
    padding: 2px 8px; font-size: 12px;
}}
QPushButton#tiny:hover {{ border-color: {ACCENT}; color: {ACCENT}; }}
QPushButton#tiny:checked {{ background: {ACCENT_DIM}; border-color: {ACCENT}; color: white; }}

QScrollBar:vertical {{ background: transparent; width: 10px; margin: 0; }}
QScrollBar::handle:vertical {{ background: {BORDER}; border-radius: 5px; min-height: 30px; }}
QScrollBar::handle:vertical:hover {{ background: {TEXT_DIM}; }}
QScrollBar:horizontal {{ background: transparent; height: 10px; margin: 0; }}
QScrollBar::handle:horizontal {{ background: {BORDER}; border-radius: 5px; min-width: 30px; }}
QScrollBar::add-line, QScrollBar::sub-line {{ width: 0; height: 0; }}
QScrollBar::add-page, QScrollBar::sub-page {{ background: transparent; }}

QSplitter::handle {{ background: {BORDER}; }}
QSplitter::handle:hover {{ background: {ACCENT}; }}

QStatusBar {{ background: {CARD_BG}; border-top: 1px solid {BORDER}; }}
QStatusBar::item {{ border: none; }}

QTabWidget::pane {{ border: 1px solid {BORDER}; border-radius: 8px; top: -1px; }}
QTabBar::tab {{
    background: {CARD_BG}; border: 1px solid {BORDER}; padding: 7px 16px;
    border-top-left-radius: 8px; border-top-right-radius: 8px; margin-right: 2px;
    color: {TEXT_DIM};
}}
QTabBar::tab:selected {{ color: {ACCENT}; border-bottom-color: {PANEL_BG}; }}

QListWidget {{
    background: {INPUT_BG}; border: 1px solid {BORDER}; border-radius: 8px; outline: none;
}}
QListWidget::item {{ padding: 6px; border-radius: 4px; }}
QListWidget::item:selected {{ background: {ACCENT_DIM}; }}

QMenu {{ background: {CARD_BG}; border: 1px solid {BORDER}; border-radius: 6px; padding: 4px; }}
QMenu::item {{ padding: 6px 20px; border-radius: 4px; }}
QMenu::item:selected {{ background: {ACCENT_DIM}; }}

QCheckBox {{ background: transparent; spacing: 6px; }}
QCheckBox::indicator {{
    width: 15px; height: 15px; border: 1px solid {BORDER}; border-radius: 4px;
    background: {INPUT_BG};
}}
QCheckBox::indicator:checked {{ background: {ACCENT}; border-color: {ACCENT}; }}

QToolTip {{ background: {CARD_BG}; color: {TEXT}; border: 1px solid {BORDER}; padding: 4px; }}
"""
