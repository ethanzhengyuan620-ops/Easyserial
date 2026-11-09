#include "model/ReceiveModel.h"

#include <QRegularExpression>
#include <QTextLayout>
#include <algorithm>
#include <climits>
#include <utility>

#include "core/HexUtils.h"

ReceiveModel::ReceiveModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int ReceiveModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || m_cumulativeRows.isEmpty())
        return 0;
    return m_cumulativeRows.constLast();
}

int ReceiveModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_mode == DisplayMode::HexDual ? 4 : 3;
}

ReceiveModel::RowLocation ReceiveModel::locate(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};
    const auto it = std::lower_bound(m_cumulativeRows.cbegin(),
                                     m_cumulativeRows.cend(), row + 1);
    const int frame = static_cast<int>(std::distance(m_cumulativeRows.cbegin(), it));
    const int firstRow = frame == 0 ? 0 : m_cumulativeRows.at(frame - 1);
    return {frame, row - firstRow};
}

QVariant ReceiveModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const RowLocation location = locate(index.row());
    if (location.frame < 0)
        return {};
    const ReceiveFrame &frame = m_frames.at(location.frame);
    const bool header = frame.showHeader && location.chunk == 0;
    const int bodyChunk = location.chunk - int(frame.showHeader);
    if (role == HeaderRole) return header;
    if (role == TimestampTextRole)
        return header && frame.showTimestamp ? frame.timestamp.toString(QStringLiteral("[HH:mm:ss.zzz]")) : QString{};
    if (role == TimestampColorRole) return m_timestampColor;

    if (role == FrameIndexRole) return location.frame;
    if (role == ChunkIndexRole) return location.chunk;
    if (role == DirectionRole) return static_cast<int>(frame.direction);
    if (role == ChunkStartRole) {
        if (header) return qlonglong{-1};
        if (m_mode == DisplayMode::Ascii)
            return static_cast<qlonglong>(asciiChunks(frame).at(bodyChunk).first);
        return static_cast<qlonglong>(bodyChunk) * m_bytesPerLine * 3;
    }
    if (role == ChunkLengthRole) {
        if (header || m_mode != DisplayMode::Ascii)
            return 0;
        return static_cast<qlonglong>(asciiChunks(frame).at(bodyChunk).second);
    }
    if (role == Qt::TextAlignmentRole)
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);

    const int dataColumn = 2;
    if (role == RawTextRole && !header && m_mode == DisplayMode::Ascii &&
        index.column() == dataColumn && frame.direction == FrameDirection::Receive)
        return asciiText(frame, false);

    if (role == Qt::ForegroundRole) {
        if (header) return m_asciiColor;
        if (index.column() == 0) return m_timestampColor;
        if (index.column() == 1 || frame.direction == FrameDirection::Note ||
            (m_mode == DisplayMode::Ascii && frame.direction == FrameDirection::Transmit))
            return m_asciiColor;
        if (m_mode == DisplayMode::Ascii) return m_textColor;
        if (m_mode == DisplayMode::HexDual && index.column() == 3) return m_asciiColor;
        return m_hexColor;
    }

    if (role != Qt::DisplayRole)
        return {};

    // The first two legacy model columns are permanently hidden by the view.
    // A single metadata line precedes the full-width body; no per-line RX gutter.
    if (index.column() < 2) return QString{};
    if (header) {
        if (index.column() != dataColumn) return QString{};
        const QString timestamp = frame.showTimestamp
            ? frame.timestamp.toString(QStringLiteral("[HH:mm:ss.zzz]  ")) : QString{};
        return timestamp + directionText(frame.direction);
    }

    if (frame.direction == FrameDirection::Note) {
        if (index.column() != dataColumn)
            return QString{};
        if (m_mode != DisplayMode::Ascii)
            return frame.note;
    }

    if (m_mode == DisplayMode::Ascii) {
        const QString text = asciiDisplayText(frame);
        const auto chunk = asciiChunks(frame).at(bodyChunk);
        return text.sliced(chunk.first, chunk.second);
    }

    const qsizetype offset = static_cast<qsizetype>(bodyChunk) * m_bytesPerLine;
    const qsizetype length = qMin<qsizetype>(m_bytesPerLine, frame.data.size() - offset);
    if (m_mode == DisplayMode::PureHex || index.column() == 2)
        return HexUtils::spaced(frame.data, offset, length);

    QString ascii;
    ascii.reserve(length);
    for (qsizetype i = offset; i < offset + length; ++i) {
        const auto byte = static_cast<unsigned char>(frame.data.at(i));
        ascii.append(byte >= 0x20 && byte < 0x7F ? QChar(byte) : QChar(0x00B7));
    }
    return ascii;
}

