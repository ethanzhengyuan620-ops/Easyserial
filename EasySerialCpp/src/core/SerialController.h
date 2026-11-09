#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QQueue>
#include <QSerialPort>
#include <QTimer>
#include <QVector>
#include <optional>

#include "core/FrameParser.h"

struct SerialParameters
{
    QString port;
    qint32 baud = 115200;
    int dataBits = 8;
    QString parity = QStringLiteral("None");
    QString stopBits = QStringLiteral("1");
    int frameGapMs = 20;
    bool splitNewline = true;
};

class SerialController final : public QObject
{
    Q_OBJECT
public:
    explicit SerialController(QObject *parent = nullptr);

    bool open(const SerialParameters &parameters);
    void close();
    quint64 write(const QByteArray &data);
    bool isOpen() const { return m_serial.isOpen(); }
    void setSplitNewline(bool enabled);
    void setAutoReplySplitNewline(bool enabled);
    QString lastWriteError() const { return m_lastWriteError; }
    qsizetype pendingWriteBytes() const { return m_pendingWriteBytes; }

signals:
    void opened();
    void closed();
    void framesReceived(const QVector<ParsedFrame> &frames);
    void autoReplyFramesReceived(const QVector<ParsedFrame> &frames,
                                 const QString &format);
    void writeBytesConfirmed(quint64 requestId, qint64 bytes,
                             const QDateTime &timestamp);
    void writeCompleted(quint64 requestId);
    void writeFailed(quint64 requestId, qint64 confirmedBytes,
                     const QString &reason);
    void errorOccurred(const QString &message);

private slots:
    void onReadyRead();
    void onGapTimeout();
    void onBytesWritten(qint64 bytes);
    void onWriteWatchdogTimeout();
    void onSerialError(QSerialPort::SerialPortError error);
    void flushFrameSignals();
    void pumpWrites();

private:
    struct PendingWrite
    {
        quint64 id = 0;
        QByteArray data;
        qsizetype submitted = 0;
        qsizetype confirmed = 0;
        int zeroWriteRetries = 0;
    };

    static constexpr qsizetype MaxPendingWriteBytes = 256 * 1024;
    static constexpr qsizetype MaxPendingWriteRequests = 256;
    static constexpr qsizetype WriteChunkSize = 4096;

    void queueFrames(QVector<ParsedFrame> &destination, QVector<ParsedFrame> frames);
    void flushDisplayFrames();
    void closeInternal(const QString &pendingWriteReason);
    void failPendingWrites(const QString &reason);
    void resetParsers();

    QSerialPort m_serial;
    QTimer m_gapTimer;
    QTimer m_frameEmitTimer;
    QTimer m_writePumpTimer;
    QTimer m_writeWatchdogTimer;
    QElapsedTimer m_sinceReceive;
    FrameParser m_displayParser;
    FrameParser m_autoReplyParser;
    QVector<ParsedFrame> m_pendingDisplayFrames;
    QVector<ParsedFrame> m_pendingAutoReplyFrames;
    QQueue<PendingWrite> m_writeQueue;
    std::optional<PendingWrite> m_activeWrite;
    int m_frameGapMs = 20;
    bool m_splitNewline = true;
    bool m_autoReplySplitNewline = true;
    bool m_closing = false;
    bool m_opening = false;
    qint64 m_inFlightBytes = 0;
    qint32 m_baudRate = 115200;
    qsizetype m_pendingWriteBytes = 0;
    quint64 m_nextWriteId = 1;
    QString m_lastWriteError;
};
