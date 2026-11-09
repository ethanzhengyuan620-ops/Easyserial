#include "core/FrameParser.h"

QVector<ParsedFrame> FrameParser::feed(const QByteArray &data,
                                       const QDateTime &now,
                                       bool splitNewline)
{
    QVector<ParsedFrame> result;
    result.reserve(qMax<qsizetype>(1, data.size() / MaxFrame));
    for (const char byte : data) {
        if (m_buffer.isEmpty())
            m_firstByteTime = now;
        m_buffer.append(byte);
        if ((splitNewline && byte == '\n') || m_buffer.size() >= MaxFrame) {
            result.push_back({m_buffer, m_firstByteTime});
            m_buffer.clear();
        }
    }
    return result;
}

QVector<ParsedFrame> FrameParser::flush()
{
    if (m_buffer.isEmpty())
        return {};
    QVector<ParsedFrame> result{{m_buffer, m_firstByteTime}};
    m_buffer.clear();
    return result;
}

void FrameParser::clear()
{
    m_buffer.clear();
    m_firstByteTime = {};
}