QVariant ReceiveModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    if (section == 0) return QStringLiteral("Time");
    if (section == 1) return QStringLiteral("Direction");
    if (m_mode == DisplayMode::Ascii) return QStringLiteral("ASCII / Text");
    if (m_mode == DisplayMode::PureHex) return QStringLiteral("HEX");
    return section == 2 ? QStringLiteral("HEX") : QStringLiteral("ASCII");
}

int ReceiveModel::rowsFor(const ReceiveFrame &frame) const
{
    if (m_mode == DisplayMode::Ascii) {
        return int(frame.showHeader) + static_cast<int>(asciiChunks(frame).size());
    }
    if (frame.direction == FrameDirection::Note)
        return int(frame.showHeader) + 1;
    const qsizetype rows = (frame.data.size() + m_bytesPerLine - 1) /
                           m_bytesPerLine;
    return int(frame.showHeader) + qMax(1, static_cast<int>(qMin<qsizetype>(rows, INT_MAX - 1)));
}

void ReceiveModel::appendFrames(QVector<ReceiveFrame> frames)
{
    if (frames.isEmpty())
        return;

    QDateTime lastVisible;
    for (auto it = m_frames.crbegin(); it != m_frames.crend(); ++it) {
        if (it->showTimestamp) { lastVisible = it->timestamp; break; }
    }
    const ReceiveFrame *previous = m_frames.isEmpty() ? nullptr : &m_frames.constLast();
    qint64 addedRows64 = 0;
    for (ReceiveFrame &frame : frames) {
        updateHeaderFlags(frame, previous, lastVisible);
        addedRows64 += rowsFor(frame);
        previous = &frame;
    }

    // Normal tail eviction removes whole frames incrementally. Keep existing
    // group boundaries so pruning does not reformat every retained message.
    if (addedRows64 < m_maximumRows &&
        static_cast<qint64>(rowCount()) + addedRows64 > m_maximumRows) {
        const int target = qMax(0, m_maximumRows - m_maximumRows / 10 - int(addedRows64) - 1);
        int count = 0;
        int removedRows = 0;
        while (count < m_frames.size() && rowCount() - removedRows > target)
            removedRows = m_cumulativeRows.at(count++);
        // Keep the tail of a large multiline boundary packet instead of
        // dropping it whole. Exceptional oversized frames use the fallback.
        if (count > 0 && rowsFor(m_frames.at(count - 1)) <= qMax(16, m_maximumRows / 10)) {
            beginRemoveRows({}, 0, removedRows - 1);
            m_frames.erase(m_frames.begin(), m_frames.begin() + count);
            m_cumulativeRows.erase(m_cumulativeRows.begin(), m_cumulativeRows.begin() + count);
            for (int &row : m_cumulativeRows) row -= removedRows;
            endRemoveRows();
            const bool promote = !m_frames.isEmpty() && !m_frames.first().showHeader;
            if (promote) {
                beginInsertRows({}, 0, 0);
                m_frames.first().showHeader = true;
                m_frames.first().showTimestamp = m_showTimestamps;
                for (int &row : m_cumulativeRows) ++row;
                endInsertRows();
            }
            emit framesPruned(count, promote);
            if (m_frames.isEmpty() && !frames.first().showHeader) {
                frames.first().showHeader = true;
                frames.first().showTimestamp = m_showTimestamps;
                ++addedRows64;
            }
        }
    }
    if (addedRows64 > m_maximumRows ||
        static_cast<qint64>(rowCount()) + addedRows64 > m_maximumRows) {
        beginResetModel();
        m_frames.reserve(m_frames.size() + frames.size());
        for (ReceiveFrame &frame : frames)
            m_frames.push_back(std::move(frame));
        rebuildIndex();
        trimToMaximumRows();
        endResetModel();
        return;
    }

    const int addedRows = static_cast<int>(addedRows64);
    const int first = rowCount();
    const int firstFrame = static_cast<int>(m_frames.size());
    beginInsertRows({}, first, first + addedRows - 1);
    int cumulative = first;
    for (ReceiveFrame &frame : frames) {
        cumulative += rowsFor(frame);
        m_frames.push_back(std::move(frame));
        m_cumulativeRows.push_back(cumulative);
    }
    endInsertRows();
    emit framesAppended(firstFrame);
}

