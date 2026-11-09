#include "core/SerialController.h"

#include <QMap>
#include <limits>
#include <utility>

SerialController::SerialController(QObject *parent)
    : QObject(parent)
{
    connect(&m_serial, &QSerialPort::readyRead, this, &SerialController::onReadyRead);
    connect(&m_serial, &QSerialPort::bytesWritten,
            this, &SerialController::onBytesWritten);
    connect(&m_serial, &QSerialPort::errorOccurred,
            this, &SerialController::onSerialError);

    m_gapTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_gapTimer, &QTimer::timeout, this, &SerialController::onGapTimeout);

    // Serial processing remains immediate; only GUI delivery is coalesced.
    m_frameEmitTimer.setSingleShot(true);
    m_frameEmitTimer.setInterval(5);
    connect(&m_frameEmitTimer, &QTimer::timeout,
            this, &SerialController::flushFrameSignals);

    m_writePumpTimer.setSingleShot(true);
    connect(&m_writePumpTimer, &QTimer::timeout, this, &SerialController::pumpWrites);
    m_writeWatchdogTimer.setSingleShot(true);
    connect(&m_writeWatchdogTimer, &QTimer::timeout,
            this, &SerialController::onWriteWatchdogTimeout);
}

bool SerialController::open(const SerialParameters &parameters)
{
    if (m_serial.isOpen())
        close();

    resetParsers();
    failPendingWrites(QStringLiteral("Serial port reconnected; pending sends were cancelled"));
    m_frameGapMs = qMax(5, parameters.frameGapMs);
    m_baudRate = qMax<qint32>(1, parameters.baud);
    m_splitNewline = parameters.splitNewline;
    m_lastWriteError.clear();

    const QMap<int, QSerialPort::DataBits> dataBits{
        {5, QSerialPort::Data5}, {6, QSerialPort::Data6},
        {7, QSerialPort::Data7}, {8, QSerialPort::Data8}
    };
    const QMap<QString, QSerialPort::Parity> parity{
        {QStringLiteral("None"), QSerialPort::NoParity},
        {QStringLiteral("Odd"), QSerialPort::OddParity},
        {QStringLiteral("Even"), QSerialPort::EvenParity},
        {QStringLiteral("Mark"), QSerialPort::MarkParity},
        {QStringLiteral("Space"), QSerialPort::SpaceParity}
    };
    const QMap<QString, QSerialPort::StopBits> stopBits{
        {QStringLiteral("1"), QSerialPort::OneStop},
        {QStringLiteral("1.5"), QSerialPort::OneAndHalfStop},
        {QStringLiteral("2"), QSerialPort::TwoStop}
    };

    m_opening = true;
    m_closing = false;
    m_serial.clearError();
    m_serial.setPortName(parameters.port);
    const bool configured =
        m_serial.setBaudRate(parameters.baud) &&
        m_serial.setDataBits(dataBits.value(parameters.dataBits, QSerialPort::Data8)) &&
        m_serial.setParity(parity.value(parameters.parity, QSerialPort::NoParity)) &&
        m_serial.setStopBits(stopBits.value(parameters.stopBits, QSerialPort::OneStop)) &&
        m_serial.setFlowControl(QSerialPort::NoFlowControl);
    if (!configured) {
        const QString message = QStringLiteral("Unsupported serial parameters: %1")
                                    .arg(m_serial.errorString());
        m_opening = false;
        emit errorOccurred(message);
        return false;
    }

    const bool success = m_serial.open(QIODevice::ReadWrite);
    m_opening = false;
    if (!success) {
        emit errorOccurred(QStringLiteral("Failed to open serial port: %1").arg(m_serial.errorString()));
        return false;
    }

    m_gapTimer.start(qBound(2, m_frameGapMs / 4, 10));
    emit opened();
    return true;
}

void SerialController::close()
{
    closeInternal(QStringLiteral("Serial port disconnected; pending sends were cancelled"));
}

