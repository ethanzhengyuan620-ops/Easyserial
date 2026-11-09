#include "ui/MainWindow.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QStringConverter>
#include <QStringDecoder>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

#include "Theme.h"
#include "core/AutoReplyEngine.h"
#include "core/HexUtils.h"
#include "ui/AppearanceDialog.h"
#include "ui/ReceiveView.h"
#include "ui/MonitorWindow.h"
#include "ui/RuleEditorDialog.h"
#include "ui/SearchBar.h"
#include "ui/SendPanel.h"
#include "ui/SerialPanel.h"
#include "ui/ToolsDialog.h"

namespace {
QString canonicalAutoReplyFraming(const QString &format)
{
    return format.compare(QStringLiteral("HEX"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("HEX") : QStringLiteral("ASCII");
}

QByteArray nextAutoReplyEchoFrame(const QByteArray &payload, qsizetype offset,
                                  const QString &framing)
{
    if (offset < 0 || offset >= payload.size())
        return {};
    qsizetype end = qMin(payload.size(), offset + FrameParser::MaxFrame);
    if (canonicalAutoReplyFraming(framing) == QStringLiteral("ASCII")) {
        const qsizetype newline = payload.indexOf('\n', offset);
        if (newline >= 0 && newline < end)
            end = newline + 1;
    }
    return payload.sliced(offset, end - offset);
}

bool autoReplyEchoEquals(QByteArray candidate, QByteArray expected,
                         const QString &framing)
{
    if (candidate == expected)
        return true;
    if (canonicalAutoReplyFraming(framing) != QStringLiteral("ASCII"))
        return false;
    while (candidate.endsWith('\r') || candidate.endsWith('\n'))
        candidate.chop(1);
    while (expected.endsWith('\r') || expected.endsWith('\n'))
        expected.chop(1);
    return candidate == expected;
}

QString logPayload(const QByteArray &data)
{
    QStringDecoder decoder(QStringDecoder::Utf8,
                           QStringConverter::Flag::Stateless);
    const QString text = decoder(data);
    if (decoder.hasError())
        return QStringLiteral("HEX ") + HexUtils::spaced(data);

    QString escaped = QStringLiteral("TEXT ");
    escaped.reserve(text.size() + 5);
    for (const QChar character : text) {
        switch (character.unicode()) {
        case '\\': escaped += QStringLiteral("\\\\"); break;
        case '\r': escaped += QStringLiteral("\\r"); break;
        case '\n': escaped += QStringLiteral("\\n"); break;
        case '\t': escaped += QStringLiteral("\\t"); break;
        case 0: escaped += QStringLiteral("\\0"); break;
        default:
            if (character.isPrint()) {
                escaped += character;
            } else {
                escaped += QStringLiteral("\\u%1")
                    .arg(static_cast<uint>(character.unicode()), 4, 16, QLatin1Char('0')).toUpper();
            }
        }
    }
    return escaped;
}
}

MainWindow::MainWindow(QWidget *parent, const QString &settingsPath)
    : QMainWindow(parent), m_settings(settingsPath)
{
    m_runtimeClock.start();
    setWindowTitle(QStringLiteral("EasySerial"));
    resize(1280, 800);
    const QByteArray geometry = QByteArray::fromBase64(
        m_settings.stringValue("geometry").toLatin1());
    if (!geometry.isEmpty()) restoreGeometry(geometry);

    auto *central = new QWidget(this);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    setCentralWidget(central);

    m_serialPanel = new SerialPanel(m_settings, central);
    root->addWidget(m_serialPanel);
    auto *separator = new QFrame(central);
    separator->setFixedWidth(1);
    separator->setStyleSheet(QStringLiteral("background:#E2E8F0;"));
    root->addWidget(separator);

    auto *receiveContainer = new QWidget(central);
    receiveContainer->setObjectName(QStringLiteral("receiveSurface"));
    auto *receiveLayout = new QVBoxLayout(receiveContainer);
    m_receiveLayout = receiveLayout;
    receiveLayout->setContentsMargins(0, 0, 0, 0);
    receiveLayout->setSpacing(0);
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(16, 12, 16, 12);
    toolbar->setSpacing(8);
    auto *receiveTitle = new QLabel(QStringLiteral("Serial Monitor"), receiveContainer);
    receiveTitle->setObjectName(QStringLiteral("sectionTitle"));
    toolbar->addWidget(receiveTitle);
    toolbar->addStretch();
    auto *monitorMode = new QPushButton(QStringLiteral("Monitor Mode"), receiveContainer);
    monitorMode->setObjectName(QStringLiteral("monitorMode"));
    monitorMode->setToolTip(QStringLiteral("Show a compact always-on-top monitor without disconnecting"));
    toolbar->addWidget(monitorMode);
    connect(monitorMode, &QPushButton::clicked, this, &MainWindow::enterMonitorMode);
    auto *decrease = new QPushButton(QStringLiteral("A−"), receiveContainer);
    auto *increase = new QPushButton(QStringLiteral("A+"), receiveContainer);
    auto *appearance = new QPushButton(QStringLiteral("Appearance"), receiveContainer);
    m_fontSize = new QLabel(QString::number(m_settings.intValue("font_size", 9)), receiveContainer);
    m_fontSize->setObjectName(QStringLiteral("currentFontSize"));
    m_fontSize->setToolTip(QStringLiteral("Font size of the active data pane. A− / A+ applies this size to all data panes."));
    for (QPushButton *button : {decrease, increase, appearance})
        button->setMinimumHeight(28);
    toolbar->addWidget(decrease);
    toolbar->addWidget(m_fontSize);
    toolbar->addWidget(increase);
    toolbar->addWidget(appearance);
    receiveLayout->addLayout(toolbar);
    m_search = new SearchBar(receiveContainer);
    receiveLayout->addWidget(m_search);
    m_receive = new ReceiveView(m_settings, receiveContainer);
    receiveLayout->addWidget(m_receive, 1);

    m_sendPanel = new SendPanel(m_settings, central);
    connect(m_receive, &ReceiveView::fontSizeChanged, this, [this](int size) {
        m_fontSize->setText(QString::number(size));
    });
    connect(m_sendPanel, &SendPanel::fontSizeChanged, this, [this](int size) {
        m_fontSize->setText(QString::number(size));
    });
    m_sendPanel->setSendEnabled(false);
    m_fontSize->setText(QString::number(m_receive->fontSize()));
    auto *splitter = new QSplitter(Qt::Vertical, central);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    splitter->addWidget(receiveContainer);
    splitter->addWidget(m_sendPanel);
    splitter->setSizes({600, 200});
    splitter->setCollapsible(0, false);
    const auto splitterState = QByteArray::fromBase64(m_settings.stringValue("splitter").toLatin1());
    if (!splitterState.isEmpty()) splitter->restoreState(splitterState);
    root->addWidget(splitter, 1);

    auto *bar = new QStatusBar(this);
    setStatusBar(bar);
    m_status = new QLabel(QStringLiteral("● Disconnected"), bar);
    m_txCounter = new QLabel(QStringLiteral("TX: 0 bytes"), bar);
    m_rxCounter = new QLabel(QStringLiteral("RX: 0 bytes"), bar);
    bar->addWidget(m_status);
    bar->addPermanentWidget(m_txCounter);
    bar->addPermanentWidget(m_rxCounter);

    connect(m_serialPanel, &SerialPanel::connectToggled, this, &MainWindow::toggleConnection);
    connect(m_serialPanel, &SerialPanel::modeChanged, this, &MainWindow::changeMode);
    connect(m_serialPanel, &SerialPanel::autoScrollChanged, m_receive, &ReceiveView::setAutoScroll);
    connect(m_serialPanel, &SerialPanel::autoScrollChanged, this, [this](bool enabled) {
        if (m_monitor) m_monitor->setAutoScrollEnabled(enabled);
    });
    connect(m_serialPanel, &SerialPanel::timestampsChanged, m_receive, &ReceiveView::setShowTimestamps);
    connect(m_serialPanel, &SerialPanel::timestampIntervalChanged,
            m_receive, &ReceiveView::setTimestampInterval);
    connect(m_serialPanel, &SerialPanel::clearClicked, this, &MainWindow::clearLog);
    connect(m_serialPanel, &SerialPanel::rulesClicked, this, &MainWindow::editRules);
    connect(m_serialPanel, &SerialPanel::toolsClicked, this, &MainWindow::openTools);
    connect(m_serialPanel, &SerialPanel::pinToggled, this, &MainWindow::setPinned);
    connect(m_serialPanel, &SerialPanel::recordToggled, this, &MainWindow::toggleRecording);
    connect(m_sendPanel, &SendPanel::sendBytes, this,
            [this](const QByteArray &data) { sendData(data); });
    connect(m_sendPanel, &SendPanel::hint, this,
            [this](const QString &message) { statusBar()->showMessage(message, 4000); });

    connect(&m_serial, &SerialController::opened, this, [this] { setConnected(true); });
    connect(&m_serial, &SerialController::closed, this, [this] { setConnected(false); });
    connect(&m_serial, &SerialController::framesReceived, this, &MainWindow::onFrames);
    connect(&m_serial, &SerialController::autoReplyFramesReceived,
            this, &MainWindow::onAutoReplyFrames);
    connect(&m_serial, &SerialController::writeBytesConfirmed,
            this, &MainWindow::onWriteBytesConfirmed);
    connect(&m_serial, &SerialController::writeCompleted,
            this, &MainWindow::onWriteCompleted);
    connect(&m_serial, &SerialController::writeFailed,
            this, &MainWindow::onWriteFailed);
    connect(&m_serial, &SerialController::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(message, 6000);
        if (!m_serial.isOpen()) setConnected(false);
        // The normal status bar is hidden in monitor mode. Keep the failure
        // visible there too, including the full message in the hover tooltip.
        if (m_monitorActive && m_monitor) {
            m_status->setText(QStringLiteral("● %1").arg(message));
            updateMonitorControls();
        }
    });
    connect(&m_logWriter, &LogWriter::openResult, this,
            [this](quint64 sessionId, bool success, const QString &message) {
        if (sessionId != m_logSessionId)
            return;
        if (!success) {
            m_logSessionId = 0;
            QMessageBox::warning(this, QStringLiteral("Recording Failed"), message);
            m_serialPanel->setRecordChecked(false);
        } else if (m_logWriter.isLogging()) {
            statusBar()->showMessage(QStringLiteral("Recording → %1").arg(message), 4000);
        }
    });
    connect(&m_logWriter, &LogWriter::writeError, this,
            [this](quint64 sessionId, const QString &message) {
        if (sessionId != m_logSessionId)
            return;
        m_logSessionId = 0;
        m_serialPanel->setRecordChecked(false);
        statusBar()->showMessage(QStringLiteral("Recording stopped: %1").arg(message), 8000);
    });
    connect(&m_logWriter, &QThread::finished, this, [this] {
        if (m_closePending)
            QTimer::singleShot(0, this, [this] { close(); });
    });
    connect(m_search, &SearchBar::searchRequested, this,
            [this](const QString &text, bool regex, bool cs, bool backwards, bool word) {
        m_receive->findNext(text, regex, cs, backwards, word);
    });
    connect(m_receive, &ReceiveView::searchStatusChanged, m_search, &SearchBar::setResult);
    connect(m_search, &SearchBar::searchClosed, m_receive, &ReceiveView::clearSearch);
    connect(decrease, &QPushButton::clicked, this, [this] { changeBaseFont(-1); });
    connect(increase, &QPushButton::clicked, this, [this] { changeBaseFont(1); });
    connect(appearance, &QPushButton::clicked, this, &MainWindow::openAppearance);
    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, m_search, &SearchBar::showAndFocus);

