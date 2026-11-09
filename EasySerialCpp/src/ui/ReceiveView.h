#pragma once

#include <QTextEdit>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include <array>

#include "Settings.h"
#include "core/ColorEngine.h"
#include "model/ReceiveModel.h"

class QSplitter;
class ReceiveView final : public QWidget
{
    Q_OBJECT
public:
    explicit ReceiveView(Settings &settings, QWidget *parent = nullptr);
    void appendFrame(const QByteArray &data, const QDateTime &timestamp);
    void appendTransmit(const QByteArray &data, const QDateTime &timestamp);
    void appendNote(const QString &text, const QDateTime &timestamp = QDateTime::currentDateTime());
    void clearAll();
    void setMode(DisplayMode mode);
    DisplayMode mode() const { return m_model->mode(); }
    void setAutoScroll(bool enabled);
    void setShowTimestamps(bool enabled);
    void setTimestampInterval(int milliseconds);
    void setRules(const QJsonArray &rules);
    void applyAppearance();
    void setMonitorMode(bool enabled);
    void setMonitorFontSize(int size);
    void zoom(int delta);
    int fontSize() const { return m_fontSize; }
    bool findNext(const QString &pattern, bool regularExpression,
                  bool caseSensitive, bool backwards, bool wholeWord = false);
    void clearSearch();
    QTextEdit *textEdit() const { return m_text; }
    QTextEdit *asciiEdit() const { return m_ascii; }
    ReceiveModel *model() const { return m_model; }

signals:
    void fontSizeChanged(int size);
    void searchStatusChanged(int current, int count, const QString &error);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void flushPending();

private:
    struct FrameRange { int start; int end; int rightStart; int rightEnd; };
    QTextEdit *createEdit(const QString &name);
    void configureEditors();
    void applyTextFont();
    void applyBackground();
    void rebuildText();
    void renderFrames(int first, bool reset);
    void pruneFrames(int count, bool headerAdded);
    void insertHeader(QTextCursor &cursor, const ReceiveFrame &frame);
    void stopScrolling();
    void scrollTo(double target, bool followTail, bool immediate = false);
    void followLatest(bool immediate = false);
    void advanceScroll();
    void setScrollPosition(int position);
    void recalculateBytesPerLine();
    void showCopyMenu(QTextEdit *edit, const QPoint &position);
    void copyRawSelection(QTextEdit *edit);
    QVector<QPair<int, int>> searchMatches() const;
    void highlightSearch(const QVector<QPair<int, int>> &matches);

    Settings &m_settings;
    ReceiveModel *m_model = nullptr;
    ColorEngine m_colors;
    QTextEdit *m_text = nullptr;
    QTextEdit *m_ascii = nullptr;
    QSplitter *m_splitter = nullptr;
    QTimer m_flushTimer;
    QTimer m_layoutTimer;
    QTimer m_scrollTimer;
    QElapsedTimer m_scrollClock;
    double m_scrollPosition = 0;
    double m_scrollTarget = 0;
    double m_wheelRemainder = 0;
    bool m_scrollFollowsTail = true;
    bool m_rendering = false;
    bool m_monitorMode = false;
    QVector<ReceiveFrame> m_pending;
    QVector<FrameRange> m_ranges;
    bool m_autoScroll = true;
    bool m_showTimestamps = true;
    bool m_syncing = false;
    int m_timestampInterval = 100;
    int m_fontSize = 9;
    std::array<int, 3> m_paneSizes{9, 9, 9};
    DisplayMode m_renderedMode = DisplayMode::HexDual;
    QRegularExpression m_search;
    int m_searchPosition = -1;
};
