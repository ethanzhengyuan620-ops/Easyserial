#include "ui/ReceiveView.h"

#include <QApplication>
#include <QClipboard>
#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMenu>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "core/HexUtils.h"

namespace {
QTextCharFormat colorFormat(const QColor &color)
{
    QTextCharFormat format;
    // Colors only: changing the document font also resizes existing text.
    format.setForeground(color);
    return format;
}
void newLine(QTextCursor &cursor)
{
    cursor.insertBlock(QTextBlockFormat{}, QTextCharFormat{});
}
QString wrapNote(const QString &text, const QFont &font, int width)
{
    QStringList lines;
    for (const auto &paragraph : text.split('\n')) {
        if (paragraph.isEmpty()) { lines.append(QString{}); continue; }
        QTextLayout layout(paragraph, font);
        QTextOption options;
        options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(options);
        layout.beginLayout();
        while (true) {
            auto line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(qMax(1, width));
            lines.append(paragraph.sliced(line.textStart(), line.textLength()));
        }
        layout.endLayout();
    }
    return lines.join('\n');
}
}

ReceiveView::ReceiveView(Settings &settings, QWidget *parent)
    : QWidget(parent), m_settings(settings)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(2);
    m_text = createEdit(QStringLiteral("receiveText"));
    m_ascii = createEdit(QStringLiteral("receiveAscii"));
    m_splitter->addWidget(m_text);
    m_splitter->addWidget(m_ascii);
    m_splitter->setCollapsible(0, false);
    m_splitter->setCollapsible(1, false);
    m_splitter->setSizes({700, 300});
    layout->addWidget(m_splitter);

    m_model = new ReceiveModel(this);
    m_autoScroll = settings.boolValue("autoscroll", true);
    m_showTimestamps = settings.boolValue("show_ts", true);
    m_timestampInterval = settings.intValue("ts_interval", 100);
    m_model->setMaximumRows(settings.intValue("max_lines", 5000));
    // Soft wrapping belongs to QTextEdit, not virtual table rows.
    m_model->setAsciiCharactersPerLine(4096);
    m_model->setMode(static_cast<DisplayMode>(qBound(0, settings.intValue("mode", 1), 2)));
    m_model->setTimestampOptions(m_showTimestamps, m_timestampInterval);
    m_colors.setRules(settings.arrayValue("rules"));
    m_renderedMode = mode();
    connect(m_model, &QAbstractItemModel::modelReset, this, &ReceiveView::rebuildText);
    connect(m_model, &ReceiveModel::framesAppended, this,
        [this](int first) { renderFrames(first, false); });
    connect(m_model, &ReceiveModel::framesPruned, this, &ReceiveView::pruneFrames);
    m_scrollTimer.setTimerType(Qt::PreciseTimer);
    m_scrollTimer.setInterval(16);
    connect(&m_scrollTimer, &QTimer::timeout, this, &ReceiveView::advanceScroll);
    m_layoutTimer.setSingleShot(true);
    m_layoutTimer.setInterval(80);
    connect(&m_layoutTimer, &QTimer::timeout, this, &ReceiveView::recalculateBytesPerLine);
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] { m_layoutTimer.start(); });
    for (auto *edit : {m_text, m_ascii}) {
        connect(edit, &QTextEdit::selectionChanged, this, [this, edit] {
            if (!m_rendering && edit->textCursor().hasSelection()) stopScrolling();
        });
        connect(edit->verticalScrollBar(), &QScrollBar::sliderPressed, this, &ReceiveView::stopScrolling);
        connect(edit->verticalScrollBar(), &QScrollBar::actionTriggered, this, [this](int) {
            if (!m_rendering && !m_syncing) stopScrolling();
        });
        connect(edit->verticalScrollBar(), &QScrollBar::valueChanged, this, [this, edit](int value) {
            if (mode() != DisplayMode::HexDual || m_syncing) return;
            m_syncing = true;
            (edit == m_text ? m_ascii : m_text)->verticalScrollBar()->setValue(value);
            m_syncing = false;
        });
    }
    configureEditors();
    applyAppearance();
    m_flushTimer.setInterval(30);
    connect(&m_flushTimer, &QTimer::timeout, this, &ReceiveView::flushPending);
}