    m_counterTimer = new QTimer(this);
    m_counterTimer->setInterval(100);
    connect(m_counterTimer, &QTimer::timeout, this, &MainWindow::updateCounters);
    m_counterTimer->start();

    m_serial.setAutoReplySplitNewline(
        canonicalAutoReplyFraming(
            m_settings.stringValue("auto_reply_framing", "ASCII")) ==
            QStringLiteral("ASCII"));

    if (m_settings.boolValue("pin", false))
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
}

MainWindow::~MainWindow()
{
    // Fallback for application-quit paths that bypass closeEvent(). The tools
    // window intentionally has no parent so it remains an independent window.
    if (m_serial.isOpen())
        m_serial.close();
    if (m_monitor) {
        if (auto *view = m_monitor->releaseView()) view->setParent(centralWidget());
        delete m_monitor.data();
    }
    if (m_tools)
        delete m_tools.data();
    m_logWriter.stopLogging();
}

MonitorWindow *MainWindow::monitorWindow() const
{
    return m_monitor.data();
}

void MainWindow::enterMonitorMode()
{
    if (m_monitorActive || m_closePending) return;
    m_normalGeometry = saveGeometry();
    m_normalRect = geometry();
    m_normalState = windowState() & ~Qt::WindowMinimized;
    if (auto *splitter = findChild<QSplitter *>(QStringLiteral("mainSplitter")))
        m_normalSplitter = splitter->saveState();
    if (!m_monitor) {
        m_monitor = new MonitorWindow(m_settings);
        m_monitor->setAttribute(Qt::WA_DontShowOnScreen, testAttribute(Qt::WA_DontShowOnScreen));
        connect(m_monitor, &MonitorWindow::restoreRequested, this, &MainWindow::leaveMonitorMode);
        connect(m_monitor, &MonitorWindow::connectionToggleRequested, this, &MainWindow::toggleConnection);
        connect(m_monitor, &MonitorWindow::autoScrollToggled, m_serialPanel, &SerialPanel::setAutoScrollChecked);
        connect(m_monitor, &MonitorWindow::clearRequested, this, &MainWindow::clearLog);
    }
    m_monitorActive = true;
    m_receiveLayout->removeWidget(m_receive);
    m_monitor->attachView(m_receive);
    updateMonitorControls();
    m_monitor->setAutoScrollEnabled(m_serialPanel->autoScrollEnabled());
    // Show the new shell before hiding the old one: there is always a visible
    // primary window. No serial, send-timer or logging lifecycle is touched.
    m_monitor->openOnScreen(screen());
    hide();
}

