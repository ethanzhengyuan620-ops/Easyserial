#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QVector>

struct ParsedFrame
{
    QByteArray data;
    QDateTime timestamp;
};

Q_DECLARE_METATYPE(ParsedFrame)

class FrameParser final
{
public:
    static constexpr qsizetype MaxFrame = 4096;

    QVector<ParsedFrame> feed(const QByteArray &data, const QDateTime &now, bool splitNewline);
    QVector<ParsedFrame> flush();
    void clear();
    bool isEmpty() const { return m_buffer.isEmpty(); }

private:
    QByteArray m_buffer;
    QDateTime m_firstByteTime;
};