QTextEdit *ReceiveView::createEdit(const QString &name)
{
    // Native pixel scrolling with real text selection. Device text is inserted
    // as plain characters and color formats, never interpreted as HTML.
    auto *edit = new QTextEdit(this);
    edit->setObjectName(name);
    edit->setReadOnly(true);
    edit->setAcceptRichText(false);
    edit->setUndoRedoEnabled(false);
    edit->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    edit->setFrameShape(QFrame::NoFrame);
    edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    edit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    edit->document()->setDocumentMargin(6);
    edit->document()->setMaximumBlockCount(qBound(200, m_settings.intValue("max_lines", 5000), 200000));
    edit->viewport()->installEventFilter(this);
    edit->installEventFilter(this);
    edit->verticalScrollBar()->installEventFilter(this);
    edit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(edit, &QWidget::customContextMenuRequested, this,
        [this, edit](const QPoint &point) { showCopyMenu(edit, point); });
    return edit;
}

void ReceiveView::appendFrame(const QByteArray &data, const QDateTime &timestamp)
{
    m_pending.push_back({data, {}, timestamp, FrameDirection::Receive, true});
    if (!m_flushTimer.isActive()) m_flushTimer.start();
}
void ReceiveView::appendTransmit(const QByteArray &data, const QDateTime &timestamp)
{
    m_pending.push_back({data, {}, timestamp, FrameDirection::Transmit, true});
    if (!m_flushTimer.isActive()) m_flushTimer.start();
}
void ReceiveView::appendNote(const QString &text, const QDateTime &timestamp)
{
    m_pending.push_back({{}, text, timestamp, FrameDirection::Note, true});
    if (!m_flushTimer.isActive()) m_flushTimer.start();
}
void ReceiveView::flushPending()
{
    if (m_pending.isEmpty()) { m_flushTimer.stop(); return; }
    QVector<ReceiveFrame> frames;
    frames.swap(m_pending);
    m_model->appendFrames(std::move(frames));
}
void ReceiveView::clearAll()
{
    stopScrolling();
    m_pending.clear();
    clearSearch();
    m_model->clear();
}
void ReceiveView::setMode(DisplayMode next)
{
    stopScrolling();
    flushPending();
    clearSearch();
    m_model->setMode(next);
    if (!m_monitorMode) m_fontSize = m_paneSizes[static_cast<int>(next)];
    configureEditors();
    applyTextFont();
    m_layoutTimer.start();
}
void ReceiveView::setAutoScroll(bool enabled)
{
    m_autoScroll = enabled;
    stopScrolling();
    if (enabled) followLatest();
}
void ReceiveView::stopScrolling()
{
    m_scrollTimer.stop();
    m_wheelRemainder = 0;
}
void ReceiveView::setScrollPosition(int position)
{
    m_syncing = true;
    m_text->verticalScrollBar()->setValue(position);
    if (mode() == DisplayMode::HexDual) m_ascii->verticalScrollBar()->setValue(position);
    m_syncing = false;
}
void ReceiveView::followLatest(bool immediate)
{
    // Auto scroll is the only follow/pause switch. Manual navigation, selection
    // and search never disable it: the next incoming batch follows the tail.
    if (!m_autoScroll) return;
    m_wheelRemainder = 0;
    scrollTo(m_text->verticalScrollBar()->maximum(), true, immediate);
}
void ReceiveView::scrollTo(double target, bool followTail, bool immediate)
{
    auto *bar = m_text->verticalScrollBar();
    m_scrollTarget = qBound(0.0, target, double(bar->maximum()));
    m_scrollFollowsTail = followTail;
    const double gap = std::abs(m_scrollTarget - bar->value());
    // Never queue animations: large bursts catch up immediately. Small updates
    // share one moving target and settle in about 150 ms, even on a live stream.
    if (immediate || (followTail && gap > 2 * m_text->viewport()->height()) || gap <= 1) {
        m_scrollTimer.stop();
        setScrollPosition(qRound(m_scrollTarget));
        m_scrollPosition = bar->value();
        return;
    }
    if (!m_scrollTimer.isActive()) {
        m_scrollPosition = bar->value();
        m_scrollClock.start();
        m_scrollTimer.start();
    }
}
void ReceiveView::advanceScroll()
{
    if (m_scrollFollowsTail && !m_autoScroll) { stopScrolling(); return; }
    auto *bar = m_text->verticalScrollBar();
    const double target = m_scrollFollowsTail ? bar->maximum()
        : qBound(0.0, m_scrollTarget, double(bar->maximum()));
    const double elapsed = qBound(qint64{1}, m_scrollClock.restart(), qint64{250});
    if (m_scrollFollowsTail && target - m_scrollPosition > 2 * m_text->viewport()->height()) {
        followLatest(true);
        return;
    }
    const double settlingTime = m_scrollFollowsTail ? 40.0 : 50.0;
    m_scrollPosition += (target - m_scrollPosition) * (1.0 - std::exp(-elapsed / settlingTime));
    if (std::abs(target - m_scrollPosition) < 1.0) {
        m_scrollPosition = target;
        m_scrollTimer.stop();
    }
    setScrollPosition(qRound(m_scrollPosition));
}
void ReceiveView::configureEditors()
{
    const bool dual = mode() == DisplayMode::HexDual;
    m_ascii->setVisible(dual);
    m_text->setLineWrapMode(dual ? QTextEdit::NoWrap : QTextEdit::WidgetWidth);
    m_ascii->setLineWrapMode(QTextEdit::NoWrap);
}
void ReceiveView::setShowTimestamps(bool enabled)
{
    m_showTimestamps = enabled;
    m_model->setTimestampOptions(enabled, m_timestampInterval);
}
void ReceiveView::setTimestampInterval(int milliseconds)
{
    m_timestampInterval = milliseconds;
    m_model->setTimestampOptions(m_showTimestamps, milliseconds);
}
void ReceiveView::setRules(const QJsonArray &rules)
{
    m_colors.setRules(rules);
    rebuildText();
}
void ReceiveView::applyAppearance()
{
    const int base = qBound(6, m_settings.intValue("font_size", 9), 36);
    const auto sizes = m_settings.arrayValue("receive_font_sizes");
    for (int i = 0; i < 3; ++i)
        m_paneSizes[i] = sizes.size() == 3 ? qBound(6, sizes[i].toInt(base), 36) : base;
    m_fontSize = m_monitorMode ? qBound(6, m_settings.intValue("monitor_font_size", m_fontSize), 36)
        : m_paneSizes[static_cast<int>(mode())];
    applyBackground();
    applyTextFont();
    rebuildText();
}
void ReceiveView::applyBackground()
{
    const QString background = m_monitorMode ? QStringLiteral("transparent")
        : m_settings.stringValue("col_bg", "#FFFFFF");
    const QString foreground = m_settings.stringValue("col_text", "#1E293B");
    setStyleSheet(m_monitorMode ? QStringLiteral("ReceiveView {background:transparent;}") : QString{});
    for (auto *edit : {m_text, m_ascii})
        edit->setStyleSheet(QStringLiteral("QTextEdit{background:%1;color:%2;border:0;padding:0;}")
            .arg(background, foreground));
}
void ReceiveView::setMonitorMode(bool enabled)
{
    if (m_monitorMode == enabled) return;
    flushPending();
    m_monitorMode = enabled;
    if (enabled) {
        const int saved = m_settings.intValue("monitor_font_size", 0);
        if (saved > 0) m_fontSize = qBound(6, saved, 36);
        m_settings.setValue("monitor_font_size", m_fontSize);
    } else {
        m_fontSize = m_paneSizes[static_cast<int>(mode())];
    }
    applyBackground();
    applyTextFont();
}
void ReceiveView::setMonitorFontSize(int size)
{
    if (!m_monitorMode) return;
    m_fontSize = qBound(6, size, 36);
    m_settings.setValue("monitor_font_size", m_fontSize);
    applyTextFont();
}
void ReceiveView::applyTextFont()
{
    stopScrolling();
    QFont font(m_settings.stringValue("font_family", "Consolas"), m_fontSize);
    font.setFamilies({m_settings.stringValue("font_family", "Consolas"), QStringLiteral("Microsoft YaHei UI")});
    font.setStyleHint(QFont::Monospace);
    for (auto *edit : {m_text, m_ascii}) {
        edit->setFont(font);
        edit->document()->setDefaultFont(font);
    }
    emit fontSizeChanged(m_fontSize);
    m_layoutTimer.start();
}
void ReceiveView::zoom(int delta)
{
    if (m_monitorMode) { setMonitorFontSize(m_fontSize + delta); return; }
    m_fontSize = qBound(6, m_fontSize + delta, 36);
    m_paneSizes[static_cast<int>(mode())] = m_fontSize;
    m_settings.setArray("receive_font_sizes", {m_paneSizes[0], m_paneSizes[1], m_paneSizes[2]});
    applyTextFont();
}
void ReceiveView::rebuildText()
{
    renderFrames(0, true);
}
void ReceiveView::insertHeader(QTextCursor &cursor, const ReceiveFrame &frame)
{
    if (frame.showTimestamp)
        cursor.insertText(frame.timestamp.toString("[HH:mm:ss.zzz]  "),
            colorFormat(QColor(m_settings.stringValue("col_ts", "#15803D"))));
    cursor.insertText(frame.direction == FrameDirection::Receive ? "RX" :
        frame.direction == FrameDirection::Transmit ? "TX" : "NOTE",
        colorFormat(QColor(m_settings.stringValue("col_ascii", "#0369A1"))));
    newLine(cursor);
}
void ReceiveView::pruneFrames(int count, bool headerAdded)
{
    if (count <= 0 || m_ranges.isEmpty()) return;
    m_rendering = true;
    m_syncing = true;
    const int scroll = m_text->verticalScrollBar()->value();
    auto visible = m_text->cursorForPosition(QPoint(0, 0));
    const int oldY = m_text->cursorRect(visible).top();
    const int cut = count < m_ranges.size() ? m_ranges[count].start : m_text->document()->characterCount() - 1;
    const int rightCut = count < m_ranges.size() ? m_ranges[count].rightStart : m_ascii->document()->characterCount() - 1;
    const auto removePrefix = [](QTextEdit *edit, int length) {
        QTextCursor cursor(edit->document());
        cursor.setPosition(length, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    };
    removePrefix(m_text, cut);
    removePrefix(m_ascii, rightCut);
    m_ranges.remove(0, qMin(count, static_cast<int>(m_ranges.size())));
    for (auto &range : m_ranges) {
        range.start = qMax(0, range.start - cut);
        range.end = qMax(0, range.end - cut);
        range.rightStart = qMax(0, range.rightStart - rightCut);
        range.rightEnd = qMax(0, range.rightEnd - rightCut);
    }
    if (headerAdded && !m_ranges.isEmpty()) {
        QTextCursor cursor(m_text->document());
        insertHeader(cursor, *m_model->frameAt(0));
        const int length = cursor.position();
        for (int i = 0; i < m_ranges.size(); ++i) {
            if (i > 0) m_ranges[i].start += length;
            m_ranges[i].end += length;
        }
    }
    if (m_searchPosition >= 0) m_searchPosition = qMax(-1, m_searchPosition - cut);
    m_syncing = false;
    setScrollPosition(m_text->verticalScrollBar()->value() + m_text->cursorRect(visible).top() - oldY);
    if (m_scrollTimer.isActive() && !m_scrollFollowsTail)
        m_scrollTarget += m_text->verticalScrollBar()->value() - scroll;
    m_scrollPosition = m_text->verticalScrollBar()->value();
    m_rendering = false;
}

void ReceiveView::renderFrames(int first, bool reset)
{
    m_rendering = true;
    if (reset) stopScrolling();
    const bool dual = mode() == DisplayMode::HexDual;
    const bool preserve = mode() == m_renderedMode;
    m_renderedMode = mode();
    // Capture numeric positions before clearing: QTextCursors track document edits.
    const auto selection = m_text->textCursor();
    const auto rightSelection = m_ascii->textCursor();
    const int anchor = selection.anchor(), position = selection.position();
    const int rightAnchor = rightSelection.anchor(), rightPosition = rightSelection.position();
    const int scroll = m_text->verticalScrollBar()->value();
    auto visible = m_text->cursorForPosition(QPoint(0, 0));
    const int oldY = m_text->cursorRect(visible).top();
    const bool selected = selection.hasSelection() || rightSelection.hasSelection();
    m_syncing = true;
    if (reset) { m_text->clear(); m_ascii->clear(); m_ranges.clear(); }
    QTextCursor left(m_text->document()), right(m_ascii->document());
    left.movePosition(QTextCursor::End);
    right.movePosition(QTextCursor::End);
    left.beginEditBlock();
    right.beginEditBlock();
    const QColor text(m_settings.stringValue("col_text", "#1E293B"));
    const QColor ascii(m_settings.stringValue("col_ascii", "#0369A1"));
    const QColor hex(m_settings.stringValue("col_hex", "#9A3412"));
    for (int i = first; i < m_model->frameCount(); ++i) {
        const auto &frame = *m_model->frameAt(i);
        if (i > 0 && mode() != DisplayMode::Ascii) {
            newLine(left);
            if (dual) newLine(right);
        } else if (frame.showHeader && left.positionInBlock() != 0) {
            // A direction/NOTE change needs its own metadata line. Ordinary
            // ASCII receive chunks append directly, with no synthetic newline.
            newLine(left);
        }
        FrameRange range{left.position(), 0, right.position(), 0};
        if (frame.showHeader) {
            insertHeader(left, frame);
            if (dual) newLine(right);
        }
        if (mode() == DisplayMode::Ascii && frame.direction == FrameDirection::Receive) {
            for (const auto &segment : m_colors.split(m_model->streamAsciiForFrame(i), text))
                left.insertText(segment.text, colorFormat(segment.color));
        } else if (frame.direction == FrameDirection::Note || mode() == DisplayMode::Ascii) {
            QString body = mode() == DisplayMode::Ascii ? m_model->streamAsciiForFrame(i) : m_model->searchFrameText(i);
            if (dual) body = wrapNote(body, m_text->font(), m_text->viewport()->width() - 16);
            left.insertText(body, colorFormat(ascii));
            if (dual) right.insertText(QString(body.count('\n'), '\n'));
        } else if (!dual) {
            left.insertText(HexUtils::spaced(frame.data), colorFormat(hex));
        } else {
            const int bytes = m_model->bytesPerLine();
            for (qsizetype offset = 0; offset < frame.data.size(); offset += bytes) {
                if (offset > 0) { newLine(left); newLine(right); }
                const qsizetype length = qMin<qsizetype>(bytes, frame.data.size() - offset);
                left.insertText(HexUtils::spaced(frame.data, offset, length), colorFormat(hex));
                QString characters;
                for (qsizetype j = offset; j < offset + length; ++j) {
                    const auto byte = static_cast<unsigned char>(frame.data[j]);
                    characters += byte >= 0x20 && byte < 0x7f ? QChar(byte) : QChar(0x00b7);
                }
                right.insertText(characters, colorFormat(ascii));
            }
        }
        range.end = left.position();
        range.rightEnd = right.position();
        m_ranges.append(range);
    }
    left.endEditBlock();
    right.endEditBlock();
    // Native block culling can remove a prefix (notably wrapped NOTE lines in
    // dual mode). Keep raw-frame copy ranges aligned with the surviving text.
    const int removed = m_ranges.isEmpty() ? 0 : qMax(0,
        m_ranges.constLast().end - (m_text->document()->characterCount() - 1));
    const int removedRight = m_ranges.isEmpty() ? 0 : qMax(0,
        m_ranges.constLast().rightEnd - (m_ascii->document()->characterCount() - 1));
    for (auto &range : m_ranges) {
        range.start = qMax(0, range.start - removed);
        range.end = qMax(0, range.end - removed);
        range.rightStart = qMax(0, range.rightStart - removedRight);
        range.rightEnd = qMax(0, range.rightEnd - removedRight);
    }
    if ((reset && preserve) || (!reset && selected)) {
        const auto restore = [](QTextEdit *edit, int from, int to) {
            QTextCursor cursor(edit->document());
            const int end = edit->document()->characterCount() - 1;
            cursor.setPosition(qBound(0, from, end));
            cursor.setPosition(qBound(0, to, end), QTextCursor::KeepAnchor);
            edit->setTextCursor(cursor);
        };
        restore(m_text, anchor - removed, position - removed);
        restore(m_ascii, rightAnchor - removedRight, rightPosition - removedRight);
    }
    m_syncing = false;
    // Preserve a character anchor rather than a scrollbar number when old
    // document blocks were culled. Reading/selection must not drift on append.
    const int restored = reset ? scroll : m_text->verticalScrollBar()->value() + m_text->cursorRect(visible).top() - oldY;
    setScrollPosition(restored);
    if (m_scrollTimer.isActive() && !m_scrollFollowsTail)
        m_scrollTarget += m_text->verticalScrollBar()->value() - scroll;
    m_scrollPosition = m_text->verticalScrollBar()->value();
    m_rendering = false;
    followLatest(reset);
    if (!m_search.pattern().isEmpty()) highlightSearch(searchMatches());
}

QVector<QPair<int, int>> ReceiveView::searchMatches() const
{
    QVector<QPair<int, int>> matches;
    if (m_search.pattern().isEmpty()) return matches;
    const QString text = m_text->toPlainText();
    auto iterator = m_search.globalMatch(text);
    while (iterator.hasNext() && matches.size() < 5000) {
        const auto match = iterator.next();
        if (match.capturedLength() > 0)
            matches.append({static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength())});
    }
    return matches;
}
void ReceiveView::highlightSearch(const QVector<QPair<int, int>> &matches)
{
    QList<QTextEdit::ExtraSelection> highlights;
    for (const auto &[start, length] : matches) {
        QTextEdit::ExtraSelection highlight;
        highlight.cursor = QTextCursor(m_text->document());
        highlight.cursor.setPosition(start);
        highlight.cursor.setPosition(start + length, QTextCursor::KeepAnchor);
        highlight.format.setBackground(QColor(start == m_searchPosition ? "#FBBF24" : "#FEF3C7"));
        highlights.append(highlight);
    }
    m_text->setExtraSelections(highlights);
}
bool ReceiveView::findNext(const QString &pattern, bool regularExpression,
                           bool caseSensitive, bool backwards, bool wholeWord)
{
    flushPending();
    if (pattern.isEmpty()) { clearSearch(); return false; }
    QString expression = regularExpression ? pattern : QRegularExpression::escape(pattern);
    if (wholeWord) expression = QStringLiteral("\\b(?:%1)\\b").arg(expression);
    auto options = QRegularExpression::PatternOptions(QRegularExpression::UseUnicodePropertiesOption);
    if (!caseSensitive) options |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression regex(expression, options);
    if (!regex.isValid()) {
        clearSearch();
        emit searchStatusChanged(0, 0, QStringLiteral("Invalid regex"));
        return false;
    }
    if (regex != m_search) m_searchPosition = -1;
    m_search = regex;
    const auto matches = searchMatches();
    int previous = -1;
    for (int i = 0; i < matches.size(); ++i)
        if (matches[i].first == m_searchPosition) previous = i;
    const int count = static_cast<int>(matches.size());
    if (!count) {
        m_searchPosition = -1;
        highlightSearch(matches);
        emit searchStatusChanged(0, 0, {});
        return false;
    }
    const int chosen = previous < 0 ? (backwards ? count - 1 : 0)
        : (previous + (backwards ? count - 1 : 1)) % count;
    m_searchPosition = matches[chosen].first;
    stopScrolling();
    highlightSearch(matches);
    QTextCursor cursor(m_text->document());
    cursor.setPosition(m_searchPosition);
    m_text->setTextCursor(cursor);
    m_text->ensureCursorVisible();
    emit searchStatusChanged(chosen + 1, count, {});
    return true;
}
void ReceiveView::clearSearch()
{
    m_search = QRegularExpression{};
    m_searchPosition = -1;
    m_text->setExtraSelections({});
    emit searchStatusChanged(0, 0, {});
}
void ReceiveView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    stopScrolling();
    m_layoutTimer.start();
}
bool ReceiveView::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)
        stopScrolling();
    if (event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down || key->key() == Qt::Key_PageUp ||
            key->key() == Qt::Key_PageDown || key->key() == Qt::Key_Home || key->key() == Qt::Key_End ||
            key->matches(QKeySequence::Find) || key->matches(QKeySequence::SelectAll)) stopScrolling();
    }
    QTextEdit *edit = nullptr;
    for (auto *candidate : {m_text, m_ascii})
        if (candidate && (watched == candidate || watched == candidate->viewport() ||
            watched == candidate->verticalScrollBar())) edit = candidate;
    if (event->type() == QEvent::Wheel && edit) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        const int delta = wheel->angleDelta().y();
        const int pixels = wheel->pixelDelta().y();
        if (wheel->modifiers().testFlag(Qt::ControlModifier)) {
            const int amount = delta != 0 ? delta : pixels;
            if (amount != 0) zoom(amount > 0 ? 1 : -1);
            wheel->accept();
            return true;
        }
        if (delta != 0 || pixels != 0) {
            auto *bar = m_text->verticalScrollBar();
            // Respect OS wheel speed; pixel deltas already include the device's
            // natural-scroll direction. Retain subpixel high-resolution input.
            const double step = qMin(double(bar->pageStep()),
                double(qMax(0, QApplication::wheelScrollLines())) * QFontMetrics(edit->font()).lineSpacing());
            const double distance = pixels != 0 ? -double(pixels) : -delta * step / 120.0;
            m_wheelRemainder += distance;
            const double wholePixels = std::trunc(m_wheelRemainder);
            m_wheelRemainder -= wholePixels;
            if (wholePixels != 0) {
                const double origin = m_scrollTimer.isActive() && !m_scrollFollowsTail
                    ? m_scrollTarget : bar->value();
                scrollTo(origin + wholePixels, false);
            }
            wheel->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
void ReceiveView::recalculateBytesPerLine()
{
    if (mode() != DisplayMode::HexDual) { followLatest(); return; }
    const QFontMetricsF metrics(m_text->font());
    const qreal byteWidth = qMax<qreal>(1, metrics.horizontalAdvance(QStringLiteral("FF ")));
    const qreal charWidth = qMax<qreal>(1, metrics.horizontalAdvance(QLatin1Char('W')));
    int bytes = qMin(qMax(1, static_cast<int>((m_text->viewport()->width() - 16) / byteWidth)),
                     qMax(1, static_cast<int>((m_ascii->viewport()->width() - 16) / charWidth)));
    if (bytes >= 8) bytes = bytes / 8 * 8;
    m_model->setBytesPerLine(bytes);
    followLatest();
}
void ReceiveView::copyRawSelection(QTextEdit *edit)
{
    const auto cursor = edit->textCursor();
    const int start = cursor.selectionStart(), end = cursor.selectionEnd();
    QStringList frames;
    for (int i = 0; i < m_ranges.size(); ++i) {
        const auto &range = m_ranges[i];
        const int from = edit == m_ascii ? range.rightStart : range.start;
        const int to = edit == m_ascii ? range.rightEnd : range.end;
        if (from == 0 && to == 0) continue;
        if (cursor.hasSelection() ? (start <= to && end > from) : (start >= from && start <= to))
            frames.append(m_model->rawHexForFrame(i));
    }
    if (!frames.isEmpty()) QApplication::clipboard()->setText(frames.join('\n'));
}
void ReceiveView::showCopyMenu(QTextEdit *edit, const QPoint &position)
{
    stopScrolling();
    std::unique_ptr<QMenu> menu(edit->createStandardContextMenu());
    menu->addSeparator();
    auto *raw = menu->addAction(QStringLiteral("Copy Selected Frames as Raw HEX"));
    if (menu->exec(edit->mapToGlobal(position)) == raw) copyRawSelection(edit);
}