void ReceiveModel::clear()
{
    beginResetModel();
    m_frames.clear();
    m_cumulativeRows.clear();
    endResetModel();
}

void ReceiveModel::setMode(DisplayMode mode)
{
    if (m_mode == mode)
        return;
    beginResetModel();
    m_mode = mode;
    rebuildIndex();
    trimToMaximumRows();
    endResetModel();
}

void ReceiveModel::setBytesPerLine(int value)
{
    value = qBound(1, value, 1024);
    if (value == m_bytesPerLine)
        return;
    beginResetModel();
    m_bytesPerLine = value;
    rebuildIndex();
    trimToMaximumRows();
    endResetModel();
}

void ReceiveModel::setAsciiCharactersPerLine(int value)
{
    value = qBound(16, value, 4096);
    if (value == m_asciiCharactersPerLine)
        return;
    beginResetModel();
    m_asciiCharactersPerLine = value;
    m_layoutWidth = 0;
    ++m_layoutRevision;
    rebuildIndex();
    trimToMaximumRows();
    endResetModel();
}

void ReceiveModel::setMaximumRows(int value)
{
    value = qBound(200, value, 200000);
    if (value == m_maximumRows)
        return;
    beginResetModel();
    m_maximumRows = value;
    trimToMaximumRows();
    endResetModel();
}

void ReceiveModel::setAsciiLayout(const QFont &font, int width)
{
    width = qMax(1, width);
    if (m_layoutFont == font && m_layoutWidth == width) return;
    beginResetModel();
    m_layoutFont = font;
    m_layoutWidth = width;
    ++m_layoutRevision;
    rebuildIndex();
    trimToMaximumRows();
    endResetModel();
}

void ReceiveModel::setTimestampOptions(bool visible, int intervalMs)
{
    beginResetModel();
    m_showTimestamps = visible;
    m_timestampIntervalMs = qMax(0, intervalMs);
    rebuildIndex();
    trimToMaximumRows();
    endResetModel();
}

void ReceiveModel::setColors(const QColor &text, const QColor &timestamp,
                             const QColor &hex, const QColor &ascii)
{
    m_textColor = text;
    m_timestampColor = timestamp;
    m_hexColor = hex;
    m_asciiColor = ascii;
    if (rowCount() > 0)
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                         {Qt::ForegroundRole});
}

void ReceiveModel::rebuildIndex()
{
    updateTimestampFlags();
    m_cumulativeRows.clear();
    m_cumulativeRows.reserve(m_frames.size());
    int total = 0;
    for (const ReceiveFrame &frame : std::as_const(m_frames)) {
        total = static_cast<int>(qMin<qint64>(
            INT_MAX, static_cast<qint64>(total) + rowsFor(frame)));
        m_cumulativeRows.push_back(total);
    }
}