void MainWindow::leaveMonitorMode()
{
    if (!m_monitorActive || !m_monitor) return;
    m_monitorActive = false;
    if (auto *view = m_monitor->releaseView()) {
        m_receiveLayout->addWidget(view, 1);
        view->show();
    }
    restoreGeometry(m_normalGeometry);
    // The original shell still exists. Preserve its exact client rectangle if
    // its screen is still available; restoreGeometry may add frame offsets.
    if (!(m_normalState & (Qt::WindowMaximized | Qt::WindowFullScreen))) {
        if (auto *screen = QGuiApplication::screenAt(m_normalRect.center());
            screen && screen->availableGeometry().contains(m_normalRect)) setGeometry(m_normalRect);
    }
    if (auto *splitter = findChild<QSplitter *>(QStringLiteral("mainSplitter")))
        splitter->restoreState(m_normalSplitter);
    setWindowState(m_normalState);
    show();
    m_monitor->hide();
    raise();
    activateWindow();
}

void MainWindow::toggleConnection()
{
    if (m_connectionBusy || m_closePending) return;
    if (m_serial.isOpen()) {
        m_connectionBusy = true;
        m_status->setText(QStringLiteral("● Disconnecting…"));
        updateMonitorControls();
        m_serialPanel->setDisconnecting(true);
        m_sendPanel->stopTimer();
        m_serial.close();
        return;
    }
    const SerialParameters parameters = m_serialPanel->parameters();
    if (parameters.port.isEmpty()) {
        if (m_monitorActive && m_monitor) {
            m_status->setText(QStringLiteral("Select a serial port in the main window first (Restore)."));
            updateMonitorControls();
        } else QMessageBox::warning(this, QStringLiteral("Notice"), QStringLiteral("Please select a serial port."));
        return;
    }
    m_settings.setValue("port", parameters.port, false);
    m_settings.setValue("baud", parameters.baud, false);
    m_settings.setValue("databits", parameters.dataBits, false);
    m_settings.setValue("parity", parameters.parity, false);
    m_settings.setValue("stopbits", parameters.stopBits, false);
    m_settings.save();
    m_serial.setAutoReplySplitNewline(
        canonicalAutoReplyFraming(
            m_settings.stringValue("auto_reply_framing", "ASCII")) ==
            QStringLiteral("ASCII"));
    m_connectionBusy = true;
    m_serialPanel->setConnecting(true);
    m_status->setText(QStringLiteral("● Connecting to %1…").arg(parameters.port));
    updateMonitorControls();
    if (!m_serial.open(parameters)) {
        m_connectionBusy = false;
        m_serialPanel->setConnected(false);
        updateMonitorControls();
    }
}

