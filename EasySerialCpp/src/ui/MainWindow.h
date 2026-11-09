#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QVector>

#include "Settings.h"
#include "core/LogWriter.h"
#include "core/SerialController.h"

class QLabel;
class QPushButton;
class QTimer;
class QCloseEvent;
class QJsonArray;
class AppearanceDialog;
class ReceiveView;
class SearchBar;
class SendPanel;
class SerialPanel;
class ToolsDialog;
class MonitorWindow;
class QVBoxLayout;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr, const QString &settingsPath = {});
    ~MainWindow() override;
    void enterMonitorMode();
    void leaveMonitorMode();
    MonitorWindow *monitorWindow() const;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct PendingTransmission
    {
        QByteArray data;
        QString source;
        qint64 confirmed = 0;
        QDateTime confirmedAt;
        QString autoReplyFraming;
        int autoReplyCooldownMs = 0;
    };

    struct RecentAutoReply
    {
        QByteArray payload;
        QString framing;
        qsizetype nextOffset = 0;
        qint64 expiresAt = 0;
    };

    void toggleConnection();
    void setConnected(bool connected);
    void onFrames(const QVector<ParsedFrame> &frames);
    void onAutoReplyFrames(const QVector<ParsedFrame> &frames, const QString &format);
    quint64 sendData(const QByteArray &data,
                     const QString &source = QStringLiteral("Manual send"),
                     const QString &autoReplyFraming = {},
                     int autoReplyCooldownMs = 0);
    bool tryAutoReply(const QByteArray &data, const QString &format,
                      const QJsonArray &rules);
    bool isRecentAutoReplyEcho(const QByteArray &data, const QString &format);
    void rememberAutoReply(const QByteArray &payload, const QString &framing,
                           int cooldownMs);
    void onWriteBytesConfirmed(quint64 requestId, qint64 bytes,
                               const QDateTime &timestamp);
    void onWriteCompleted(quint64 requestId);
    void onWriteFailed(quint64 requestId, qint64 confirmedBytes,
                       const QString &reason);
    void appendTransmitEcho(const QByteArray &data, const QDateTime &timestamp);
    void changeMode(int mode);
    void clearLog();
    void editRules();
    void openTools();
    void openAppearance();
    void setPinned(bool pinned);
    void toggleRecording(bool enabled);
    void changeBaseFont(int delta);
    void updateCounters();
    void updateMonitorControls();

    Settings m_settings;
    SerialController m_serial;
    LogWriter m_logWriter;
    SerialPanel *m_serialPanel = nullptr;
    ReceiveView *m_receive = nullptr;
    SendPanel *m_sendPanel = nullptr;
    SearchBar *m_search = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_txCounter = nullptr;
    QLabel *m_rxCounter = nullptr;
    QLabel *m_fontSize = nullptr;
    QTimer *m_counterTimer = nullptr;
    QPointer<ToolsDialog> m_tools;
    QPointer<MonitorWindow> m_monitor;
    QVBoxLayout *m_receiveLayout = nullptr;
    QByteArray m_normalGeometry;
    QRect m_normalRect;
    QByteArray m_normalSplitter;
    Qt::WindowStates m_normalState;
    bool m_monitorActive = false;
    QHash<quint64, PendingTransmission> m_pendingTransmissions;
    QVector<RecentAutoReply> m_recentAutoReplies;
    quint64 m_logSessionId = 0;
    quint64 m_receivedBytes = 0;
    quint64 m_transmittedBytes = 0;
    bool m_connected = false;
    bool m_connectionBusy = false;
    bool m_closePending = false;
    QElapsedTimer m_autoReplyClock;
    QElapsedTimer m_runtimeClock;
};