void SerialController::closeInternal(const QString &pendingWriteReason)
{
    if (m_closing)
        return;

    const bool wasOpen = m_serial.isOpen();
    const bool hadPendingWrites = m_activeWrite.has_value() || !m_writeQueue.isEmpty();
    if (!wasOpen && !hadPendingWrites) {
        resetParsers();
        return;
    }

    m_closing = true;
    m_gapTimer.stop();
    m_frameEmitTimer.stop();
    m_writePumpTimer.stop();
    m_writeWatchdogTimer.stop();

    queueFrames(m_pendingDisplayFrames, m_displayParser.flush());
    flushDisplayFrames();
    m_autoReplyParser.clear();
    m_pendingAutoReplyFrames.clear();
    m_sinceReceive.invalidate();

    if (wasOpen) {
        // Prevent queued OS-buffer data from leaking out after disconnect.
        m_serial.clear(QSerialPort::Output);
        m_serial.close();
    }
    failPendingWrites(pendingWriteReason);

    m_closing = false;
    if (wasOpen)
        emit closed();
}

quint64 SerialController::write(const QByteArray &data)
{
    if (data.isEmpty()) {
        m_lastWriteError = QStringLiteral("Send data is empty");
        return 0;
    }
    if (!m_serial.isOpen() || m_closing || m_opening) {
        m_lastWriteError = QStringLiteral("Serial port is not connected");
        return 0;
    }

    const qsizetype requestCount = static_cast<qsizetype>(m_writeQueue.size()) +
        (m_activeWrite.has_value() ? qsizetype{1} : qsizetype{0});
    if (data.size() > MaxPendingWriteBytes ||
        m_pendingWriteBytes > MaxPendingWriteBytes - data.size() ||
        requestCount >= MaxPendingWriteRequests) {
        m_lastWriteError = QStringLiteral("Send queue is full (limit: %1 KiB / %2 requests). Increase the repeat interval.")
            .arg(MaxPendingWriteBytes / 1024)
            .arg(MaxPendingWriteRequests);
        return 0;
    }

    quint64 id = m_nextWriteId++;
    if (id == 0)
        id = m_nextWriteId++;
    m_writeQueue.enqueue(PendingWrite{id, data});
    m_pendingWriteBytes += data.size();
    m_lastWriteError.clear();

    // Defer pumping so the caller can register the id before error signals fire.
    if (!m_writePumpTimer.isActive())
        m_writePumpTimer.start(0);
    return id;
}

void SerialController::setSplitNewline(bool enabled)
{
    if (m_splitNewline == enabled)
        return;

    // Flush bytes accumulated under the previous display framing policy.
    queueFrames(m_pendingDisplayFrames, m_displayParser.flush());
    flushFrameSignals();
    m_splitNewline = enabled;
}

void SerialController::setAutoReplySplitNewline(bool enabled)
{
    if (m_autoReplySplitNewline == enabled)
        return;
    // Changing the framing policy starts a fresh receive frame. Bytes gathered
    // under the previous policy must not trigger a delayed reply afterwards.
    m_autoReplyParser.clear();
    m_pendingAutoReplyFrames.clear();
    m_autoReplySplitNewline = enabled;
}

void SerialController::onReadyRead()
{
    const QByteArray data = m_serial.readAll();
    if (data.isEmpty())
        return;

    const QDateTime now = QDateTime::currentDateTime();
    m_sinceReceive.restart();
    queueFrames(m_pendingDisplayFrames,
                m_displayParser.feed(data, now, m_splitNewline));
    // Automatic-reply framing is independent from the selected display mode.
    queueFrames(m_pendingAutoReplyFrames,
                m_autoReplyParser.feed(data, now, m_autoReplySplitNewline));

    if (!m_frameEmitTimer.isActive())
        m_frameEmitTimer.start();
}

