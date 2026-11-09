#pragma once

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QString>

namespace Theme {
inline constexpr auto Background = "#F5F7FB";
inline constexpr auto Panel = "#FFFFFF";
inline constexpr auto Card = "#F1F5F9";
inline constexpr auto Border = "#DCE3ED";
inline constexpr auto Text = "#1E293B";
inline constexpr auto Dim = "#64748B";
inline constexpr auto Accent = "#2563EB";
inline constexpr auto AccentHover = "#1D4ED8";
inline constexpr auto Danger = "#DC2626";
inline constexpr auto Warning = "#B45309";

inline QString styleSheet()
{
    return QStringLiteral(R"QSS(
        QWidget { background:#F5F7FB; color:#1E293B; }
        QMainWindow, QDialog { background:#F5F7FB; }
        QWidget#sidebar, QWidget#receiveSurface, QWidget#sendSurface { background:#FFFFFF; }
        QLabel { background:transparent; }
        QLabel#sectionTitle { font-weight:600; color:#0F172A; }
        QLabel#dim { color:#64748B; }
        QLineEdit, QPlainTextEdit, QComboBox, QSpinBox, QListWidget {
            background:#FFFFFF; color:#1E293B; border:1px solid #DCE3ED;
            border-radius:6px; padding:6px; selection-background-color:#DBEAFE;
            selection-color:#1E3A8A;
        }
        QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus { border-color:#93B4F4; }
        QTableView { background:#FFFFFF; border:0; padding:0; gridline-color:#E8EDF4; }
        QHeaderView::section {
            background:#F8FAFC; color:#64748B; border:0;
            border-bottom:1px solid #E2E8F0; padding:8px 6px;
        }
        QComboBox::drop-down { border:0; width:22px; }
        QComboBox::down-arrow { image:url(:/icons/down.svg); width:16px; height:16px; }
        QSpinBox { padding-right:22px; }
        QSpinBox::up-button { subcontrol-origin:border; subcontrol-position:top right; width:22px; border:0; }
        QSpinBox::down-button { subcontrol-origin:border; subcontrol-position:bottom right; width:22px; border:0; }
        QSpinBox::up-arrow { image:url(:/icons/up.svg); width:12px; height:12px; }
        QSpinBox::down-arrow { image:url(:/icons/down.svg); width:12px; height:12px; }
        QComboBox QAbstractItemView { background:#FFFFFF; selection-background-color:#DBEAFE; }
        QPushButton {
            background:#FFFFFF; color:#334155; border:1px solid #DCE3ED;
            border-radius:6px; padding:6px 12px; min-height:18px;
        }
        QPushButton:hover { background:#F1F5F9; border-color:#B7C6DC; }
        QPushButton:pressed { background:#E2E8F0; }
        QPushButton:checked { background:#EFF6FF; color:#1D4ED8; border-color:#93B4F4; }
        QPushButton:disabled { color:#94A3B8; background:#F8FAFC; border-color:#E8EDF4; }
        QPushButton#accent { background:#2563EB; color:white; border:1px solid #2563EB; font-weight:600; }
        QPushButton#accent:hover { background:#1D4ED8; }
        QPushButton#accent:disabled { background:#DBEAFE; color:#64748B; border-color:#DBEAFE; }
        QPushButton#danger { background:#FEF2F2; color:#B91C1C; border-color:#FECACA; }
        QPushButton#quick { padding:4px 10px; }
        QCheckBox, QRadioButton { background:transparent; spacing:7px; }
        QCheckBox::indicator { width:15px; height:15px; border:1px solid #B6C4D8; border-radius:4px; background:white; }
        QCheckBox::indicator:checked { background:#2563EB; border-color:#2563EB; image:url(:/icons/check.svg); }
        QCheckBox::indicator:disabled { background:#F1F5F9; border-color:#DCE3ED; }
        QRadioButton::indicator { width:14px; height:14px; border:1px solid #B6C4D8; border-radius:8px; background:white; }
        QRadioButton::indicator:checked { background:#2563EB; border:1px solid #2563EB; }
        QTabWidget::pane { background:white; border:1px solid #DCE3ED; border-radius:6px; }
        QTabBar::tab {
            background:#F1F5F9; color:#64748B; padding:9px 16px; margin-right:3px;
        }
        QTabBar::tab:selected { color:#1D4ED8; background:#FFFFFF; border-bottom:2px solid #2563EB; }
        QGroupBox {
            border:1px solid #DCE3ED; border-radius:7px; margin-top:12px; padding-top:10px;
        }
        QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 5px; }
        QSplitter::handle { background:#E2E8F0; }
        QStatusBar { background:#FFFFFF; border-top:1px solid #E2E8F0; }
        QScrollBar:vertical { background:#F8FAFC; width:10px; }
        QScrollBar::handle:vertical { background:#CBD5E1; border-radius:4px; min-height:30px; }
        QScrollBar::handle:vertical:hover { background:#94A3B8; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
        QScrollBar:horizontal { background:#F8FAFC; height:10px; }
        QScrollBar::handle:horizontal { background:#CBD5E1; border-radius:4px; min-width:30px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
        QMenu { background:#FFFFFF; border:1px solid #DCE3ED; padding:4px; }
        QMenu::item { padding:6px 20px; }
        QMenu::item:selected { background:#EFF6FF; color:#1D4ED8; }
        QToolTip { background:#FFFFFF; color:#334155; border:1px solid #CBD5E1; padding:5px; }
    )QSS");
}

inline void apply(QApplication &app)
{
    // QSS font-size would override runtime zoom; use the application font instead.
    app.setStyle(QStringLiteral("Fusion"));
    QFont font(QStringLiteral("Segoe UI"), 10);
    font.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Microsoft YaHei")});
    app.setFont(font);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(Background));
    palette.setColor(QPalette::WindowText, QColor(Text));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor(Card));
    palette.setColor(QPalette::Text, QColor(Text));
    palette.setColor(QPalette::Button, Qt::white);
    palette.setColor(QPalette::ButtonText, QColor(Text));
    palette.setColor(QPalette::Highlight, QColor("#DBEAFE"));
    palette.setColor(QPalette::HighlightedText, QColor("#1E3A8A"));
    palette.setColor(QPalette::PlaceholderText, QColor(Dim));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#94A3B8"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#94A3B8"));
    app.setPalette(palette);
    app.setStyleSheet(styleSheet());
}
}
