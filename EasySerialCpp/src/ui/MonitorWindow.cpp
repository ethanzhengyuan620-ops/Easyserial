#include "ui/MonitorWindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <limits>

#include "ui/ReceiveView.h"

namespace {
enum class MonitorAction { Connection, AutoScroll, Clear, Position };

QIcon actionIcon(MonitorAction action)
{
    QIcon icon;
    for (int size : {16, 32, 48}) {
        QPixmap pixels(size, size);
        pixels.fill(Qt::transparent);
        QPainter painter(&pixels);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 20.0, size / 20.0);
        painter.setPen(QPen(QColor("#475569"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        switch (action) {
        case MonitorAction::Connection:
            painter.drawArc(QRectF(3, 3, 14, 14), 50 * 16, -280 * 16);
            painter.drawLine(QPointF(10, 1.5), QPointF(10, 9));
            break;
        case MonitorAction::AutoScroll:
            painter.drawLine(QPointF(10, 2), QPointF(10, 13));
            painter.drawLine(QPointF(6, 9), QPointF(10, 13));
            painter.drawLine(QPointF(10, 13), QPointF(14, 9));
            painter.drawLine(QPointF(4, 17), QPointF(16, 17));
            break;
        case MonitorAction::Clear:
            painter.drawLine(QPointF(3, 5), QPointF(17, 5));
            painter.drawPolyline(QPolygonF{QPointF(7, 5), QPointF(7, 2), QPointF(13, 2), QPointF(13, 5)});
            painter.drawPolyline(QPolygonF{QPointF(5, 5), QPointF(6, 18), QPointF(14, 18), QPointF(15, 5)});
            painter.drawLine(QPointF(8, 8), QPointF(8.5, 15));
            painter.drawLine(QPointF(12, 8), QPointF(11.5, 15));
            break;
        case MonitorAction::Position:
            for (int x : {3, 17}) for (int y : {3, 17}) {
                painter.drawLine(QPointF(x, y), QPointF(x == 3 ? 7 : 13, y));
                painter.drawLine(QPointF(x, y), QPointF(x, y == 3 ? 7 : 13));
            }
            painter.drawEllipse(QPointF(10, 10), 1.5, 1.5);
            break;
        }
        painter.end();
        icon.addPixmap(pixels);
    }
    return icon;
}

QRect workArea(const QRect &available)
{
    const int margin = qMin(12, qMax(0, qMin(available.width(), available.height()) / 10));
    return available.adjusted(margin, margin, -margin, -margin);
}
}

MonitorWindow::MonitorWindow(Settings &settings)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      m_settings(settings)
{
    setObjectName(QStringLiteral("monitorWindow"));
    setWindowTitle(QStringLiteral("EasySerial — Monitor"));
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setMinimumSize(360, 200);
    resize(480, 320);
    m_opacity = qBound(30, settings.intValue("monitor_opacity", 90), 100);
    m_autoSnap = settings.boolValue("monitor_snap", true);
    m_corner = static_cast<Qt::Corner>(qBound(0, settings.intValue("monitor_corner", 3), 3));
    // Only the custom-painted background has alpha. Text and controls remain
    // fully opaque, and no windowOpacity/global opacity effect is applied.
    setStyleSheet(QStringLiteral(R"QSS(
        QWidget { background:transparent; }
        QLabel { background:transparent; }
        QLabel#monitorTitle { font-weight:600; }
        QPushButton, QToolButton { background:#FFFFFF; color:#334155; border:1px solid #CBD5E1;
            border-radius:6px; padding:4px 8px; min-height:18px; }
        QPushButton:hover, QToolButton:hover { background:#EFF6FF; border-color:#93B4F4; }
        QPushButton#monitorRestore { background:#2563EB; color:white; border-color:#2563EB; }
        QPushButton#monitorRestore:hover { background:#1D4ED8; }
        QToolButton[compact="true"] { padding:0; min-width:0; min-height:0; }
        QToolButton[compact="true"]:checked { background:#DBEAFE; border-color:#60A5FA; }
        QToolButton#monitorConnection:checked { background:#DCFCE7; border-color:#4ADE80; }
        QToolButton[compact="true"]:disabled { background:#F1F5F9; border-color:#E2E8F0; }
        QToolButton::menu-indicator { image:none; }
        QSlider::groove:horizontal { background:#CBD5E1; height:4px; border-radius:2px; }
        QSlider::sub-page:horizontal { background:#2563EB; border-radius:2px; }
        QSlider::handle:horizontal { background:#FFFFFF; border:2px solid #2563EB;
            width:10px; margin:-5px 0; border-radius:7px; }
        QScrollBar:vertical { background:transparent; width:8px; }
        QScrollBar::handle:vertical { background:#94A3B8; border-radius:4px; min-height:24px; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }
        QSplitter::handle { background:rgba(148,163,184,80); }
        QMenu { background:#FFFFFF; color:#334155; border:1px solid #CBD5E1; padding:4px; }
        QMenu::item { padding:6px 20px; }
        QMenu::item:selected { background:#EFF6FF; }
    )QSS"));
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(12, 10, 12, 12);
    m_layout->setSpacing(8);
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("monitorDragBar"));
    m_titleBar->setCursor(Qt::SizeAllCursor);
    m_titleBar->installEventFilter(this);
    auto *header = new QHBoxLayout(m_titleBar);
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(4);
    auto *labels = new QVBoxLayout;
    labels->setSpacing(1);
    m_title = new QLabel(QStringLiteral("Serial Monitor"), m_titleBar);
    m_title->setObjectName(QStringLiteral("monitorTitle"));
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_title->setMinimumWidth(0);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_title->setToolTip(QStringLiteral("Serial Monitor — drag here to move"));
    m_status = new QLabel(QStringLiteral("Disconnected"), m_titleBar);
    m_status->setObjectName(QStringLiteral("monitorStatus"));
    m_status->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_status->setMinimumWidth(0);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont small = font(); small.setPointSize(8); m_status->setFont(small);
    labels->addWidget(m_title);
    labels->addWidget(m_status);
    header->addLayout(labels, 1);
    const auto compactButton = [this, header](const QString &name, const QString &text, MonitorAction action) {
        auto *button = new QToolButton(m_titleBar);
        button->setObjectName(name);
        button->setProperty("compact", true);
        button->setText(text);
        button->setAccessibleName(text);
        button->setToolTip(text);
        button->setIcon(actionIcon(action));
        button->setIconSize(QSize(16, 16));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setFixedSize(28, 28);
        button->setCursor(Qt::PointingHandCursor);
        header->addWidget(button);
        return button;
    };
    m_connectionButton = compactButton("monitorConnection", "Connect serial port", MonitorAction::Connection);
    m_connectionButton->setCheckable(true);
    m_autoScrollButton = compactButton("monitorAutoScroll", "Auto scroll", MonitorAction::AutoScroll);
    m_autoScrollButton->setCheckable(true);
    setAutoScrollEnabled(settings.boolValue("autoscroll", true));
    auto *clear = compactButton("monitorClear", "Clear screen", MonitorAction::Clear);
    clear->setToolTip(QStringLiteral("Clear displayed data and counters; recorded files are not deleted"));
    connect(m_connectionButton, &QToolButton::clicked, this, [this] {
        // Checked means actually connected, never merely an attempted open.
        const QSignalBlocker blocker(m_connectionButton);
        m_connectionButton->setChecked(m_status->property("connected").toBool());
        emit connectionToggleRequested();
    });
    connect(m_autoScrollButton, &QToolButton::toggled, this, [this](bool enabled) {
        setAutoScrollEnabled(enabled);
        emit autoScrollToggled(enabled);
    });
    connect(clear, &QToolButton::clicked, this, &MonitorWindow::clearRequested);
    auto *position = compactButton("monitorPosition", "Position", MonitorAction::Position);
    position->setToolTip(QStringLiteral("Corner docking or free placement"));
    position->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(position);
    auto *group = new QActionGroup(menu);
    m_autoAction = menu->addAction(QStringLiteral("Automatic corner snap"));
    m_freeAction = menu->addAction(QStringLiteral("Free position"));
    for (auto *action : {m_autoAction, m_freeAction}) {
        action->setCheckable(true);
        group->addAction(action);
    }
    m_autoAction->setChecked(m_autoSnap);
    m_freeAction->setChecked(!m_autoSnap);
    connect(m_autoAction, &QAction::triggered, this, [this] { setAutoSnap(true); });
    connect(m_freeAction, &QAction::triggered, this, [this] { setAutoSnap(false); });
    menu->addSeparator();
    const QStringList corners{"Top left", "Top right", "Bottom left", "Bottom right"};
    for (int i = 0; i < corners.size(); ++i) {
        auto *action = menu->addAction(corners[i]);
        connect(action, &QAction::triggered, this, [this, i] {
            m_autoSnap = true;
            m_autoAction->setChecked(true);
            dockToCorner(static_cast<Qt::Corner>(i));
        });
    }
    position->setMenu(menu);
    auto *restore = new QPushButton(QStringLiteral("Restore"), m_titleBar);
    restore->setObjectName(QStringLiteral("monitorRestore"));
    restore->setToolTip(QStringLiteral("Return to the full window without disconnecting"));
    auto *close = new QPushButton(QStringLiteral("×"), m_titleBar);
    close->setObjectName(QStringLiteral("monitorClose"));
    close->setToolTip(QStringLiteral("Return to the full window"));
    close->setFixedWidth(26);
    header->addWidget(restore);
    header->addWidget(close);
    connect(restore, &QPushButton::clicked, this, &MonitorWindow::restoreRequested);
    connect(close, &QPushButton::clicked, this, &QWidget::close);
    m_layout->addWidget(m_titleBar);

    auto *controls = new QWidget(this);
    auto *grid = new QGridLayout(controls);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(3);
    m_opacitySlider = new QSlider(Qt::Horizontal, controls);
    m_opacitySlider->setObjectName(QStringLiteral("monitorOpacity"));
    m_opacitySlider->setRange(30, 100);
    m_opacitySlider->setValue(m_opacity);
    m_opacitySlider->setToolTip(QStringLiteral("Background opacity; text stays fully visible"));
    m_fontSlider = new QSlider(Qt::Horizontal, controls);
    m_fontSlider->setObjectName(QStringLiteral("monitorFont"));
    m_fontSlider->setRange(6, 36);
    m_opacityLabel = new QLabel(QString::number(m_opacity) + QLatin1Char('%'), controls);
    m_fontLabel = new QLabel(controls);
    for (auto *label : {m_opacityLabel, m_fontLabel}) {
        label->setFixedWidth(42);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    grid->addWidget(new QLabel(QStringLiteral("Opacity"), controls), 0, 0);
    grid->addWidget(m_opacitySlider, 0, 1);
    grid->addWidget(m_opacityLabel, 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("Font size"), controls), 1, 0);
    grid->addWidget(m_fontSlider, 1, 1);
    grid->addWidget(m_fontLabel, 1, 2);
    grid->setColumnStretch(1, 1);
    m_layout->addWidget(controls);
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_opacity = value;
        m_opacityLabel->setText(QString::number(value) + QLatin1Char('%'));
        m_settings.setValue("monitor_opacity", value);
        update();
    });
    connect(m_fontSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_view) m_view->setMonitorFontSize(value);
    });
    m_snapAnimation = new QPropertyAnimation(this, "pos", this);
    m_snapAnimation->setDuration(140);
    m_snapAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_snapAnimation, &QPropertyAnimation::finished, this, &MonitorWindow::saveState);
    for (auto *screen : QGuiApplication::screens()) watchScreen(screen);
    connect(qApp, &QGuiApplication::screenAdded, this, &MonitorWindow::watchScreen);
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] {
        QTimer::singleShot(0, this, &MonitorWindow::ensureOnScreen);
    });
}

void MonitorWindow::attachView(ReceiveView *view)
{
    Q_ASSERT(!m_view && view);
    m_view = view;
    view->hide();
    m_layout->insertWidget(1, view, 1);
    view->setMonitorMode(true);
    const auto updateFont = [this](int size) {
        const QSignalBlocker blocker(m_fontSlider);
        m_fontSlider->setValue(size);
        m_fontLabel->setText(QStringLiteral("%1 pt").arg(size));
    };
    updateFont(view->fontSize());
    m_fontConnection = connect(view, &ReceiveView::fontSizeChanged, this, updateFont);
    view->show();
}

ReceiveView *MonitorWindow::releaseView()
{
    if (!m_view) return nullptr;
    m_dragging = false;
    m_resizing = false;
    if (QWidget::mouseGrabber() == m_titleBar) m_titleBar->releaseMouse();
    if (QWidget::mouseGrabber() == this) releaseMouse();
    unsetCursor();
    saveState();
    m_snapAnimation->stop();
    disconnect(m_fontConnection);
    auto *view = m_view.data();
    view->hide();
    m_layout->removeWidget(view);
    view->setParent(nullptr);
    view->setMonitorMode(false);
    m_view.clear();
    return view;
}

QRect MonitorWindow::constrainedGeometry(QRect geometry, const QRect &available)
{
    if (!available.isValid()) return geometry;
    const QRect area = workArea(available);
    geometry.setSize(QSize(qBound(1, geometry.width(), area.width()), qBound(1, geometry.height(), area.height())));
    geometry.moveLeft(qBound(area.left(), geometry.left(), area.right() - geometry.width() + 1));
    geometry.moveTop(qBound(area.top(), geometry.top(), area.bottom() - geometry.height() + 1));
    return geometry;
}

QRect MonitorWindow::cornerGeometry(QSize size, const QRect &available, Qt::Corner corner)
{
    QRect result = constrainedGeometry(QRect(available.topLeft(), size), available);
    const QRect area = workArea(available);
    result.moveLeft(corner == Qt::TopRightCorner || corner == Qt::BottomRightCorner
        ? area.right() - result.width() + 1 : area.left());
    result.moveTop(corner == Qt::BottomLeftCorner || corner == Qt::BottomRightCorner
        ? area.bottom() - result.height() + 1 : area.top());
    return result;
}

Qt::Corner MonitorWindow::nearestCorner(const QRect &geometry, const QRect &available)
{
    Qt::Corner best = Qt::BottomRightCorner;
    qreal distance = std::numeric_limits<qreal>::max();
    for (int i = 0; i < 4; ++i) {
        const auto corner = static_cast<Qt::Corner>(i);
        const QPointF delta = geometry.topLeft() - cornerGeometry(geometry.size(), available, corner).topLeft();
        const qreal score = delta.x() * delta.x() + delta.y() * delta.y();
        if (score < distance) { distance = score; best = corner; }
    }
    return best;
}

QScreen *MonitorWindow::currentScreen() const
{
    if (auto *screen = QGuiApplication::screenAt(frameGeometry().center())) return screen;
    if (windowHandle() && windowHandle()->screen()) return windowHandle()->screen();
    return QGuiApplication::primaryScreen();
}

void MonitorWindow::openOnScreen(QScreen *preferred)
{
    auto *screen = preferred ? preferred : QGuiApplication::primaryScreen();
    for (auto *candidate : QGuiApplication::screens())
        if (candidate->name() == m_settings.stringValue("monitor_screen")) { screen = candidate; break; }
    QRect saved(QPoint(0, 0), QSize(480, 320));
    const auto values = m_settings.arrayValue("monitor_geometry");
    if (values.size() == 4 && values[2].toInt() > 0 && values[3].toInt() > 0)
        saved = QRect(values[0].toInt(), values[1].toInt(), values[2].toInt(), values[3].toInt());
    saved.setSize(saved.size().expandedTo(QSize(360, 200)));
    if (screen) {
        const QRect available = screen->availableGeometry();
        setMinimumSize(QSize(360, 200).boundedTo(workArea(available).size()));
        saved = m_autoSnap ? cornerGeometry(saved.size(), available, m_corner) : constrainedGeometry(saved, available);
    }
    setGeometry(saved);
    show();
    QTimer::singleShot(0, this, &MonitorWindow::updateStatusLabel);
    saveState();
}

void MonitorWindow::watchScreen(QScreen *screen)
{
    connect(screen, &QScreen::availableGeometryChanged, this, [this] { ensureOnScreen(); });
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { ensureOnScreen(); });
}

void MonitorWindow::ensureOnScreen()
{
    if (!isVisible() || m_dragging || m_resizing) return;
    if (auto *screen = currentScreen()) {
        const auto area = screen->availableGeometry();
        setMinimumSize(QSize(360, 200).boundedTo(workArea(area).size()));
        moveInto(m_autoSnap ? cornerGeometry(size(), area, m_corner) : constrainedGeometry(geometry(), area), false);
    }
}

void MonitorWindow::moveInto(const QRect &target, bool animated)
{
    m_snapAnimation->stop();
    resize(target.size());
    if (animated && isVisible() && pos() != target.topLeft()) {
        m_snapAnimation->setStartValue(pos());
        m_snapAnimation->setEndValue(target.topLeft());
        m_snapAnimation->start();
    } else {
        move(target.topLeft());
        saveState();
    }
}

void MonitorWindow::dockToCorner(Qt::Corner corner, bool animated)
{
    m_corner = corner;
    if (auto *screen = currentScreen()) moveInto(cornerGeometry(size(), screen->availableGeometry(), corner), animated);
}

void MonitorWindow::setAutoSnap(bool enabled)
{
    m_autoSnap = enabled;
    m_autoAction->setChecked(enabled);
    m_freeAction->setChecked(!enabled);
    if (enabled) {
        if (auto *screen = currentScreen()) dockToCorner(nearestCorner(geometry(), screen->availableGeometry()));
    } else {
        m_snapAnimation->stop();
        saveState();
    }
}

void MonitorWindow::saveState()
{
    if (!m_view) return;
    m_settings.setArray("monitor_geometry", {x(), y(), width(), height()}, false);
    if (auto *screen = currentScreen()) m_settings.setValue("monitor_screen", screen->name(), false);
    m_settings.setValue("monitor_corner", int(m_corner), false);
    m_settings.setValue("monitor_snap", m_autoSnap, false);
    m_settings.save();
}

void MonitorWindow::setConnectionStatus(const QString &text, bool connected, bool busy)
{
    const QSignalBlocker blocker(m_connectionButton);
    m_connectionButton->setChecked(connected);
    m_connectionButton->setEnabled(!busy);
    const QString action = busy ? QStringLiteral("Serial port operation in progress")
        : connected ? QStringLiteral("Disconnect serial port") : QStringLiteral("Connect serial port using main window settings");
    m_connectionButton->setAccessibleName(action);
    m_connectionButton->setToolTip(action + '\n' + text);
    if (m_statusText == text && m_status->property("connected").toBool() == connected) return;
    m_statusText = text;
    m_status->setProperty("connected", connected);
    m_status->setStyleSheet(connected ? "color:#15803D;background:transparent;" : "color:#64748B;background:transparent;");
    updateStatusLabel();
}

void MonitorWindow::setAutoScrollEnabled(bool enabled)
{
    const QSignalBlocker blocker(m_autoScrollButton);
    m_autoScrollButton->setChecked(enabled);
    m_autoScrollButton->setToolTip(enabled ? QStringLiteral("Auto scroll: On — click to browse history")
        : QStringLiteral("Auto scroll: Off — click to follow new data"));
}

void MonitorWindow::updateStatusLabel()
{
    m_title->setText(m_title->fontMetrics().elidedText(QStringLiteral("Serial Monitor"), Qt::ElideRight, m_title->width()));
    m_status->setText(m_status->fontMetrics().elidedText(m_statusText, Qt::ElideRight, m_status->width()));
    m_status->setToolTip(m_statusText);
}

void MonitorWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor background(m_settings.stringValue("col_bg", "#FFFFFF"));
    background.setAlphaF(m_opacity / 100.0);
    painter.setBrush(background);
    painter.setPen(QPen(QColor(148, 163, 184, 170), 1));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
    for (int inset : {4, 7, 10})
        painter.drawLine(width() - inset, height() - 4, width() - 4, height() - inset);
}

void MonitorWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    emit restoreRequested();
}

void MonitorWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_status) updateStatusLabel();
}

void MonitorWindow::finishDrag(const QPoint &globalPosition)
{
    auto *screen = QGuiApplication::screenAt(globalPosition);
    if (!screen) screen = currentScreen();
    if (!screen) return;
    const QRect area = screen->availableGeometry();
    setMinimumSize(QSize(360, 200).boundedTo(workArea(area).size()));
    if (m_autoSnap) {
        m_corner = nearestCorner(geometry(), area);
        moveInto(cornerGeometry(size(), area, m_corner), true);
    } else moveInto(constrainedGeometry(geometry(), area), true);
}

bool MonitorWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_titleBar) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_snapAnimation->stop();
                m_dragging = true;
                m_pressGlobal = mouse->globalPosition().toPoint();
                m_pressGeometry = geometry();
                if (windowHandle() && windowHandle()->isVisible()) m_titleBar->grabMouse();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            move(m_pressGeometry.topLeft() + mouse->globalPosition().toPoint() - m_pressGlobal);
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_dragging) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_dragging = false;
                if (QWidget::mouseGrabber() == m_titleBar) m_titleBar->releaseMouse();
                finishDrag(mouse->globalPosition().toPoint());
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

Qt::Edges MonitorWindow::edgesAt(const QPoint &point) const
{
    Qt::Edges edges;
    if (point.x() < 8) edges |= Qt::LeftEdge;
    if (point.x() >= width() - 8) edges |= Qt::RightEdge;
    if (point.y() < 8) edges |= Qt::TopEdge;
    if (point.y() >= height() - 8) edges |= Qt::BottomEdge;
    return edges;
}