void SerialController::onGapTimeout()
{
    if (!m_sinceReceive.isValid() || m_sinceReceive.elapsed() < m_frameGapMs)
        return;

    queueFrames(m_pendingDisplayFrames, m_displayParser.flush());
    queueFrames(m_pendingAutoReplyFrames, m_autoReplyParser.flush());
    m_sinceReceive.invalidate();
    flushFrameSignals();
}

void SerialController::onBytesWritten(qint64 bytes)
{
    if (m_closing || bytes <= 0 || !m_activeWrite)
        return;

    const quint64 requestId = m_activeWrite->id;
    const qint64 outstanding = static_cast<qint64>(
        m_activeWrite->submitted - m_activeWrite->confirmed);
    const qint64 confirmedNow = qMin(bytes, qMax<qint64>(0, outstanding));
    if (confirmedNow <= 0)
        return;

    m_activeWrite->confirmed += static_cast<qsizetype>(confirmedNow);
    m_inFlightBytes = qMax<qint64>(0, m_inFlightBytes - confirmedNow);
    if (m_inFlightBytes > 0) {
        const qint64 expectedMs = (m_inFlightBytes * 12 * 1000 + m_baudRate - 1)
                                  / m_baudRate;
        m_writeWatchdogTimer.start(static_cast<int>(qBound<qint64>(
            qint64{3000}, expectedMs * 2 + 2000, qint64{std::numeric_limits<int>::max()})));
    } else {
        m_writeWatchdogTimer.stop();
    }
    emit writeBytesConfirmed(requestId, confirmedNow, QDateTime::currentDateTime());

    // A direct signal handler is allowed to close the port. Revalidate before
    // touching the optional again.
    if (!m_activeWrite || m_activeWrite->id != requestId || m_closing)
        return;

    if (m_activeWrite->confirmed == m_activeWrite->data.size() &&
        m_activeWrite->submitted == m_activeWrite->data.size()) {
        m_pendingWriteBytes -= m_activeWrite->data.size();
        m_activeWrite.reset();
        m_inFlightBytes = 0;
        emit writeCompleted(requestId);
    }

    if (!m_writePumpTimer.isActive())
        m_writePumpTimer.start(0);
}

void SerialController::pumpWrites()
{
    if (m_closing || !m_serial.isOpen() || m_inFlightBytes > 0)
        return;

    if (!m_activeWrite) {
        if (m_writeQueue.isEmpty())
            return;
        m_activeWrite = m_writeQueue.dequeue();
    }

    const quint64 requestId = m_activeWrite->id;
    const qsizetype submitted = m_activeWrite->submitted;
    if (submitted >= m_activeWrite->data.size())
        return;

    const qsizetype length = qMin(WriteChunkSize, m_activeWrite->data.size() - submitted);
    const QByteArray chunk = m_activeWrite->data.mid(submitted, length);
    const qint64 accepted = m_serial.write(chunk);

    // errorOccurred can be delivered synchronously from write() and may close
    // the port, which clears m_activeWrite. Never retain a reference across it.
    if (!m_activeWrite || m_activeWrite->id != requestId ||
        m_closing || !m_serial.isOpen()) {
        return;
    }
    PendingWrite &request = *m_activeWrite;
    if (accepted < 0) {
        const QString message = QStringLiteral("Serial write failed: %1").arg(m_serial.errorString());
        emit errorOccurred(message);
        closeInternal(message);
        return;
    }
    if (accepted == 0) {
        ++request.zeroWriteRetries;
        if (request.zeroWriteRetries > 50) {
            const QString message = QStringLiteral("Send aborted: the serial port is not accepting data");
            emit errorOccurred(message);
            closeInternal(message);
            return;
        }
        m_writePumpTimer.start(10);
        return;
    }

    request.zeroWriteRetries = 0;
    request.submitted += static_cast<qsizetype>(accepted);
    m_inFlightBytes += accepted;
    const qint64 expectedMs = (accepted * 12 * 1000 + m_baudRate - 1) / m_baudRate;
    m_writeWatchdogTimer.start(static_cast<int>(qBound<qint64>(
        qint64{3000}, expectedMs * 2 + 2000, qint64{std::numeric_limits<int>::max()})));
}