void ReceiveModel::trimToMaximumRows()
{
    if (m_frames.isEmpty() || rowCount() <= m_maximumRows)
        return;

    const int totalRows = rowCount();
    // Keep some headroom so a busy serial stream does not force a full model
    // reset on every small append after the limit is reached.
    const int targetRows = qMax(1, m_maximumRows - m_maximumRows / 10);
    qsizetype removeCount = 0;
    while (removeCount < m_frames.size() - 1) {
        const int removedRows = removeCount == 0
            ? 0 : m_cumulativeRows.at(removeCount - 1);
        if (totalRows - removedRows <= targetRows)
            break;
        ++removeCount;
    }
    if (removeCount <= 0) {
        trimFirstFrameToRows(targetRows);
        return;
    }

    const int suffixRows = totalRows - m_cumulativeRows.at(removeCount - 1);
    if (suffixRows < targetRows) {
        // Retain the newest part of the boundary frame as well as all newer
        // frames, so max_lines behaves like a true tail buffer.
        const int boundaryIndex = static_cast<int>(removeCount - 1);
        if (boundaryIndex > 0)
            m_frames.erase(m_frames.begin(), m_frames.begin() + boundaryIndex);
        rebuildIndex();
        trimFirstFrameToRows(targetRows - suffixRows);
    } else {
        m_frames.erase(m_frames.begin(), m_frames.begin() + removeCount);
        rebuildIndex();
        if (rowCount() > targetRows)
            trimFirstFrameToRows(targetRows);
    }
}

void ReceiveModel::trimFirstFrameToRows(int targetRows)
{
    if (m_frames.isEmpty() || targetRows <= 0 ||
        rowsFor(m_frames.first()) <= targetRows) {
        return;
    }

    ReceiveFrame &frame = m_frames.first();
    frame.chunkRevision = -1;
    if (m_mode == DisplayMode::Ascii) {
        const qsizetype maximumCharacters = static_cast<qsizetype>(targetRows) *
                                            m_asciiCharactersPerLine;
        if (frame.direction == FrameDirection::Note) {
            if (frame.note.size() > maximumCharacters)
                frame.note = frame.note.right(maximumCharacters);
        } else if (frame.data.size() > maximumCharacters) {
            // Keeping at most this many source bytes guarantees no more than
            // maximumCharacters decoded display characters, even for invalid
            // UTF-8. Older bytes are the first data discarded by max_lines.
            frame.data = frame.data.right(maximumCharacters);
            frame.continuesCrLf = false;
        }
    } else if (frame.direction != FrameDirection::Note) {
        const qsizetype maximumBytes = static_cast<qsizetype>(targetRows) *
                                       m_bytesPerLine;
        if (frame.data.size() > maximumBytes)
            frame.data = frame.data.right(maximumBytes);
    }
    // Embedded newlines and expanded tabs can use more rows than byte length
    // alone predicts. Always enforce the bound while retaining original bytes.
    int rows = rowsFor(frame);
    // Keep at least one body line in addition to any visible group header.
    if (targetRows < int(frame.showHeader) + 1) {
        m_frames.removeFirst();
        rebuildIndex();
        return;
    }
    while (rows > targetRows) {
        const qsizetype length = frame.direction == FrameDirection::Note ? frame.note.size() : frame.data.size();
        if (length == 0) break;
        const qsizetype remove = qMax<qsizetype>(1, length * (rows - targetRows) / rows);
        if (frame.direction == FrameDirection::Note) frame.note.remove(0, remove);
        else { frame.data.remove(0, remove); frame.continuesCrLf = false; }
        frame.chunkRevision = -1;
        rows = rowsFor(frame);
    }
    rebuildIndex();
}

void ReceiveModel::updateTimestampFlags()
{
    QDateTime lastVisible;
    const ReceiveFrame *previous = nullptr;
    for (ReceiveFrame &frame : m_frames) {
        updateHeaderFlags(frame, previous, lastVisible);
        previous = &frame;
    }
}

