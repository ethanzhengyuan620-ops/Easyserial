#pragma once

#include <QByteArray>
#include <QMutex>
#include <QQueue>
#include <QString>
#include <QThread>
#include <QWaitCondition>
#include <atomic>

class LogWriter final : public QThread
{
    Q_OBJECT
public:
    explicit LogWriter(QObject *parent = nullptr);
    ~LogWriter() override;

    quint64 startLogging(const QString &path);
    void enqueue(const QByteArray &line);
    void requestStop();
    void stopLogging();
    bool isLogging() const { return m_accepting.load(); }

signals:
    void openResult(quint64 sessionId, bool success, const QString &message);
    void writeError(quint64 sessionId, const QString &message);

protected:
    void run() override;

private:
    static constexpr qsizetype MaxQueuedBytes = 16 * 1024 * 1024;
    static constexpr qsizetype MaxQueuedRecords = 65536;

    QString m_path;
    QQueue<QByteArray> m_queue;
    qsizetype m_queuedBytes = 0;
    QMutex m_mutex;
    QWaitCondition m_hasData;
    bool m_stopRequested = false;
    quint64 m_sessionId = 0;
    std::atomic_bool m_accepting{false};
};