void MainWindow::setConnected(bool connected)
{
    m_connected = connected;
    m_connectionBusy = false;
    m_autoReplyClock.invalidate();
    m_recentAutoReplies.clear();
    m_serialPanel->setConnected(connected);
    m_sendPanel->setSendEnabled(connected);
    const SerialParameters parameters = m_serialPanel->parameters();
    if (connected) {
        m_status->setText(QStringLiteral("● Connected to %1 @ %2 bps")
                          .arg(parameters.port).arg(parameters.baud));
        m_status->setStyleSheet(QStringLiteral("color:#15803D;padding-left:8px;"));
        setWindowTitle(QStringLiteral("EasySerial — %1 @ %2")
                       .arg(parameters.port).arg(parameters.baud));
    } else {
        m_status->setText(QStringLiteral("● Disconnected"));
        m_status->setStyleSheet(QStringLiteral("color:#64748B;padding-left:8px;"));
        setWindowTitle(QStringLiteral("EasySerial"));
    }
    updateMonitorControls();
}

void MainWindow::onFrames(const QVector<ParsedFrame> &frames)
{
    for (const ParsedFrame &frame : frames) {
        m_receivedBytes += static_cast<quint64>(frame.data.size());
        m_receive->appendFrame(frame.data, frame.timestamp);
        if (m_logWriter.isLogging()) {
            m_logWriter.enqueue(QStringLiteral("%1 %2\n")
                .arg(frame.timestamp.toString(QStringLiteral("[HH:mm:ss.zzz]")),
                     logPayload(frame.data))
                .toUtf8());
        }
    }
}