void ReceiveModel::updateHeaderFlags(ReceiveFrame &frame, const ReceiveFrame *previous,
                                      QDateTime &lastVisible) const
{
    // A timestamp and direction form ONE group header. Continuation frames
    // contribute body rows only, not an orphan RX/TX row (or a blank spacer).
    // Direction changes always begin a new group, so TX is never labeled RX.
    // An idle-gap/4096-byte chunk boundary is NOT a text line boundary. Defer
    // periodic headers until a real CR/LF boundary, even after a long pause.
    // Keep this context on the first retained frame if its preceding CR frame
    // was evicted, so an appearance/mode rebuild cannot resurrect the paired LF.
    if (previous) frame.continuesCrLf = previous->direction == frame.direction &&
        previous->data.endsWith('\r') && frame.data.startsWith('\n');
    const bool lineBoundary = previous &&
        (previous->data.endsWith('\r') || previous->data.endsWith('\n'));
    const bool onlyCrLfContinuation = frame.continuesCrLf && frame.data == "\n";
    frame.showHeader = m_mode != DisplayMode::Ascii || !previous ||
        previous->direction != frame.direction || frame.direction == FrameDirection::Note ||
        (lineBoundary && !onlyCrLfContinuation && m_showTimestamps && (!lastVisible.isValid() ||
            lastVisible.msecsTo(frame.timestamp) >= m_timestampIntervalMs));
    frame.showTimestamp = m_showTimestamps && frame.showHeader;
    if (frame.showTimestamp) lastVisible = frame.timestamp;
}

QString ReceiveModel::streamAsciiForFrame(int index) const
{
    const auto *frame = frameAt(index);
    if (!frame) return {};
    if (frame->direction == FrameDirection::Note) return frame->note;
    QString text = QString::fromUtf8(frame->data);
    if (frame->continuesCrLf && text.startsWith('\n')) text.remove(0, 1);
    // Preserve all real trailing/blank lines. CRLF (including a split pair)
    // represents one newline; standalone CR keeps the existing newline policy.
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QLatin1Char('\t'), QStringLiteral("    "));
    return text;
}

QString ReceiveModel::asciiText(const ReceiveFrame &frame, bool stripTags) const
{
    QString text = QString::fromUtf8(frame.data);
    // The row budget excludes just the final empty block, not genuine blank
    // lines. The native editor uses streamAsciiForFrame() without this trimming.
    if (text.endsWith("\r\n")) text.chop(2);
    else if (text.endsWith('\r') || text.endsWith('\n')) text.chop(1);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QLatin1Char('\t'), QStringLiteral("    "));
    return stripTags ? stripColorTags(text) : text;
}

QString ReceiveModel::asciiDisplayText(const ReceiveFrame &frame) const
{
    if (frame.direction == FrameDirection::Note)
        return frame.note;
    // Received inline color tags are formatting instructions.  Transmitted data
    // is an exact echo, so tag-looking text must remain visible literally.
    return asciiText(frame, frame.direction == FrameDirection::Receive);
}