void SerialController::onWriteWatchdogTimeout()
{
    if (m_closing || !m_serial.isOpen() || m_inFlightBytes <= 0)
        return;
    const QString message = QStringLiteral(
        "Send queue aborted: serial write timed out without progress");
    emit errorOccurred(message);
    closeInternal(message);
}

void SerialController::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || m_closing || m_opening)
        return;

    const QString message = QStringLiteral("Serial error: %1").arg(m_serial.errorString());
    switch (error) {
    case QSerialPort::ResourceError:
    case QSerialPort::DeviceNotFoundError:
    case QSerialPort::PermissionError:
    case QSerialPort::ReadError:
    case QSerialPort::WriteError:
    case QSerialPort::OpenError:
    case QSerialPort::NotOpenError:
    case QSerialPort::UnsupportedOperationError:
    case QSerialPort::UnknownError:
        emit errorOccurred(message);
        closeInternal(message);
        break;
    case QSerialPort::TimeoutError:
        // A timeout is unusual without blocking wait APIs but not always fatal.
        emit errorOccurred(message);
        break;
    case QSerialPort::NoError:
        break;
    default:
        // Keep forward compatibility with any non-fatal error values added by
        // a newer Qt version.
        emit errorOccurred(message);
        break;
    }
}

void SerialController::queueFrames(QVector<ParsedFrame> &destination,
                                   QVector<ParsedFrame> frames)
{
    if (frames.isEmpty())
        return;
    destination.reserve(destination.size() + frames.size());
    for (ParsedFrame &frame : frames)
        destination.push_back(std::move(frame));
}

void SerialController::flushFrameSignals()
{
    m_frameEmitTimer.stop();

    if (!m_pendingDisplayFrames.isEmpty()) {
        QVector<ParsedFrame> frames;
        frames.swap(m_pendingDisplayFrames);
        emit framesReceived(frames);
    }
    if (!m_pendingAutoReplyFrames.isEmpty()) {
        QVector<ParsedFrame> frames;
        frames.swap(m_pendingAutoReplyFrames);
        emit autoReplyFramesReceived(
            frames, m_autoReplySplitNewline ? QStringLiteral("ASCII")
                                            : QStringLiteral("HEX"));
    }
}

void SerialController::flushDisplayFrames()
{
    if (m_pendingDisplayFrames.isEmpty())
        return;
    QVector<ParsedFrame> frames;
    frames.swap(m_pendingDisplayFrames);
    emit framesReceived(frames);
}

void SerialController::failPendingWrites(const QString &reason)
{
    struct Failure
    {
        quint64 id;
        qint64 confirmed;
    };
    QVector<Failure> failures;
    failures.reserve(m_writeQueue.size() + (m_activeWrite ? 1 : 0));
    if (m_activeWrite) {
        failures.push_back({m_activeWrite->id,
                            static_cast<qint64>(m_activeWrite->confirmed)});
    }
    for (const PendingWrite &request : std::as_const(m_writeQueue))
        failures.push_back({request.id, 0});

    m_activeWrite.reset();
    m_writeQueue.clear();
    m_inFlightBytes = 0;
    m_writeWatchdogTimer.stop();
    m_pendingWriteBytes = 0;

    for (const Failure &failure : std::as_const(failures))
        emit writeFailed(failure.id, failure.confirmed, reason);
}

void SerialController::resetParsers()
{
    m_gapTimer.stop();
    m_frameEmitTimer.stop();
    m_writeWatchdogTimer.stop();
    m_sinceReceive.invalidate();
    m_displayParser.clear();
    m_autoReplyParser.clear();
    m_pendingDisplayFrames.clear();
    m_pendingAutoReplyFrames.clear();
}