quint64 MainWindow::sendData(const QByteArray &data, const QString &source,
                             const QString &autoReplyFraming,
                             int autoReplyCooldownMs)
{
    if (!m_connected || !m_serial.isOpen()) {
        statusBar()->showMessage(QStringLiteral("Cannot send: no serial port is connected."), 3000);
        m_sendPanel->stopTimer();
        return 0;
    }
    const quint64 requestId = m_serial.write(data);
    if (requestId == 0) {
        const QString reason = m_serial.lastWriteError().isEmpty()
            ? QStringLiteral("The serial port did not accept the send request") : m_serial.lastWriteError();
        statusBar()->showMessage(QStringLiteral("Send failed: %1").arg(reason), 6000);
        m_sendPanel->stopTimer();
        return 0;
    }

    PendingTransmission transmission;
    transmission.data = data;
    transmission.source = source;
    transmission.autoReplyFraming = autoReplyFraming;
    transmission.autoReplyCooldownMs = autoReplyCooldownMs;
    m_pendingTransmissions.insert(requestId, std::move(transmission));
    statusBar()->showMessage(QStringLiteral("%1: %2 bytes queued (%3 bytes pending)")
        .arg(source).arg(data.size()).arg(m_serial.pendingWriteBytes()), 1800);
    return requestId;
}