QString ReceiveModel::stripColorTags(const QString &text)
{
    static const QRegularExpression regex(QStringLiteral(
        R"TAG(\[(?:red|green|yellow|blue|cyan|magenta|white|gray|orange|#[0-9A-Fa-f]{6}):"((?:\\.|[^"\\])*)"\])TAG"));
    QString output;
    qsizetype position = 0;
    auto matches = regex.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        output += text.sliced(position, match.capturedStart() - position);
        QString inner = match.captured(1);
        inner.replace(QStringLiteral("\\\""), QStringLiteral("\""));
        inner.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        output += inner;
        position = match.capturedEnd();
    }
    output += text.sliced(position);
    return output;
}

const QVector<QPair<qsizetype, qsizetype>> &ReceiveModel::asciiChunks(const ReceiveFrame &frame) const
{
    if (frame.chunkRevision == m_layoutRevision) return frame.chunks;
    const QString text = asciiDisplayText(frame);
    auto &chunks = frame.chunks;
    chunks.clear();
    frame.chunkRevision = m_layoutRevision;
    qsizetype start = 0;
    do {
        const qsizetype newline = text.indexOf(QLatin1Char('\n'), start);
        const qsizetype lineEnd = newline < 0 ? text.size() : newline;
        if (m_layoutWidth > 0 && lineEnd > start) {
            QTextLayout layout(text.sliced(start, lineEnd - start), m_layoutFont);
            QTextOption options;
            options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            layout.setTextOption(options);
            layout.beginLayout();
            while (true) {
                QTextLine line = layout.createLine();
                if (!line.isValid()) break;
                line.setLineWidth(m_layoutWidth);
                chunks.push_back({start + line.textStart(), line.textLength()});
            }
            layout.endLayout();
            if (lineEnd == text.size()) break;
            start = lineEnd + 1;
            continue;
        }
        qsizetype end = qMin(lineEnd, start + m_asciiCharactersPerLine);
        if (end < lineEnd && end > start && text.at(end - 1).isHighSurrogate()) --end;
        chunks.push_back({start, end - start});
        start = end == lineEnd && newline >= 0 ? end + 1 : end;
        if (end == text.size()) break;
    } while (start <= text.size());
    return chunks;
}

QString ReceiveModel::directionText(FrameDirection direction)
{
    switch (direction) {
    case FrameDirection::Receive: return QStringLiteral("RX");
    case FrameDirection::Transmit: return QStringLiteral("TX");
    case FrameDirection::Note: return QStringLiteral("NOTE");
    }
    return {};
}

int ReceiveModel::frameIndexForRow(int row) const
{
    const RowLocation location = locate(row);
    return location.frame;
}

int ReceiveModel::rowForFrameOffset(int frameIndex, qsizetype characterOffset) const
{
    if (frameIndex < 0 || frameIndex >= m_frames.size())
        return -1;
    const int firstRow = frameIndex == 0 ? 0 : m_cumulativeRows.at(frameIndex - 1);
    const ReceiveFrame &frame = m_frames.at(frameIndex);
    int chunk = 0;
    if (m_mode == DisplayMode::Ascii) {
        const auto chunks = asciiChunks(frame);
        for (int i = 0; i < chunks.size(); ++i) {
            if (chunks[i].first > characterOffset) break;
            chunk = i;
        }
    } else if (frame.direction != FrameDirection::Note) {
        // A spaced HEX string uses three characters per complete byte.
        const qsizetype byteOffset = qMax<qsizetype>(0, characterOffset) / 3;
        chunk = static_cast<int>(byteOffset / m_bytesPerLine);
    }
    const int headerRows = int(frame.showHeader);
    chunk = qBound(0, chunk, rowsFor(frame) - headerRows - 1);
    return firstRow + headerRows + chunk;
}

QString ReceiveModel::searchFrameText(int frameIndex) const
{
    if (frameIndex < 0 || frameIndex >= m_frames.size())
        return {};
    const ReceiveFrame &frame = m_frames.at(frameIndex);
    if (frame.direction == FrameDirection::Note)
        return frame.note;
    if (m_mode == DisplayMode::Ascii)
        return asciiDisplayText(frame);
    return HexUtils::spaced(frame.data);
}

QString ReceiveModel::displayTextForRow(int row) const
{
    const RowLocation location = locate(row);
    if (location.frame < 0)
        return {};
    const QString primary = data(index(row, 2), Qt::DisplayRole).toString();
    if (m_mode != DisplayMode::HexDual)
        return primary;
    const QString ascii = data(index(row, 3), Qt::DisplayRole).toString();
    return ascii.isEmpty() ? primary
                           : primary + QStringLiteral("    ") + ascii;
}

QString ReceiveModel::rawHexForFrame(int frameIndex) const
{
    if (frameIndex < 0 || frameIndex >= m_frames.size())
        return {};
    const ReceiveFrame &frame = m_frames.at(frameIndex);
    if (frame.direction == FrameDirection::Note)
        return HexUtils::spaced(frame.note.toUtf8());
    return HexUtils::spaced(frame.data);
}
