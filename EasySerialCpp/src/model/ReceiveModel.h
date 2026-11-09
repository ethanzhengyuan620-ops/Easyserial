#pragma once

#include <QAbstractTableModel>
#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QVector>

enum class DisplayMode { Ascii = 0, HexDual = 1, PureHex = 2 };
enum class FrameDirection { Receive, Transmit, Note };

struct ReceiveFrame
{
    QByteArray data;
    QString note;
    QDateTime timestamp;
    FrameDirection direction = FrameDirection::Receive;
    bool showTimestamp = true;
    mutable int chunkRevision = -1;
    mutable QVector<QPair<qsizetype, qsizetype>> chunks{};
    bool showHeader = true;
    bool continuesCrLf = false;
};

class ReceiveModel final : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Roles {
        RawTextRole = Qt::UserRole + 1,
        DirectionRole,
        FrameIndexRole,
        ChunkIndexRole,
        ChunkStartRole,
        ChunkLengthRole,
        HeaderRole,
        TimestampTextRole,
        TimestampColorRole
    };

    explicit ReceiveModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

    void appendFrames(QVector<ReceiveFrame> frames);
    void clear();
    void setMode(DisplayMode mode);
    DisplayMode mode() const { return m_mode; }
    void setBytesPerLine(int value);
    int bytesPerLine() const { return m_bytesPerLine; }
    void setAsciiCharactersPerLine(int value);
    void setAsciiLayout(const QFont &font, int width);
    int asciiCharactersPerLine() const { return m_asciiCharactersPerLine; }
    void setMaximumRows(int value);
    void setTimestampOptions(bool visible, int intervalMs);
    void setColors(const QColor &text, const QColor &timestamp,
                   const QColor &hex, const QColor &ascii);

    int frameIndexForRow(int row) const;
    int rowForFrameOffset(int frameIndex, qsizetype characterOffset) const;
    QString searchFrameText(int frameIndex) const;
    QString displayTextForRow(int row) const;
    QString rawHexForFrame(int frameIndex) const;
    qsizetype frameCount() const { return m_frames.size(); }
    const ReceiveFrame *frameAt(int index) const
    { return index >= 0 && index < m_frames.size() ? &m_frames.at(index) : nullptr; }
    // Continuous text for the native editor, including original line endings.
    // Legacy per-frame row/index helpers above are only a bounded-history index.
    QString streamAsciiForFrame(int index) const;

signals:
    void framesAppended(int first);
    void framesPruned(int count, bool headerAdded);

private:
    struct RowLocation { int frame = -1; int chunk = 0; };
    RowLocation locate(int row) const;
    int rowsFor(const ReceiveFrame &frame) const;
    void rebuildIndex();
    void trimToMaximumRows();
    void trimFirstFrameToRows(int targetRows);
    void updateTimestampFlags();
    void updateHeaderFlags(ReceiveFrame &frame, const ReceiveFrame *previous,
                           QDateTime &lastVisible) const;
    QString asciiText(const ReceiveFrame &frame, bool stripTags) const;
    QString asciiDisplayText(const ReceiveFrame &frame) const;
    const QVector<QPair<qsizetype, qsizetype>> &asciiChunks(const ReceiveFrame &frame) const;
    static QString stripColorTags(const QString &text);
    static QString directionText(FrameDirection direction);

    QVector<ReceiveFrame> m_frames;
    QVector<int> m_cumulativeRows;
    DisplayMode m_mode = DisplayMode::HexDual;
    int m_bytesPerLine = 16;
    int m_asciiCharactersPerLine = 80;
    QFont m_layoutFont;
    int m_layoutWidth = 0;
    int m_layoutRevision = 0;
    int m_maximumRows = 5000;
    bool m_showTimestamps = true;
    int m_timestampIntervalMs = 100;
    QColor m_textColor = QColor(QStringLiteral("#E2E8F0"));
    QColor m_timestampColor = QColor(QStringLiteral("#00C853"));
    QColor m_hexColor = QColor(QStringLiteral("#E0A86A"));
    QColor m_asciiColor = QColor(QStringLiteral("#4FC3F7"));
};