void MainWindow::onWriteBytesConfirmed(quint64 requestId, qint64 bytes,
                                       const QDateTime &timestamp)
{
    if (bytes <= 0)
        return;

    auto it = m_pendingTransmissions.find(requestId);
    if (it == m_pendingTransmissions.end())
        return;

    const qint64 remaining = static_cast<qint64>(it->data.size()) - it->confirmed;
    const qint64 accounted = qMin(bytes, qMax<qint64>(0, remaining));
    if (accounted <= 0)
        return;

    it->confirmed += accounted;
    it->confirmedAt = timestamp;
    m_transmittedBytes += static_cast<quint64>(accounted);
}

void MainWindow::onWriteCompleted(quint64 requestId)
{
    const auto it = m_pendingTransmissions.find(requestId);
    if (it == m_pendingTransmissions.end())
        return;

    const PendingTransmission transmission = it.value();
    m_pendingTransmissions.erase(it);
    const qint64 shownBytes = qBound<qint64>(0, transmission.confirmed,
                                             transmission.data.size());
    if (shownBytes > 0) {
        appendTransmitEcho(transmission.data.first(shownBytes),
            transmission.confirmedAt.isValid() ? transmission.confirmedAt
                                               : QDateTime::currentDateTime());
    }
    if (!transmission.autoReplyFraming.isEmpty()) {
        rememberAutoReply(transmission.data, transmission.autoReplyFraming,
                          transmission.autoReplyCooldownMs);
    }
    statusBar()->showMessage(QStringLiteral("%1: serial driver confirmed %2 bytes written")
                             .arg(transmission.source).arg(shownBytes), 1800);
}

void MainWindow::onWriteFailed(quint64 requestId, qint64 confirmedBytes,
                               const QString &reason)
{
    const auto it = m_pendingTransmissions.find(requestId);
    if (it == m_pendingTransmissions.end())
        return;

    PendingTransmission transmission = it.value();
    m_pendingTransmissions.erase(it);
    const qint64 actual = qBound<qint64>(0,
        qMax(transmission.confirmed, confirmedBytes), transmission.data.size());
    if (actual > transmission.confirmed) {
        m_transmittedBytes += static_cast<quint64>(actual - transmission.confirmed);
        transmission.confirmed = actual;
    }
    if (actual > 0) {
        appendTransmitEcho(transmission.data.first(actual),
            transmission.confirmedAt.isValid() ? transmission.confirmedAt
                                               : QDateTime::currentDateTime());
        if (!transmission.autoReplyFraming.isEmpty()) {
            rememberAutoReply(transmission.data.first(actual),
                              transmission.autoReplyFraming,
                              transmission.autoReplyCooldownMs);
        }
    }

    m_sendPanel->stopTimer();
    statusBar()->showMessage(
        QStringLiteral("%1 failed: %2 bytes requested, %3 bytes confirmed by the serial driver; %4")
            .arg(transmission.source).arg(transmission.data.size()).arg(actual).arg(reason),
        8000);
}

void MainWindow::appendTransmitEcho(const QByteArray &data,
                                    const QDateTime &timestamp)
{
    // Keep one very large manual/timed send from becoming a single frame that
    // can exceed ReceiveModel's configured display-row limit by itself.
    for (qsizetype offset = 0; offset < data.size(); offset += FrameParser::MaxFrame) {
        const qsizetype length = qMin(FrameParser::MaxFrame, data.size() - offset);
        m_receive->appendTransmit(data.sliced(offset, length), timestamp);
    }
}