void MonitorWindow::updateResizeCursor(Qt::Edges edges)
{
    if ((edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::TopEdge)) ||
        (edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::BottomEdge))) setCursor(Qt::SizeFDiagCursor);
    else if ((edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::TopEdge)) ||
        (edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::BottomEdge))) setCursor(Qt::SizeBDiagCursor);
    else if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge)) setCursor(Qt::SizeHorCursor);
    else if (edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge)) setCursor(Qt::SizeVerCursor);
    else unsetCursor();
}

void MonitorWindow::mousePressEvent(QMouseEvent *event)
{
    const auto edges = edgesAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && edges) {
        m_snapAnimation->stop();
        m_resizing = true;
        m_resizeEdges = edges;
        m_pressGlobal = event->globalPosition().toPoint();
        m_pressGeometry = geometry();
        if (windowHandle() && windowHandle()->isVisible()) grabMouse();
        event->accept();
    } else QWidget::mousePressEvent(event);
}

void MonitorWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_resizing) { updateResizeCursor(edgesAt(event->position().toPoint())); return; }
    const QPoint delta = event->globalPosition().toPoint() - m_pressGlobal;
    QRect target = m_pressGeometry;
    if (m_resizeEdges.testFlag(Qt::LeftEdge)) target.setLeft(qMin(target.left() + delta.x(), target.right() - minimumWidth() + 1));
    if (m_resizeEdges.testFlag(Qt::RightEdge)) target.setRight(qMax(target.right() + delta.x(), target.left() + minimumWidth() - 1));
    if (m_resizeEdges.testFlag(Qt::TopEdge)) target.setTop(qMin(target.top() + delta.y(), target.bottom() - minimumHeight() + 1));
    if (m_resizeEdges.testFlag(Qt::BottomEdge)) target.setBottom(qMax(target.bottom() + delta.y(), target.top() + minimumHeight() - 1));
    if (auto *screen = currentScreen()) target = constrainedGeometry(target, screen->availableGeometry());
    setGeometry(target);
    event->accept();
}

void MonitorWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_resizing && event->button() == Qt::LeftButton) {
        m_resizing = false;
        if (QWidget::mouseGrabber() == this) releaseMouse();
        unsetCursor();
        if (m_autoSnap) dockToCorner(m_corner);
        else finishDrag(event->globalPosition().toPoint());
        event->accept();
    } else QWidget::mouseReleaseEvent(event);
}

void MonitorWindow::leaveEvent(QEvent *event)
{
    if (!m_resizing) unsetCursor();
    QWidget::leaveEvent(event);
}
