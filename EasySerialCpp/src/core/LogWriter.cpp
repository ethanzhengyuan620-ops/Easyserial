#include "core/LogWriter.h"

#include <QElapsedTimer>
#include <QFile>
#include <QMutexLocker>

LogWriter::LogWriter(QObject *parent)
    : QThread(parent)
{
}

LogWriter::~LogWriter()
{
    stopLogging();
}

quint64 LogWriter::startLogging(const QString &path)
{
    if (isRunning())
        return 0;
    quint64 sessionId = 0;
    {
        QMutexLocker locker(&m_mutex);
        ++m_sessionId;
        if (m_sessionId == 0)
            ++m_sessionId;
        sessionId = m_sessionId;
        m_path = path;
        m_queue.clear();
        m_queuedBytes = 0;
        m_stopRequested = false;
        m_accepting.store(true);
    }
    start();
    return sessionId;
}

void LogWriter::enqueue(const QByteArray &line)
{
    if (line.isEmpty() || !m_accepting.load())
        return;

    bool overflow = false;
    quint64 sessionId = 0;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_accepting.load())
            return;
        if (m_queue.size() >= MaxQueuedRecords
            || line.size() > MaxQueuedBytes
            || m_queuedBytes > MaxQueuedBytes - line.size()) {
            m_accepting.store(false);
            m_stopRequested = true;
            overflow = true;
            sessionId = m_sessionId;
        } else {
            m_queue.enqueue(line);
            m_queuedBytes += line.size();
        }
        m_hasData.wakeOne();
    }

    if (overflow) {
        emit writeError(sessionId, QStringLiteral(
            "Log recording stopped: incoming data exceeded the log buffer capacity."));
    }
}

void LogWriter::requestStop()
{
    QMutexLocker locker(&m_mutex);
    m_accepting.store(false);
    m_stopRequested = true;
    m_hasData.wakeOne();
}

void LogWriter::stopLogging()
{
    requestStop();

    // Destructor fallback: normal UI shutdown uses requestStop() and waits
    // asynchronously for finished(), so the GUI is not blocked by file I/O.
    // A QThread still must never be destroyed while run() is active.
    if (QThread::currentThread() != this)
        wait();

    QMutexLocker locker(&m_mutex);
    m_queue.clear();
    m_queuedBytes = 0;
}

void LogWriter::run()
{
    QString path;
    quint64 sessionId = 0;
    {
        QMutexLocker locker(&m_mutex);
        path = m_path;
        sessionId = m_sessionId;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        m_accepting.store(false);
        {
            QMutexLocker locker(&m_mutex);
            m_queue.clear();
            m_queuedBytes = 0;
        }
        emit openResult(sessionId, false, file.errorString());
        return;
    }
    {
        QMutexLocker locker(&m_mutex);
        if (m_stopRequested || !m_accepting.load()) {
            file.close();
            return;
        }
    }
    emit openResult(sessionId, true, path);

    const auto fail = [this, &file, sessionId](const QString &operation) {
        m_accepting.store(false);
        {
            QMutexLocker locker(&m_mutex);
            m_queue.clear();
            m_queuedBytes = 0;
        }
        QString detail = file.errorString();
        if (detail.isEmpty())
            detail = QStringLiteral("Unknown file error");
        emit writeError(sessionId,
                        QStringLiteral("Log %1 failed: %2").arg(operation, detail));
    };

    QElapsedTimer flushTimer;
    flushTimer.start();
    qsizetype unflushedBytes = 0;

    for (;;) {
        QQueue<QByteArray> batch;
        bool stopping = false;
        {
            QMutexLocker locker(&m_mutex);
            if (m_queue.isEmpty() && !m_stopRequested)
                m_hasData.wait(&m_mutex, 500);
            batch.swap(m_queue);
            m_queuedBytes = 0;
            stopping = m_stopRequested;
            if (batch.isEmpty() && stopping)
                break;
        }

        while (!batch.isEmpty()) {
            const QByteArray line = batch.dequeue();
            qsizetype offset = 0;
            while (offset < line.size()) {
                const qint64 written = file.write(
                    line.constData() + offset,
                    static_cast<qint64>(line.size() - offset));
                if (written <= 0) {
                    fail(QStringLiteral("write"));
                    return;
                }
                offset += static_cast<qsizetype>(written);
                unflushedBytes += static_cast<qsizetype>(written);
            }
        }

        constexpr qsizetype FlushThreshold = 1024 * 1024;
        if (unflushedBytes > 0
            && (stopping || unflushedBytes >= FlushThreshold
                || flushTimer.elapsed() >= 500)) {
            if (!file.flush()) {
                fail(QStringLiteral("flush"));
                return;
            }
            unflushedBytes = 0;
            flushTimer.restart();
        }
    }

    if (unflushedBytes > 0 && !file.flush()) {
        fail(QStringLiteral("flush"));
        return;
    }
    file.close();
    m_accepting.store(false);
}