void MainWindow::onAutoReplyFrames(const QVector<ParsedFrame> &frames,
                                   const QString &format)
{
    if (!m_connected || !m_settings.boolValue("auto_reply_enabled", false))
        return;
    if (canonicalAutoReplyFraming(
            m_settings.stringValue("auto_reply_framing", "ASCII")) != format) {
        return;
    }

    const QJsonArray rules = m_settings.arrayValue("auto_reply_rules");
    for (const ParsedFrame &frame : frames)
        tryAutoReply(frame.data, format, rules);
}

bool MainWindow::tryAutoReply(const QByteArray &data, const QString &format,
                              const QJsonArray &rules)
{
    if (isRecentAutoReplyEcho(data, format))
        return false;

    const int cooldown = qMax(0, m_settings.intValue("auto_reply_cooldown", 100));
    if (m_autoReplyClock.isValid() && m_autoReplyClock.elapsed() < cooldown)
        return false;

    // The selected parser defines the frame boundary. Every rule then sees the
    // same raw frame, so the configured top-to-bottom order also works when
    // ASCII and HEX rules are mixed.
    const AutoReplyResult result = AutoReplyEngine::find(data, rules);
    if (!result.error.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Auto reply configuration error: %1").arg(result.error),
                                 5000);
    }
    if (!result.matched)
        return false;

    const quint64 requestId = sendData(result.payload,
        QStringLiteral("Auto reply [%1]").arg(result.ruleName),
        format, cooldown);
    if (requestId == 0)
        return false;

    // Cooldown starts only after the reply has actually entered our bounded queue.
    m_autoReplyClock.restart();
    return true;
}

bool MainWindow::isRecentAutoReplyEcho(const QByteArray &data, const QString &format)
{
    const qint64 now = m_runtimeClock.elapsed();
    for (qsizetype index = m_recentAutoReplies.size(); index > 0; --index) {
        if (m_recentAutoReplies.at(index - 1).expiresAt <= now)
            m_recentAutoReplies.remove(index - 1);
    }

    const QString framing = canonicalAutoReplyFraming(format);
    for (qsizetype index = 0; index < m_recentAutoReplies.size(); ++index) {
        RecentAutoReply &recent = m_recentAutoReplies[index];
        if (recent.framing != framing)
            continue;
        const QByteArray expected = nextAutoReplyEchoFrame(
            recent.payload, recent.nextOffset, recent.framing);
        if (expected.isEmpty() || !autoReplyEchoEquals(data, expected, framing))
            continue;
        recent.nextOffset += expected.size();
        if (recent.nextOffset >= recent.payload.size())
            m_recentAutoReplies.remove(index);
        return true;
    }
    return false;
}

void MainWindow::rememberAutoReply(const QByteArray &payload,
                                   const QString &framing, int cooldownMs)
{
    const qint64 baud = qMax<qint64>(1, m_serialPanel->parameters().baud);
    const qint64 wireTimeMs = (static_cast<qint64>(payload.size()) * 12 * 1000
                               + baud - 1) / baud;
    const qint64 window = qMax<qint64>(500,
        qMax(static_cast<qint64>(cooldownMs) * 2, wireTimeMs + 500));
    m_recentAutoReplies.push_back({payload, canonicalAutoReplyFraming(framing),
                                   0, m_runtimeClock.elapsed() + window});
    while (m_recentAutoReplies.size() > 16)
        m_recentAutoReplies.remove(0);
}

void MainWindow::changeMode(int mode)
{
    // Display packets follow serial idle gaps in every mode, not each ASCII LF.
    // Auto-reply has its own independently configured framing pipeline.
    m_serial.setSplitNewline(false);
    m_receive->setMode(static_cast<DisplayMode>(qBound(0, mode, 2)));
}

void MainWindow::clearLog()
{
    m_receive->clearAll();
    m_receivedBytes = 0;
    m_transmittedBytes = 0;
    updateCounters();
}

void MainWindow::editRules()
{
    RuleEditorDialog dialog(m_settings.arrayValue("rules"), this);
    if (dialog.exec() != QDialog::Accepted) return;
    m_settings.setArray("rules", dialog.rules());
    m_receive->setRules(dialog.rules());
}

void MainWindow::openTools()
{
    if (!m_tools) {
        m_tools = new ToolsDialog(m_settings);
        m_tools->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_tools, &QObject::destroyed, this, [this] {
            if (m_closePending)
                QTimer::singleShot(0, this, [this] { close(); });
        });
        connect(m_tools, &ToolsDialog::autoReplyFramingChanged,
                this, [this](const QString &format) {
            m_serial.setAutoReplySplitNewline(
                canonicalAutoReplyFraming(format) == QStringLiteral("ASCII"));
        });
    }
    if (m_tools->isMinimized()) m_tools->showNormal(); else m_tools->show();
    m_tools->raise();
    m_tools->activateWindow();
}

void MainWindow::openAppearance()
{
    AppearanceDialog dialog(m_settings, this);
    connect(&dialog, &AppearanceDialog::changed, this, [this] {
        m_receive->applyAppearance();
        m_sendPanel->applyAppearance();
        m_fontSize->setText(QString::number(m_receive->fontSize()));
    });
    dialog.exec();
}

void MainWindow::setPinned(bool pinned)
{
    m_settings.setValue("pin", pinned);
    setWindowFlag(Qt::WindowStaysOnTopHint, pinned);
    show();
}

void MainWindow::toggleRecording(bool enabled)
{
    if (!enabled) {
        m_logSessionId = 0;
        m_logWriter.requestStop();
        return;
    }
    const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose Log Folder"));
    if (folder.isEmpty()) {
        m_serialPanel->setRecordChecked(false);
        return;
    }
    const QString name = QStringLiteral("easyserial_%1.log")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const quint64 sessionId = m_logWriter.startLogging(QDir(folder).filePath(name));
    if (sessionId == 0) {
        QMessageBox::warning(this, QStringLiteral("Recording Failed"), QStringLiteral("The log writer is already running."));
        m_serialPanel->setRecordChecked(false);
    } else {
        m_logSessionId = sessionId;
    }
}

void MainWindow::changeBaseFont(int delta)
{
    const int size = qBound(6, m_fontSize->text().toInt() + delta, 36);
    m_settings.setValue("font_size", size, false);
    m_settings.setArray("receive_font_sizes", {size, size, size}, false);
    m_settings.setValue("send_font_size", size);
    m_fontSize->setText(QString::number(size));
    m_receive->applyAppearance();
    m_sendPanel->applyAppearance();
}

void MainWindow::updateCounters()
{
    m_txCounter->setText(QStringLiteral("TX: %L1 bytes").arg(m_transmittedBytes));
    m_rxCounter->setText(QStringLiteral("RX: %L1 bytes").arg(m_receivedBytes));
    updateMonitorControls();
}

void MainWindow::updateMonitorControls()
{
    if (m_monitor)
        m_monitor->setConnectionStatus(m_status->text(), m_connected, m_connectionBusy);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_monitorActive) leaveMonitorMode();
    m_closePending = true;
    m_sendPanel->stopTimer();
    if (m_serial.isOpen())
        m_serial.close();
    if (m_logWriter.isRunning()) {
        m_logSessionId = 0;
        m_logWriter.requestStop();
    }
    if (m_tools)
        m_tools->close();
    if (m_logWriter.isRunning() || m_tools) {
        statusBar()->showMessage(QStringLiteral("Waiting for background file operations to finish…"));
        event->ignore();
        return;
    }
    m_settings.setValue("geometry", QString::fromLatin1(saveGeometry().toBase64()), false);
    if (auto *splitter = findChild<QSplitter *>(QStringLiteral("mainSplitter")))
        m_settings.setValue("splitter", QString::fromLatin1(splitter->saveState().toBase64()), false);
    m_settings.save();
    m_closePending = false;
    QMainWindow::closeEvent(event);
}
