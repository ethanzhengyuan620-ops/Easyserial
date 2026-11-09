#include <QtTest>
#include <QAbstractItemModelTester>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QClipboard>
#include <QCryptographicHash>
#include <QTextBlock>
#include <QTextLayout>
#include <QScrollBar>
#include <QTableView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QFontMetricsF>
#include <QIcon>
#include <QListWidget>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QMenu>

#include "Settings.h"
#include "Theme.h"
#include "core/AutoReplyEngine.h"
#include "core/ColorEngine.h"
#include "core/FrameParser.h"
#include "core/HexUtils.h"
#include "core/TaskProtoDecoder.h"
#include "core/ProtoSchema.h"
#include "ui/PbDecoderWidget.h"
#include "model/ReceiveModel.h"
#include "ui/MainWindow.h"
#include "ui/MonitorWindow.h"
#include "ui/ReceiveView.h"
#include "ui/SendPanel.h"
#include "ui/SerialPanel.h"
#include "ui/ToolsDialog.h"
#include "ui/AppearanceDialog.h"
#include "ui/AutoReplyWidget.h"
#include "ui/RuleEditorDialog.h"

namespace {
QRect colorBounds(const QImage &image, const QColor &color)
{
    QRect bounds;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y).rgb() == color.rgb())
                bounds = bounds.united(QRect(x, y, 1, 1));
    return bounds;
}

QPushButton *button(QWidget &widget, const QString &text)
{
    for (auto *item : widget.findChildren<QPushButton *>())
        if (item->text() == text) return item;
    return nullptr;
}

void flush(ReceiveView &view)
{
    QMetaObject::invokeMethod(&view, "flushPending", Qt::DirectConnection);
    QApplication::processEvents();
}

void sendWheel(QWidget *target, int angle, QPoint pixels = {}, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const QPoint point(10, 10);
    QWheelEvent wheel(QPointF(point), QPointF(target->mapToGlobal(point)), pixels, QPoint(0, angle),
        Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QApplication::sendEvent(target, &wheel);
}

void dragWidget(QWidget *target, const QPoint &localStart, const QPoint &globalEnd)
{
    const QPoint globalStart = target->mapToGlobal(localStart);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(localStart), QPointF(globalStart),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(target->mapFromGlobal(globalEnd)), QPointF(globalEnd),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(target->mapFromGlobal(globalEnd)), QPointF(globalEnd),
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &release);
}
}

class RegressionTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        Theme::apply(*qobject_cast<QApplication *>(QApplication::instance()));
        QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
        QDir().mkpath("artifacts");
    }

    void settingsMigrationAndPersistence()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("config.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"col_bg", "#0D0D0D"}, {"col_text", "#123456"},
            {"col_hex", "#E0A86A"}, {"font_size", 18}, {"show_ts", false}}).toJson());
        file.close();
        Settings settings(path);
        QCOMPARE(settings.stringValue("col_bg"), QString("#FFFFFF"));
        QCOMPARE(settings.stringValue("col_text"), QString("#123456"));
        QCOMPARE(settings.stringValue("col_hex"), QString("#9A3412"));
        QCOMPARE(settings.intValue("font_size"), 18);
        QVERIFY(!settings.boolValue("show_ts"));
        settings.setValue("col_bg", "#FDF8E1");
        Settings reopened(path);
        QCOMPARE(reopened.stringValue("col_bg"), QString("#FDF8E1"));
        QCOMPARE(reopened.stringValue("col_text"), QString("#123456"));
    }

    void englishBrandingAndLegacyQuickCommands()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("config.json");
        QJsonArray legacy{
            QJsonObject{{"name", QStringLiteral("\u7248\u672c")}, {"data", "AT+VER?"}, {"hex", false}},
            QJsonObject{{"name", QStringLiteral("\u7248\u672c")}, {"data", "MY COMMAND"}, {"hex", false}},
            QJsonObject{{"name", "My custom command"}, {"data", "AT+RST"}, {"hex", false}}};
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"quick_cmds", legacy}}).toJson());
        file.close();
        Settings migrated(path);
        auto commands = migrated.arrayValue("quick_cmds");
        QCOMPARE(commands[0].toObject().value("name").toString(), QString("Version"));
        QCOMPARE(commands[1], legacy[1]);
        QCOMPARE(commands[2], legacy[2]);
        QVERIFY(migrated.save());
        QCOMPARE(Settings(path).arrayValue("quick_cmds"), commands);

        Settings clean(dir.filePath("clean.json"));
        MainWindow window(nullptr, dir.filePath("main.json"));
        ToolsDialog tools(clean);
        AppearanceDialog appearance(clean);
        AutoReplyRuleDialog autoReply;
        RuleEditorDialog colorRules(clean.arrayValue("rules"));
        const QRegularExpression han(QStringLiteral("\\p{Han}"));
        const QList<QWidget *> roots{&window, &tools, &appearance, &autoReply, &colorRules};
        for (auto *root : roots) {
            auto widgets = root->findChildren<QWidget *>();
            widgets.prepend(root);
            for (auto *widget : widgets) {
                for (const char *property : {"windowTitle", "text", "toolTip", "placeholderText"}) {
                    const QString text = widget->property(property).toString();
                    QVERIFY2(!han.match(text).hasMatch(), qPrintable(text));
                }
                if (auto *combo = qobject_cast<QComboBox *>(widget)) {
                    // A font picker may list localized, OS-provided font family names.
                    if (root != &appearance)
                        for (int i = 0; i < combo->count(); ++i)
                            QVERIFY2(!han.match(combo->itemText(i)).hasMatch(), qPrintable(combo->itemText(i)));
                }
                if (auto *tabs = qobject_cast<QTabWidget *>(widget))
                    for (int i = 0; i < tabs->count(); ++i)
                        QVERIFY(!han.match(tabs->tabText(i)).hasMatch());
            }
        }
        QCOMPARE(window.windowTitle(), QString("EasySerial"));
        const QIcon icon = window.windowIcon();
        QVERIFY(!icon.isNull());
        for (int size : {16, 32, 48, 256}) QVERIFY(!icon.pixmap(size, size).isNull());
        QVERIFY(icon.pixmap(256, 256).save("artifacts/app-icon.png"));
        QVERIFY(window.close());
    }

    void actualFontPixelsAndPerModeZoom()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", 0, false);
        settings.setValue("col_text", "#AA0055", false);
        settings.setArray("rules", {}, false);
        ReceiveView view(settings);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(1100, 360);
        view.show();
        view.appendFrame("MMMMMMMM", QDateTime::currentDateTime());
        flush(view);
        QTest::qWait(150);
        const auto small = colorBounds(view.textEdit()->viewport()->grab().toImage(), QColor("#AA0055"));
        QVERIFY(!small.isEmpty());
        settings.setValue("font_size", 20, false);
        view.applyAppearance();
        QTest::qWait(150);
        QCOMPARE(view.textEdit()->font().pointSize(), 20);
        const auto large = colorBounds(view.textEdit()->viewport()->grab().toImage(), QColor("#AA0055"));
        QVERIFY2(large.height() > small.height() * 1.5, "Glyphs must grow, not only row spacing");
        QVERIFY(large.width() > small.width() * 1.5);
        view.zoom(3);
        QCOMPARE(view.textEdit()->font().pointSize(), 23);
        view.setMode(DisplayMode::HexDual);
        QCOMPARE(view.textEdit()->font().pointSize(), 20);
        view.zoom(-2);
        view.setMode(DisplayMode::Ascii);
        QCOMPARE(view.textEdit()->font().pointSize(), 23);
        view.applyAppearance();
        view.setMode(DisplayMode::HexDual);
        QCOMPARE(view.textEdit()->font().pointSize(), 18); // Appearance must retain saved per-pane zoom.
    }

    void configuredColorsArePainted_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("ASCII") << 0;
        QTest::newRow("HEX dual") << 1;
        QTest::newRow("pure HEX") << 2;
    }

    void configuredColorsArePainted()
    {
        QFETCH(int, mode);
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", mode, false);
        settings.setValue("font_size", 18, false);
        settings.setArray("rules", {}, false);
        ReceiveView view(settings);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(1200, 380);
        view.show();
        view.appendFrame("MMMM hello", QDateTime::currentDateTime());
        flush(view);
        settings.setValue("col_bg", "#FFF4DB", false);
        settings.setValue("col_text", "#AD0068", false);
        settings.setValue("col_ts", "#075E22", false);
        settings.setValue("col_hex", "#B73A07", false);
        settings.setValue("col_ascii", "#0058BE", false);
        view.applyAppearance();
        QTest::qWait(150);
        const auto image = view.textEdit()->viewport()->grab().toImage();
        QVERIFY(!colorBounds(image, QColor("#FFF4DB")).isEmpty());
        QVERIFY(!colorBounds(image, QColor("#075E22")).isEmpty());
        QVERIFY(!colorBounds(image, QColor(mode == 0 ? "#AD0068" : "#B73A07")).isEmpty());
        if (mode == 1) QVERIFY(!colorBounds(image, QColor("#0058BE")).isEmpty());
        QVERIFY(image.save(QString("artifacts/colors-%1.png").arg(mode)));
    }

    void timestampHeaderWithoutReservedColumns()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", 2, false);
        settings.setValue("show_ts", false, false);
        ReceiveView view(settings);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(1000, 350);
        view.show();
        view.appendFrame("complete packet", QDateTime::currentDateTime());
        flush(view);
        QTest::qWait(150);
        QVERIFY(view.findChildren<QTableView *>().isEmpty());
        QCOMPARE(view.textEdit()->document()->firstBlock().text(), QString("RX"));
        const int width = view.textEdit()->viewport()->width();
        view.setShowTimestamps(true);
        QVERIFY(view.textEdit()->document()->firstBlock().text().endsWith("  RX"));
        view.setShowTimestamps(false);
        QCOMPARE(view.textEdit()->viewport()->width(), width);
        QCOMPARE(view.textEdit()->document()->firstBlock().text(), QString("RX"));
        for (auto mode : {DisplayMode::Ascii, DisplayMode::HexDual, DisplayMode::PureHex}) {
            view.setMode(mode);
            QVERIFY(view.textEdit()->isReadOnly());
            QVERIFY(view.findChildren<QTableView *>().isEmpty());
        }
    }

    void searchCountNavigationWholeWordAndHighlight()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", 0, false);
        ReceiveView view(settings);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(1200, 350);
        view.show();
        view.appendFrame("cat scatter CAT cat", QDateTime::currentDateTime());
        flush(view);
        QTest::qWait(150);
        QSignalSpy status(&view, &ReceiveView::searchStatusChanged);
        QVERIFY(view.findNext("cat", false, false, false, true));
        QCOMPARE(status.last().at(0).toInt(), 1);
        QCOMPARE(status.last().at(1).toInt(), 3);
        QVERIFY(!colorBounds(view.textEdit()->viewport()->grab().toImage(), QColor("#FBBF24")).isEmpty());
        QVERIFY(view.findNext("cat", false, false, true, true));
        QCOMPARE(status.last().at(0).toInt(), 3);
        QVERIFY(view.findNext("cat", false, true, false, true));
        QCOMPARE(status.last().at(1).toInt(), 2);
        QVERIFY(!view.findNext("[", true, false, false));
        QVERIFY(!status.last().at(2).toString().isEmpty());
        view.clearSearch();
        QVERIFY(colorBounds(view.textEdit()->viewport()->grab().toImage(), QColor("#FBBF24")).isEmpty());
    }

    void multilineAndInlineColors()
    {
        ReceiveModel model;
        model.setMode(DisplayMode::Ascii);
        model.appendFrames({{QByteArray("one\n[red:\"two\"]\r\nthree\n"), {},
            QDateTime::currentDateTime(), FrameDirection::Receive, true}});
        QCOMPARE(model.rowCount(), 4);
        QVERIFY(model.index(0, 2).data(ReceiveModel::HeaderRole).toBool());
        QCOMPARE(model.index(2, 2).data().toString(), QString("two"));
        QCOMPARE(model.rowForFrameOffset(0, 8), 3);
        model.setAsciiLayout(QFont(QStringLiteral("Consolas"), 10), 60);
        QCOMPARE(model.rowCount(), 4);
        QString wrapped;
        for (int row = 1; row < model.rowCount(); ++row)
            wrapped += model.index(row, 2).data().toString();
        QCOMPARE(wrapped, QString("onetwothree"));
        ColorEngine engine;
        engine.setRules({QJsonObject{{"pattern", "two"}, {"color", "#123456"}, {"enabled", true}}});
        auto parts = engine.split("[red:\"two\"] two", QColor("black"));
        QCOMPARE(parts[0].color, QColor("#FF5252"));
        QCOMPARE(parts.last().color, QColor("#123456"));
    }

    void sendParityAndAppearance()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("send_hex", true, false);
        settings.setValue("line_ending", 1, false);
        SendPanel panel(settings);
        panel.setAttribute(Qt::WA_DontShowOnScreen);
        panel.setSendEnabled(true);
        auto *input = panel.findChild<QPlainTextEdit *>("sendInput");
        auto *send = button(panel, QStringLiteral("Send"));
        QVERIFY(input && send);
        QSignalSpy sent(&panel, &SendPanel::sendBytes);
        QSignalSpy hints(&panel, &SendPanel::hint);
        input->setPlainText("01 03 FF");
        send->click();
        QCOMPARE(sent.takeFirst().at(0).toByteArray(), QByteArray::fromHex("0103FF"));
        input->setPlainText("123");
        send->click();
        QCOMPARE(sent.count(), 0);
        QVERIFY(!hints.isEmpty());
        for (auto *radio : panel.findChildren<QRadioButton *>())
            if (radio->text() == "ASCII") radio->click();
        input->setPlainText(QStringLiteral("你好"));
        send->click();
        QCOMPARE(sent.takeFirst().at(0).toByteArray(), QStringLiteral("你好\r\n").toUtf8());
        settings.setValue("font_size", 22, false);
        settings.setValue("col_text", "#AA0055", false);
        settings.setValue("col_bg", "#FFF4DB", false);
        panel.applyAppearance();
        panel.resize(1100, 360);
        panel.show();
        QTest::qWait(50);
        QCOMPARE(input->font().pointSize(), 22);
        QVERIFY(!colorBounds(input->viewport()->grab().toImage(), QColor("#FFF4DB")).isEmpty());
        QVERIFY(!colorBounds(input->viewport()->grab().toImage(), QColor("#AA0055")).isEmpty());
    }

    void framingHexAndAutoReply()
    {
        QCOMPARE(HexUtils::parse("0x01,0x03;FF").value(), QByteArray::fromHex("0103FF"));
        QVERIFY(!HexUtils::parse("0Z"));
        FrameParser parser;
        const auto now = QDateTime::currentDateTime();
        auto frames = parser.feed("abc\ndef", now, true);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].data, QByteArray("abc\n"));
        QCOMPARE(parser.flush()[0].data, QByteArray("def"));
        QVERIFY(parser.feed(QByteArray::fromHex("010A0D"), now, false).isEmpty());
        QCOMPARE(parser.flush()[0].data, QByteArray::fromHex("010A0D"));
        QJsonObject rule{{"rx_format", "ASCII"}, {"pattern", "PING"}, {"match", "exact"},
            {"tx_format", "HEX"}, {"reply", "01 02"}, {"line_ending", 1}};
        const auto result = AutoReplyEngine::find("PING\r\n", {rule});
        QVERIFY(result.matched);
        QCOMPARE(result.payload, QByteArray::fromHex("0102"));
        QVERIFY(!AutoReplyEngine::find("NO", {rule}).matched);
    }

    void boundedHistoryAndTimestampIntervals()
    {
        ReceiveModel model;
        model.setMode(DisplayMode::Ascii);
        model.setMaximumRows(200);
        QByteArray data;
        for (int i = 0; i < 1500; ++i) data += "line\n";
        model.appendFrames({{data, {}, QDateTime::currentDateTime(), FrameDirection::Receive, true}});
        QVERIFY(model.rowCount() <= 200);
        QVERIFY(model.rowCount() > 0);
        model.setMode(DisplayMode::PureHex);
        QVERIFY(model.rowCount() <= 200);
        model.clear();
        model.setMode(DisplayMode::Ascii);
        model.setTimestampOptions(true, 100);
        const auto now = QDateTime::currentDateTime();
        model.appendFrames({{"a\n", {}, now}, {"b\n", {}, now.addMSecs(50)}, {"c\n", {}, now.addMSecs(150)}});
        QVERIFY(!model.index(0, 2).data(ReceiveModel::TimestampTextRole).toString().isEmpty());
        QVERIFY(model.index(2, 2).data(ReceiveModel::TimestampTextRole).toString().isEmpty());
        QVERIFY(!model.index(3, 2).data(ReceiveModel::TimestampTextRole).toString().isEmpty());
        model.setMode(DisplayMode::HexDual);
        QVERIFY(!model.index(2, 2).data(ReceiveModel::TimestampTextRole).toString().isEmpty());
        model.setTimestampOptions(false, 100);
        for (int i = 0; i < model.rowCount(); ++i)
            QVERIFY(model.index(i, 2).data(ReceiveModel::TimestampTextRole).toString().isEmpty());
    }

    void oneDirectionHeaderPerTimestampGroup()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", 0, false);
        settings.setValue("font_size", 16, false);
        ReceiveView view(settings);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(700, 300);
        view.show();
        const auto now = QDateTime::fromString("2026-09-20T07:17:39.519", Qt::ISODateWithMs);
        view.appendFrame("press_down_handler\r\n", now);
        flush(view);
        // Also check append across batches, not just a single list of frames.
        view.appendFrame("press_up_handler\r\n", now.addMSecs(50));
        flush(view);
        QTest::qWait(150);
        auto *model = view.model();
        QCOMPARE(model->rowCount(), 3);
        QCOMPARE(model->displayTextForRow(0), QString("[07:17:39.519]  RX"));
        QCOMPARE(model->displayTextForRow(1), QString("press_down_handler"));
        QCOMPARE(model->displayTextForRow(2), QString("press_up_handler"));
        QVERIFY(!model->index(2, 2).data(ReceiveModel::HeaderRole).toBool());
        QCOMPARE(model->rowForFrameOffset(1, 0), 2);
        QVERIFY(view.grab().save("artifacts/single-rx-per-timestamp.png"));
        view.appendTransmit("ACK", now.addMSecs(60));
        flush(view);
        QCOMPARE(model->displayTextForRow(3), QString("[07:17:39.579]  TX"));
        QCOMPARE(model->displayTextForRow(4), QString("ACK"));
        view.setTimestampInterval(20);
        QCOMPARE(model->rowCount(), 6);
        view.setTimestampInterval(100);
        QCOMPARE(model->rowCount(), 5);
        view.setShowTimestamps(false);
        QCOMPARE(model->rowCount(), 5);
        QCOMPARE(model->displayTextForRow(0), QString("RX"));
        QCOMPARE(model->displayTextForRow(3), QString("TX"));
        view.setShowTimestamps(true);
        QCOMPARE(model->rowCount(), 5);
        model->setMaximumRows(200);
        QVector<ReceiveFrame> frames;
        for (int i = 0; i < 300; ++i)
            frames.push_back({"body", {}, now.addMSecs(70 + i)});
        model->appendFrames(std::move(frames));
        QVERIFY(model->rowCount() <= 200);
        QVERIFY(model->index(0, 2).data(ReceiveModel::HeaderRole).toBool());
    }

    void asciiChunkBoundariesDoNotAddNewlines_data()
    {
        QTest::addColumn<bool>("timestamps");
        QTest::addColumn<bool>("monitor");
        QTest::addColumn<bool>("separateBatches");
        for (bool ts : {false, true})
            for (bool floating : {false, true})
                for (bool batches : {false, true})
                    QTest::newRow(qPrintable(QString("ts%1-monitor%2-batches%3").arg(ts).arg(floating).arg(batches)))
                        << ts << floating << batches;
    }

    void asciiChunkBoundariesDoNotAddNewlines()
    {
        QFETCH(bool, timestamps);
        QFETCH(bool, monitor);
        QFETCH(bool, separateBatches);
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", 0, false);
        settings.setValue("show_ts", timestamps, false);
        settings.setValue("ts_interval", 60000, false);
        ReceiveView view(settings);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.setMonitorMode(monitor);
        const auto now = QDateTime::fromString("2026-11-01T12:00:00.000", Qt::ISODateWithMs);
        const QByteArray payload = "mb test 5\r\nfeed: 00 01 00 00 00 00\r\n\r\nfeed: put 6, buf 6\nlast\r\n\r\n";
        const QString header = timestamps ? "[12:00:00.000]  RX\n" : "RX\n";
        QString body = QString::fromLatin1(payload);
        body.replace("\r\n", "\n");
        view.appendFrame(payload, now);
        flush(view);
        QCOMPARE(view.textEdit()->toPlainText(), header + body);
        // Every possible cut includes a mid-word cut and every split CR/LF.
        for (int cut = 1; cut < payload.size(); ++cut) {
            view.clearAll();
            FrameParser parser;
            int part = 0;
            for (const auto &chunk : {payload.left(cut), payload.mid(cut)}) {
                auto frames = parser.feed(chunk, now.addMSecs(part++ * 30), false);
                frames += parser.flush(); // Same flush as the 20 ms idle gap.
                for (const auto &frame : frames) view.appendFrame(frame.data, frame.timestamp);
                if (separateBatches) flush(view);
            }
            flush(view);
            QCOMPARE(view.textEdit()->toPlainText(), header + body);
            QCOMPARE(view.model()->frameAt(0)->data + view.model()->frameAt(1)->data, payload);
        }
    }

    void asciiTimestampWaitsForRealLineBoundary()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", 0, false);
        ReceiveView view(settings);
        const auto now = QDateTime::fromString("2026-11-01T12:00:00.000", Qt::ISODateWithMs);
        view.appendFrame("feed: 00 ", now);
        flush(view);
        view.appendFrame("01 00 00 00 00", now.addSecs(5)); // Far beyond the timestamp interval.
        flush(view);
        QCOMPARE(view.textEdit()->toPlainText(), QString("[12:00:00.000]  RX\nfeed: 00 01 00 00 00 00"));
        view.appendFrame("\r", now.addSecs(6));
        flush(view);
        view.appendFrame("\n", now.addSecs(7));
        flush(view);
        QCOMPARE(view.textEdit()->toPlainText(), QString("[12:00:00.000]  RX\nfeed: 00 01 00 00 00 00\n"));
        view.appendFrame("next line", now.addSecs(8));
        flush(view);
        QCOMPARE(view.textEdit()->toPlainText(), QString("[12:00:00.000]  RX\nfeed: 00 01 00 00 00 00\n[12:00:08.000]  RX\nnext line"));
        // A real direction change still has a distinct header, not merged RX/TX.
        view.appendTransmit("ACK", now.addSecs(9));
        flush(view);
        QVERIFY(view.textEdit()->toPlainText().endsWith("next line\n[12:00:09.000]  TX\nACK"));
        view.appendNote("status", now.addSecs(10));
        view.appendFrame("RX again\n", now.addSecs(11));
        flush(view);
        QVERIFY(view.textEdit()->toPlainText().endsWith("NOTE\nstatus\n[12:00:11.000]  RX\nRX again\n"));
        view.clearAll();
        view.appendFrame("\nnew stream", now);
        flush(view);
        QCOMPARE(view.textEdit()->toPlainText(), QString("[12:00:00.000]  RX\n\nnew stream"));
    }

    void asciiSizeSplitWrapAndRebuild()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", 0, false);
        ReceiveView view(settings);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(550, 300);
        view.show();
        const auto now = QDateTime::fromString("2026-11-01T12:00:00.000", Qt::ISODateWithMs);
        const QByteArray payload = QByteArray(9000, 'W') + " tail\r\n\r\n";
        FrameParser parser;
        auto frames = parser.feed(payload, now, false);
        frames += parser.flush();
        QCOMPARE(frames.size(), 3);
        for (int i = 0; i < frames.size(); ++i) {
            view.appendFrame(frames[i].data, now.addSecs(i));
            flush(view);
        }
        const QString expected = "[12:00:00.000]  RX\n" + QString(9000, 'W') + " tail\n\n";
        QCOMPARE(view.textEdit()->toPlainText(), expected);
        QTest::qWait(100);
        QVERIFY(view.textEdit()->document()->findBlockByNumber(1).layout()->lineCount() > 1);
        QTextCursor cursor(view.textEdit()->document());
        const int start = expected.indexOf('W') + 4090;
        cursor.setPosition(start);
        cursor.setPosition(start + 12, QTextCursor::KeepAnchor);
        view.textEdit()->setTextCursor(cursor);
        QCOMPARE(cursor.selectedText(), QString(12, 'W')); // Crosses the parser's 4096-byte cut.
        view.resize(300, 300);
        view.zoom(2);
        QTest::qWait(100);
        QCOMPARE(view.textEdit()->toPlainText(), expected);
        QCOMPARE(view.textEdit()->textCursor().selectedText(), QString(12, 'W'));
        view.applyAppearance();
        QCOMPARE(view.textEdit()->toPlainText(), expected);
        view.setMonitorMode(true);
        view.setMonitorMode(false);
        QCOMPARE(view.textEdit()->toPlainText(), expected);
        for (const auto mode : {DisplayMode::HexDual, DisplayMode::PureHex}) {
            view.setMode(mode);
            QCOMPARE(view.model()->frameCount(), qsizetype{3});
            QCOMPARE(view.textEdit()->toPlainText().count("  RX"), 3); // HEX remains frame based.
            view.setMode(DisplayMode::Ascii);
            QCOMPARE(view.textEdit()->toPlainText(), expected);
        }
        QVERIFY(view.findNext(QString(8, 'W') + " tail", false, true, false));
    }

    void asciiStreamPruningPreservesCrLfContext()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", 0, false);
        settings.setValue("show_ts", false, false);
        settings.setValue("max_lines", 200, false);
        ReceiveView view(settings);
        QAbstractItemModelTester tester(view.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        const auto now = QDateTime::currentDateTime();
        const QList<QByteArray> fragments{"feed: ", "00 ", "01\r", "\n"};
        for (int i = 0; i < 350; ++i) {
            view.appendFrame(fragments[i % fragments.size()], now.addMSecs(i));
            flush(view);
            if (i < 200) continue;
            const QString before = view.textEdit()->toPlainText();
            QVERIFY(before.startsWith("RX\n"));
            QVERIFY(!before.contains("\n\n"));
            QVERIFY(!before.contains("feed: \n"));
            view.applyAppearance();
            QCOMPARE(view.textEdit()->toPlainText(), before);
            QVERIFY(view.model()->rowCount() <= 200);
        }
    }

    void wheelSynchronizesLabelAndFontRelativeSpacing()
    {
        QTemporaryDir dir;
        MainWindow window(nullptr, dir.filePath("config.json"));
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
        auto *view = window.findChild<ReceiveView *>();
        auto *label = window.findChild<QLabel *>("currentFontSize");
        QVERIFY(view && label);
        auto wheel = [](QWidget *target, int delta) {
            const QPointF point(40, 40);
            QWheelEvent event(point, target->mapToGlobal(point.toPoint()), {}, QPoint(0, delta),
                              Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(target, &event);
        };
        QCOMPARE(label->text(), QString("9"));
        wheel(view->textEdit()->viewport(), 120);
        QCOMPARE(label->text(), QString("10"));
        QCOMPARE(view->textEdit()->font().pointSize(), 10);
        button(window, QStringLiteral("A+"))->click();
        QCOMPARE(label->text(), QString("11"));
        view->zoom(-5);
        const int small = QFontMetrics(view->textEdit()->font()).lineSpacing();
        QCOMPARE(label->text(), QString("6"));
        view->zoom(18);
        const int large = QFontMetrics(view->textEdit()->font()).lineSpacing();
        QVERIFY(large > small * 2);
        QCOMPARE(view->textEdit()->document()->defaultFont().pointSize(), 24);
        QCOMPARE(label->text(), QString("24"));
        view->setMode(DisplayMode::Ascii);
        QCOMPARE(label->text(), QString("11"));
        auto *input = window.findChild<QPlainTextEdit *>("sendInput");
        wheel(input->viewport(), 120);
        QCOMPARE(label->text(), QString("12"));
        QCOMPARE(input->font().pointSize(), 12);
        QVERIFY(!window.findChild<SerialPanel *>()->parameters().splitNewline);
        QVERIFY(window.close());
    }

    void packetLayoutUsesFullWidthAndPreservesBody()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", 0, false);
        ReceiveView view(settings);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(900, 620);
        view.show();
        const QByteArray packet = "Range: bytes=23048-231311\r\nUser-Agent: 290c13457865/3016/1\r\nConnection: keep-alive\r\n\r\n"
            + QByteArray(220, 'W') + "\r\nOK\r\n";
        view.appendFrame(packet, QDateTime::currentDateTime());
        view.appendTransmit("AT+CIPSEND=4,172\r\n", QDateTime::currentDateTime());
        flush(view);
        QTest::qWait(150);
        QCOMPARE(view.model()->frameCount(), qsizetype{2});
        auto *edit = view.textEdit();
        const QString original = edit->toPlainText();
        QCOMPARE(original.count("  RX"), 1);
        QCOMPARE(original.count("  TX"), 1);
        QVERIFY(original.contains(QString(220, 'W')));
        QCOMPARE(edit->lineWrapMode(), QTextEdit::WidgetWidth);
        QVERIFY(view.asciiEdit()->isHidden());
        const int longStart = original.indexOf(QString(220, 'W'));
        const auto block = edit->document()->findBlock(longStart);
        const int wideLines = block.layout()->lineCount();
        QVERIFY(wideLines > 1);
        QVERIFY(view.grab().save("artifacts/packet-layout.png"));
        view.resize(520, 620);
        QTest::qWait(150);
        QCOMPARE(edit->toPlainText(), original); // Soft wraps never add copied newlines.
        QVERIFY(block.layout()->lineCount() > wideLines);
        view.setMode(DisplayMode::HexDual);
        QTest::qWait(150);
        QVERIFY(!view.asciiEdit()->isHidden());
        QCOMPARE(edit->document()->blockCount(), view.asciiEdit()->document()->blockCount());
        view.setMode(DisplayMode::Ascii);
        QTest::qWait(150);
        QCOMPARE(edit->toPlainText(), original);
    }

    void characterSelectionSurvivesWrappingAndIncomingData()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", 0, false);
        settings.setValue("font_size", 12, false);
        ReceiveView view(settings);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(600, 330);
        view.show();
        view.appendFrame("protobuf recv num(81) tag[0x5B02].\n" + QByteArray(300, 'W'), QDateTime::currentDateTime());
        flush(view);
        QTest::qWait(100);
        auto *edit = view.textEdit();
        edit->verticalScrollBar()->setValue(0);
        edit->setFocus();
        const int start = edit->toPlainText().indexOf("recv");
        QTextCursor cursor(edit->document());
        cursor.setPosition(start);
        const QPoint from = edit->cursorRect(cursor).center();
        cursor.setPosition(start + 4);
        const QPoint to = edit->cursorRect(cursor).center();
        QTest::mousePress(edit->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(edit->viewport()->mapToGlobal(to)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(edit->viewport(), &move);
        QTest::mouseRelease(edit->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCOMPARE(edit->textCursor().selectedText(), QString("recv")); // Not the whole row.
        const int longStart = edit->toPlainText().indexOf(QString(300, 'W'));
        cursor.setPosition(longStart + 3);
        cursor.setPosition(longStart + 160, QTextCursor::KeepAnchor);
        edit->setTextCursor(cursor);
        QCOMPARE(edit->textCursor().selectedText(), QString(157, 'W'));
        view.resize(340, 330);
        QTest::qWait(100);
        QCOMPARE(edit->textCursor().selectedText(), QString(157, 'W'));
        view.appendFrame("new incoming data", QDateTime::currentDateTime());
        flush(view);
        QCOMPARE(edit->textCursor().selectedText(), QString(157, 'W'));
        QVERIFY(view.grab().save("artifacts/character-selection.png"));
        view.zoom(-2);
        QTest::qWait(100);
        QCOMPARE(edit->textCursor().selectedText(), QString(157, 'W'));
        view.setAutoScroll(false);
        view.model()->setMaximumRows(200);
        QVector<ReceiveFrame> many;
        for (int i = 0; i < 1000; ++i)
            many.append({"line", {}, QDateTime::currentDateTime().addMSecs(i * 200)});
        view.model()->appendFrames(std::move(many));
        QVERIFY(edit->document()->blockCount() <= 200);
        QVERIFY(view.model()->rowCount() <= 200);
    }

    void smoothFollowing_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("ASCII") << 0;
        QTest::newRow("HEX dual") << 1;
        QTest::newRow("pure HEX") << 2;
    }

    void smoothFollowing()
    {
        QFETCH(int, mode);
        QTemporaryDir dir;
        // A previously saved disabled switch must no longer disable animation.
        QFile legacy(dir.filePath("config.json"));
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write(QJsonDocument(QJsonObject{{"mode", mode}, {"smooth_scroll", false}}).toJson());
        legacy.close();
        Settings settings(dir.filePath("config.json"));
        QVERIFY(!settings.value("smooth_scroll").isValid());
        QVERIFY(settings.save());
        QVERIFY(legacy.open(QIODevice::ReadOnly));
        QVERIFY(!QJsonDocument::fromJson(legacy.readAll()).object().contains("smooth_scroll"));
        legacy.close();
        ReceiveView view(settings);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(700, 320);
        view.show();
        QTest::qWait(100);
        const auto now = QDateTime::currentDateTime();
        for (int i = 0; i < 60; ++i) view.appendFrame("packet 1234\n", now.addMSecs(i * 200));
        flush(view);
        auto *bar = view.textEdit()->verticalScrollBar();
        QTRY_COMPARE(bar->value(), bar->maximum());
        QVERIFY(bar->maximum() > 500);
        const int before = bar->value();
        QSignalSpy movement(bar, &QScrollBar::valueChanged);
        view.appendFrame("new packet\n", now.addSecs(20));
        flush(view);
        QVERIFY(bar->maximum() > before);
        QVERIFY2(bar->value() < bar->maximum(), "Small appends must not jump straight to the bottom");
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), bar->maximum(), 500);
        QVERIFY(movement.size() >= 3);
        bool intermediate = false;
        bool subLineStep = false;
        int previous = before;
        for (const auto &event : movement) {
            const int value = event[0].toInt();
            QVERIFY(value >= previous);
            intermediate |= value > before && value < bar->maximum();
            subLineStep |= value > previous && value - previous < QFontMetrics(view.textEdit()->font()).lineSpacing();
            previous = value;
        }
        QVERIFY(intermediate);
        QVERIFY2(subLineStep, "Scrolling must move in pixels, not whole text lines");
        if (mode == 1) QCOMPARE(view.asciiEdit()->verticalScrollBar()->value(), bar->value());

        // Repeated batches retarget the existing motion rather than queueing it.
        for (int i = 0; i < 15; ++i) {
            view.appendFrame("continuous packet\n", now.addSecs(21).addMSecs(i * 200));
            flush(view);
            QTest::qWait(20);
            QVERIFY(bar->maximum() - bar->value() < view.height() * 2);
        }
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), bar->maximum(), 500);
        const int bottom = bar->value();
        sendWheel(view.textEdit()->viewport(), 120);
        QTRY_VERIFY_WITH_TIMEOUT(bar->value() < bottom, 500);
        QVERIFY(!view.findChild<QPushButton *>("backToLatest"));
        // Auto scroll stays enabled: new data replaces the manual wheel target.
        view.appendFrame("keep following incoming data\n", now.addSecs(30));
        flush(view);
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), bar->maximum(), 500);

        // Huge bursts are already fully present, without an animation backlog.
        for (int i = 0; i < 100; ++i) view.appendFrame("burst\n", now.addSecs(40).addMSecs(i * 200));
        flush(view);
        QCOMPARE(bar->value(), bar->maximum());
        view.appendFrame("animation stays enabled after a burst\n", now.addSecs(80));
        flush(view);
        QVERIFY(bar->value() < bar->maximum());
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), bar->maximum(), 500);
    }

    void incrementalHistoryPreservesSelectionAndViewport()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", 0, false);
        settings.setValue("max_lines", 200, false);
        settings.setValue("show_ts", false, false);
        ReceiveView view(settings);
        QAbstractItemModelTester tester(view.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(700, 320);
        view.show();
        const auto now = QDateTime::currentDateTime();
        for (int i = 0; i < 190; ++i) view.appendFrame("message_" + QByteArray::number(i) + '\n', now.addMSecs(i));
        flush(view);
        view.setAutoScroll(false); // Only this explicit option pauses following.
        QSignalSpy resets(view.model(), &QAbstractItemModel::modelReset);
        QSignalSpy pruning(view.model(), &ReceiveModel::framesPruned);
        QSignalSpy changes(view.textEdit()->document(), &QTextDocument::contentsChange);
        auto *edit = view.textEdit();
        QTextCursor selection(edit->document());
        const int position = edit->toPlainText().indexOf("message_150");
        selection.setPosition(position);
        selection.setPosition(position + 11, QTextCursor::KeepAnchor);
        edit->setTextCursor(selection);
        edit->ensureCursorVisible();
        auto anchor = edit->cursorForPosition(QPoint(0, 0));
        const int top = edit->cursorRect(anchor).top();
        const int oldCharacters = edit->document()->characterCount();
        for (int i = 190; i < 215; ++i) view.appendFrame("message_" + QByteArray::number(i) + '\n', now.addMSecs(i));
        flush(view);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(pruning.size(), 1);
        QCOMPARE(edit->textCursor().selectedText(), QString("message_150"));
        QVERIFY(qAbs(edit->cursorRect(anchor).top() - top) <= 1);
        QVERIFY(edit->toPlainText().startsWith("RX\n"));
        QVERIFY(edit->toPlainText().contains("message_214"));
        QVERIFY(view.model()->rowCount() <= 200);
        for (const auto &change : changes)
            QVERIFY2(change[1].toInt() < oldCharacters / 2, "Eviction must not clear/rebuild the retained document");
        for (int i = 215; i < 800; ++i) {
            view.appendFrame("message_" + QByteArray::number(i) + '\n', now.addMSecs(i));
            if (i % 20 == 0) flush(view);
        }
        flush(view);
        QCOMPARE(resets.size(), 0);
        QVERIFY(edit->document()->blockCount() <= 200);
        QVERIFY(edit->toPlainText().endsWith("message_799\n"));
        QCOMPARE(edit->document()->blockCount(), view.model()->rowCount() + 1);
        // Every visible frame range must remain aligned with its raw model.
        QStringList expected;
        for (int i = 0; i < view.model()->rowCount(); ++i) expected << view.model()->displayTextForRow(i);
        QCOMPARE(edit->toPlainText(), expected.join('\n') + '\n');
        QVERIFY(view.grab().save("artifacts/paused-history.png"));
    }

    void incrementalHistoryAcrossModes()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("max_lines", 200, false);
        ReceiveView view(settings);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(700, 320);
        view.show();
        QAbstractItemModelTester tester(view.model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto now = QDateTime::currentDateTime();
        for (const auto mode : {DisplayMode::Ascii, DisplayMode::HexDual, DisplayMode::PureHex}) {
            view.clearAll();
            view.setMode(mode);
            QTest::qWait(100);
            QSignalSpy resets(view.model(), &QAbstractItemModel::modelReset);
            for (int batch = 0; batch < 50; ++batch) {
                for (int i = 0; i < 10; ++i) {
                    const int sequence = batch * 10 + i;
                    view.appendFrame("message_" + QByteArray::number(sequence) + '\n', now.addMSecs(sequence * 50));
                }
                flush(view);
                QStringList expected;
                for (int row = 0; row < view.model()->rowCount(); ++row)
                    expected << view.model()->index(row, 2).data().toString();
                QCOMPARE(view.textEdit()->toPlainText(), expected.join('\n') + (mode == DisplayMode::Ascii ? "\n" : ""));
                QVERIFY(view.model()->rowCount() <= 200);
                if (mode == DisplayMode::HexDual)
                    QCOMPARE(view.textEdit()->document()->blockCount(), view.asciiEdit()->document()->blockCount());
            }
            QCOMPARE(resets.size(), 0);
        }
        // A large boundary frame must retain its tail, not disappear wholesale.
        view.clearAll();
        view.setMode(DisplayMode::Ascii);
        QByteArray packet;
        for (int i = 0; i < 185; ++i) packet += "long_packet_" + QByteArray::number(i) + '\n';
        view.appendFrame(packet, now);
        flush(view);
        for (int i = 0; i < 25; ++i) view.appendFrame("tail\n", now.addMSecs(i + 1));
        flush(view);
        QVERIFY(view.textEdit()->toPlainText().contains("long_packet_184"));
        QVERIFY(view.model()->rowCount() > 150);
        QVERIFY(view.model()->rowCount() <= 200);
    }

    void followingControlsNeverLatchPaused()
    {
        QTemporaryDir dir;
        MainWindow window(nullptr, dir.filePath("config.json"));
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
        auto *view = window.findChild<ReceiveView *>();
        view->setMode(DisplayMode::Ascii);
        QVERIFY(!window.findChild<QCheckBox *>("smoothScroll"));
        for (auto *box : window.findChildren<QCheckBox *>())
            QVERIFY(box->text() != "Smooth scroll");
        auto now = QDateTime::currentDateTime();
        for (int i = 0; i < 100; ++i) view->appendFrame("find_me_" + QByteArray::number(i) + '\n', now.addMSecs(i * 200));
        flush(*view);
        QVERIFY(view->findNext("find_me_2", false, true, false));
        auto *bar = view->textEdit()->verticalScrollBar();
        QVERIFY(bar->value() < bar->maximum());
        view->appendFrame("new\n", now.addSecs(30));
        flush(*view);
        QCOMPARE(bar->value(), bar->maximum());
        QVERIFY(!view->findChild<QPushButton *>("backToLatest"));
        QTextCursor selection(view->textEdit()->document());
        const int match = view->textEdit()->toPlainText().indexOf("find_me_2");
        selection.setPosition(match);
        selection.setPosition(match + 9, QTextCursor::KeepAnchor);
        view->textEdit()->setTextCursor(selection);
        view->textEdit()->ensureCursorVisible();
        QVERIFY(bar->value() < bar->maximum());
        view->appendFrame("still following with a selection\n", now.addSecs(31));
        flush(*view);
        QCOMPARE(bar->value(), bar->maximum());
        QCOMPARE(view->textEdit()->textCursor().selectedText(), QString("find_me_2"));
        QCheckBox *autoscroll = nullptr;
        for (auto *box : window.findChildren<QCheckBox *>())
            if (box->text() == "Auto scroll") autoscroll = box;
        QVERIFY(autoscroll);
        QVERIFY(autoscroll->isChecked());
        autoscroll->setChecked(false);
        QVERIFY(!Settings(dir.filePath("config.json")).boolValue("autoscroll", true));
        view->findNext("find_me_3", false, true, false);
        const int position = bar->value();
        view->appendFrame("preserve history only when auto scroll is off\n", now.addSecs(32));
        flush(*view);
        QTest::qWait(200);
        QCOMPARE(bar->value(), position);
        autoscroll->setChecked(true);
        QVERIFY(autoscroll->isChecked());
        QCOMPARE(bar->value(), bar->maximum());
        view->clearAll();
        QVERIFY(!view->findChild<QPushButton *>("backToLatest"));
        QVERIFY(window.grab().save("artifacts/smooth-scroll-controls.png"));
    }

    void smoothMouseWheel_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("ASCII") << 0;
        QTest::newRow("HEX dual") << 1;
        QTest::newRow("pure HEX") << 2;
    }

    void smoothMouseWheel()
    {
        QFETCH(int, mode);
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", mode, false);
        settings.setValue("autoscroll", false, false);
        ReceiveView view(settings);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(700, 320);
        view.show();
        QTest::qWait(120);
        const auto now = QDateTime::currentDateTime();
        for (int i = 0; i < 80; ++i) view.appendFrame("packet 1234\n", now.addMSecs(i * 200));
        flush(view);
        auto *bar = view.textEdit()->verticalScrollBar();
        const int middle = bar->maximum() / 2;
        const int step = qMin(bar->pageStep(), QApplication::wheelScrollLines() * QFontMetrics(view.textEdit()->font()).lineSpacing());
        QVERIFY(step > 1);
        bar->setValue(middle);
        QSignalSpy movement(bar, &QScrollBar::valueChanged);
        auto *surface = mode == 1 ? view.asciiEdit()->viewport() : view.textEdit()->viewport();
        sendWheel(surface, 120);
        QCOMPARE(bar->value(), middle); // No whole-notch jump on dispatch.
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), middle - step, 1000);
        QVERIFY(movement.size() >= 3);
        int previous = middle;
        bool subLineStep = false;
        for (const auto &event : movement) {
            const int value = event[0].toInt();
            QVERIFY(value <= previous);
            subLineStep |= value < previous && previous - value < QFontMetrics(view.textEdit()->font()).lineSpacing();
            previous = value;
        }
        QVERIFY(subLineStep);
        movement.clear();
        sendWheel(surface, -120);
        QCOMPARE(bar->value(), middle - step);
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), middle, 1000);
        QVERIFY(movement.size() >= 3);
        previous = middle - step;
        for (const auto &event : movement) {
            const int value = event[0].toInt();
            QVERIFY(value >= previous);
            previous = value;
        }
        if (mode == 1) QCOMPARE(view.asciiEdit()->verticalScrollBar()->value(), bar->value());

        // Rapid wheel notches accumulate once and can reverse without queuing.
        sendWheel(surface, 120);
        sendWheel(surface, 120);
        sendWheel(surface, -120);
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), middle - step, 1000);
        sendWheel(surface, 0, QPoint(0, -37));
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), middle - step + 37, 1000);
        // New data with Auto scroll OFF must not overwrite a manual animation.
        const int before = bar->value();
        sendWheel(surface, 120);
        view.appendFrame("new data while scrolling history\n", now.addSecs(40));
        flush(view);
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), before - step, 1000);

        // Wheel events over the scrollbar itself use the same smooth controller.
        sendWheel(bar, -120);
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), before, 1000);
        const int size = view.fontSize();
        sendWheel(surface, 120, {}, Qt::ControlModifier);
        QCOMPARE(view.fontSize(), size + 1);
        QTest::qWait(120);
        const int current = bar->value();
        QWheelEvent horizontal(QPointF(10, 10), QPointF(10, 10), {}, QPoint(120, 0),
            Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(surface, &horizontal);
        QCOMPARE(view.fontSize(), size + 1);
        QCOMPARE(bar->value(), current);

        sendWheel(surface, 120 * 200);
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), 0, 1000);
        sendWheel(surface, -120 * 200);
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), bar->maximum(), 1000);
        sendWheel(surface, -120);
        QTest::qWait(60);
        QCOMPARE(bar->value(), bar->maximum());
        // Reaching a boundary never disables animation on the next wheel turn.
        const int nextStep = qMin(bar->pageStep(), QApplication::wheelScrollLines() * QFontMetrics(view.textEdit()->font()).lineSpacing());
        sendWheel(surface, 120);
        QCOMPARE(bar->value(), bar->maximum());
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), bar->maximum() - nextStep, 1000);
        sendWheel(surface, -120);
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), bar->maximum(), 1000);
    }

    void fontSizesPersistAcrossRestart()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("config.json");
        {
            Settings settings(path);
            settings.setValue("mode", 0);
            ReceiveView view(settings);
            QCOMPARE(view.fontSize(), 9);
            view.zoom(3);
            view.setMode(DisplayMode::HexDual);
            view.zoom(-1);
            view.setMode(DisplayMode::PureHex);
            view.zoom(2);
            SendPanel send(settings);
            auto *input = send.findChild<QPlainTextEdit *>("sendInput");
            QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), {}, QPoint(0, -120),
                              Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(input->viewport(), &wheel);
            QCOMPARE(input->font().pointSize(), 8);
            view.applyAppearance();
            QCOMPARE(view.fontSize(), 11);
        }
        Settings reopened(path);
        QCOMPARE(reopened.arrayValue("receive_font_sizes"), QJsonArray({12, 8, 11}));
        QCOMPARE(reopened.intValue("send_font_size"), 8);
        MainWindow window(nullptr, path);
        auto *view = window.findChild<ReceiveView *>();
        auto *label = window.findChild<QLabel *>("currentFontSize");
        QCOMPARE(view->fontSize(), 12);
        QCOMPARE(label->text(), QString("12"));
        QCOMPARE(window.findChild<QPlainTextEdit *>("sendInput")->font().pointSize(), 8);
        button(window, QStringLiteral("A+"))->click();
        QCOMPARE(Settings(path).arrayValue("receive_font_sizes"), QJsonArray({13, 13, 13}));
        QCOMPARE(Settings(path).intValue("send_font_size"), 13);
        view->setMode(DisplayMode::HexDual);
        QCOMPARE(view->fontSize(), 13);
        QVERIFY(window.close());
    }

    void colorRulesRepaintExistingText()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        settings.setValue("mode", 0, false);
        settings.setValue("font_size", 18, false);
        ReceiveView view(settings);
        view.setAttribute(Qt::WA_DontShowOnScreen);
        view.resize(1100, 350);
        view.show();
        view.appendFrame("MMMM", QDateTime::currentDateTime());
        flush(view);
        QTest::qWait(150);
        view.setRules({QJsonObject{{"pattern", "MMMM"}, {"color", "#CC0088"}, {"enabled", true}}});
        QVERIFY(!colorBounds(view.textEdit()->viewport()->grab().toImage(), QColor("#CC0088")).isEmpty());
        view.setRules({QJsonObject{{"pattern", "MMMM"}, {"color", "#CC0088"}, {"enabled", false}}});
        QVERIFY(colorBounds(view.textEdit()->viewport()->grab().toImage(), QColor("#CC0088")).isEmpty());
    }

    void monitorModeRoundTrip_data()
    {
        QTest::addColumn<bool>("pinned");
        QTest::newRow("normal priority") << false;
        QTest::newRow("already pinned") << true;
    }

    void monitorModeRoundTrip()
    {
        QFETCH(bool, pinned);
        QTemporaryDir dir;
        const QString path = dir.filePath("config.json");
        Settings config(path);
        config.setValue("mode", 0, false);
        config.setValue("pin", pinned, false);
        config.setValue("send_font_size", 14, false);
        config.setArray("receive_font_sizes", {11, 12, 13});
        MainWindow window(nullptr, path);
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.resize(1100, 760);
        window.show();
        QTest::qWait(100);
        auto *view = window.findChild<ReceiveView *>();
        auto *model = view->model();
        auto *document = view->textEdit()->document();
        auto *send = window.findChild<QPlainTextEdit *>("sendInput");
        send->setPlainText("unsent data must remain");
        auto *repeatTimer = window.findChild<SendPanel *>()->findChild<QTimer *>();
        QVERIFY(repeatTimer);
        repeatTimer->start(60000); // No send will fire; verify switching doesn't stop it.
        const auto now = QDateTime::currentDateTime();
        view->appendFrame("payload before switching\nOK", now);
        flush(*view);
        const QString initial = view->textEdit()->toPlainText();
        const QRect normal = window.geometry();
        auto *splitter = window.findChild<QSplitter *>("mainSplitter");
        const auto sizes = splitter->sizes();
        QTextCursor selected(document);
        const int start = initial.indexOf("payload");
        selected.setPosition(start);
        selected.setPosition(start + 7, QTextCursor::KeepAnchor);
        view->textEdit()->setTextCursor(selected);
        QSignalSpy lastWindow(qApp, &QGuiApplication::lastWindowClosed);
        auto *enter = window.findChild<QPushButton *>("monitorMode");
        QVERIFY(enter);
        enter->click();
        auto *monitor = window.monitorWindow();
        QVERIFY(monitor && monitor->isVisible());
        QVERIFY(window.isHidden());
        QVERIFY(monitor->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QVERIFY(monitor->windowFlags().testFlag(Qt::FramelessWindowHint));
        QCOMPARE(monitor->findChild<ReceiveView *>(), view);
        QCOMPARE(view->model(), model);
        QCOMPARE(view->textEdit()->document(), document);
        QCOMPARE(view->textEdit()->toPlainText(), initial);
        QCOMPARE(view->textEdit()->textCursor().selectedText(), QString("payload"));
        QCOMPARE(view->fontSize(), 11);
        QVERIFY(repeatTimer->isActive());
        auto *opacity = monitor->findChild<QSlider *>("monitorOpacity");
        auto *font = monitor->findChild<QSlider *>("monitorFont");
        QVERIFY(opacity && font);
        QCOMPARE(opacity->value(), 90);
        font->setValue(18);
        QCOMPARE(view->fontSize(), 18);
        sendWheel(view->textEdit()->viewport(), 120, {}, Qt::ControlModifier);
        QCOMPARE(font->value(), 19);
        QCOMPARE(Settings(path).intValue("monitor_font_size"), 19);
        QCOMPARE(Settings(path).arrayValue("receive_font_sizes"), QJsonArray({11, 12, 13}));
        opacity->setValue(45);
        QCOMPARE(monitor->backgroundOpacity(), 45);
        QCOMPARE(monitor->windowOpacity(), 1.0); // Text/buttons are never globally faded.
        view->appendFrame("payload while monitoring", now.addSecs(1));
        flush(*view);
        QVERIFY(view->textEdit()->toPlainText().contains("payload while monitoring"));
        QCOMPARE(view->textEdit()->textCursor().selectedText(), QString("payload"));
        monitor->findChild<QPushButton *>("monitorRestore")->click();
        QVERIFY(window.isVisible());
        QVERIFY(monitor->isHidden());
        QCOMPARE(window.findChild<ReceiveView *>(), view);
        QCOMPARE(view->model(), model);
        QCOMPARE(view->textEdit()->document(), document);
        QCOMPARE(view->fontSize(), 11);
        QCOMPARE(send->font().pointSize(), 14);
        QCOMPARE(send->toPlainText(), QString("unsent data must remain"));
        QCOMPARE(window.windowFlags().testFlag(Qt::WindowStaysOnTopHint), pinned);
        QCOMPARE(window.geometry(), normal);
        QCOMPARE(splitter->sizes(), sizes);
        QCOMPARE(Settings(path).boolValue("pin"), pinned);
        QCOMPARE(Settings(path).intValue("monitor_opacity"), 45);
        for (int i = 0; i < 8; ++i) {
            window.enterMonitorMode();
            QCOMPARE(window.monitorWindow(), monitor);
            QCOMPARE(view->fontSize(), 19);
            view->appendFrame("cycle " + QByteArray::number(i), now.addSecs(i + 2));
            flush(*view);
            if (i % 2) monitor->close(); // OS close / Alt+F4 is also Restore.
            else monitor->findChild<QPushButton *>("monitorClose")->click();
            QVERIFY(window.isVisible());
            QVERIFY(monitor->isHidden());
            QCOMPARE(view->fontSize(), 11);
        }
        QVERIFY(repeatTimer->isActive());
        QCOMPARE(lastWindow.size(), 0);
        QCOMPARE(view->model()->frameCount(), qsizetype{10});
        QVERIFY(view->textEdit()->toPlainText().endsWith("cycle 7"));
        QVERIFY(window.close());
    }

    void monitorCompactControlsSyncAndLayout()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("config.json");
        Settings config(path);
        config.setValue("mode", 0, false);
        config.setValue("autoscroll", false);
        MainWindow window(nullptr, path);
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
        auto *view = window.findChild<ReceiveView *>();
        auto *sidebar = window.findChild<SerialPanel *>();
        auto *normalAuto = sidebar->findChild<QCheckBox *>("autoScroll");
        auto *send = window.findChild<QPlainTextEdit *>("sendInput");
        QVERIFY(view && normalAuto && send);
        send->setPlainText("keep this draft");
        auto *repeat = window.findChild<SendPanel *>()->findChild<QTimer *>();
        QVERIFY(repeat);
        repeat->start(60000);
        view->appendFrame(QByteArray("line\n").repeated(120), QDateTime::currentDateTime());
        flush(*view);
        window.enterMonitorMode();
        auto *monitor = window.monitorWindow();
        auto *automatic = monitor->findChild<QToolButton *>("monitorAutoScroll");
        auto *clear = monitor->findChild<QToolButton *>("monitorClear");
        auto *port = monitor->findChild<QToolButton *>("monitorConnection");
        QVERIFY(automatic && clear && port);
        QVERIFY(!automatic->isChecked());
        QCOMPARE(monitor->size(), QSize(480, 320));
        QTest::qWait(120);
        QVERIFY(view->height() >= 200); // No extra toolbar row or taller window.
        auto *bar = view->textEdit()->verticalScrollBar();
        bar->setValue(0);
        automatic->click();
        QVERIFY(automatic->isChecked());
        QVERIFY(normalAuto->isChecked());
        QVERIFY(Settings(path).boolValue("autoscroll"));
        QCOMPARE(bar->value(), bar->maximum());
        QVERIFY(bar->maximum() > 0);
        automatic->click();
        QVERIFY(!normalAuto->isChecked());
        QVERIFY(!Settings(path).boolValue("autoscroll", true));
        bar->setValue(0);
        view->appendFrame("new data\n", QDateTime::currentDateTime());
        flush(*view);
        QTest::qWait(100);
        QCOMPARE(bar->value(), 0);
        QVERIFY(monitor->grab().save("artifacts/monitor-controls.png"));
        monitor->resize(360, 200);
        QTest::qWait(100);
        QCOMPARE(monitor->size(), QSize(360, 200));
        auto *titleBar = monitor->findChild<QWidget *>("monitorDragBar");
        const auto buttons = titleBar->findChildren<QAbstractButton *>(QString{}, Qt::FindDirectChildrenOnly);
        QCOMPARE(buttons.size(), 6); // Three new controls, position, restore, close.
        for (auto *control : buttons) {
            QVERIFY(control->isVisible());
            QVERIFY(titleBar->rect().contains(control->geometry()));
            for (auto *other : buttons)
                if (control != other) QVERIFY(!control->geometry().intersects(other->geometry()));
        }
        QVERIFY(view->height() >= 80);
        QVERIFY(monitor->grab().save("artifacts/monitor-controls-minimum.png"));
        // Clear also drops queued but not-yet-rendered data, and never erases
        // the sending draft or stops the active send timer.
        view->appendFrame("pending before clear", QDateTime::currentDateTime());
        clear->click();
        flush(*view);
        QCOMPARE(view->model()->frameCount(), qsizetype{0});
        QVERIFY(view->textEdit()->toPlainText().isEmpty());
        QCOMPARE(send->toPlainText(), QString("keep this draft"));
        QVERIFY(repeat->isActive());
        window.leaveMonitorMode();
        normalAuto->setChecked(true);
        QVERIFY(automatic->isChecked());
        window.enterMonitorMode();
        QVERIFY(automatic->isChecked());
        QSignalSpy toggles(sidebar, &SerialPanel::autoScrollChanged);
        for (int i = 0; i < 3; ++i) {
            automatic->click();
            window.leaveMonitorMode();
            window.enterMonitorMode();
            QCOMPARE(automatic->isChecked(), normalAuto->isChecked());
        }
        QCOMPARE(toggles.size(), 3); // No duplicate connections after restoring.
        window.leaveMonitorMode();
        QVERIFY(window.close());
    }

    void monitorConnectionFailureIsVisibleAndRetryable()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("config.json");
        MainWindow window(nullptr, path);
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
        auto *sidebar = window.findChild<SerialPanel *>();
        auto *selectedPort = sidebar->findChild<QComboBox *>("serialPort");
        QVERIFY(selectedPort);
        selectedPort->setCurrentText("");
        window.enterMonitorMode();
        auto *monitor = window.monitorWindow();
        auto *port = monitor->findChild<QToolButton *>("monitorConnection");
        auto *status = monitor->findChild<QLabel *>("monitorStatus");
        QVERIFY(port && status);
        port->click(); // Missing configuration must not open an invisible modal.
        QVERIFY(window.isHidden());
        QVERIFY(monitor->isVisible());
        QVERIFY(status->toolTip().contains("Select a serial port"));
        QVERIFY(!port->isChecked());
        // Deliberately nonexistent path, never a real COM port or test hardware.
        const QString missingPort = dir.filePath("nonexistent-serial-device");
        selectedPort->setCurrentText(missingPort);
        for (int attempt = 0; attempt < 2; ++attempt) {
            port->click();
            QVERIFY(status->toolTip().contains("Failed to open serial port"));
            QVERIFY(port->toolTip().contains("Failed to open serial port"));
            QVERIFY(!port->isChecked());
            QVERIFY(port->isEnabled());
            QCOMPARE(Settings(path).stringValue("port"), missingPort);
            QVERIFY(button(window, "Connect")->isEnabled());
            QTest::qWait(150); // Counter refresh must not hide the failure.
            QVERIFY(status->toolTip().contains("Failed to open serial port"));
        }
        window.leaveMonitorMode();
        QVERIFY(window.close());
    }

    void monitorConnectionButtonReflectsActualState()
    {
        QTemporaryDir dir;
        Settings config(dir.filePath("config.json"));
        MonitorWindow monitor(config);
        auto *port = monitor.findChild<QToolButton *>("monitorConnection");
        QSignalSpy requests(&monitor, &MonitorWindow::connectionToggleRequested);
        monitor.setConnectionStatus("Connected to test port", true);
        QVERIFY(port->isChecked());
        QVERIFY(port->toolTip().startsWith("Disconnect"));
        port->click();
        QCOMPARE(requests.size(), 1);
        QVERIFY(port->isChecked()); // A request is not proof of disconnection.
        monitor.setConnectionStatus("Disconnecting", true, true);
        QVERIFY(!port->isEnabled());
        port->click();
        QCOMPARE(requests.size(), 1);
        monitor.setConnectionStatus("Disconnected", false);
        QVERIFY(port->isEnabled());
        QVERIFY(!port->isChecked());
        port->click();
        QCOMPARE(requests.size(), 2);
        QVERIFY(!port->isChecked()); // Failed/open-pending is never shown green.
    }

    void monitorBackgroundAlphaAndOpaqueText()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("config.json");
        Settings config(path);
        config.setValue("mode", 0, false);
        config.setValue("font_size", 16, false);
        config.setValue("col_text", "#AA0055");
        MainWindow window(nullptr, path);
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
        auto *view = window.findChild<ReceiveView *>();
        view->appendFrame("MMMMM monitor payload\nOK", QDateTime::currentDateTime());
        flush(*view);
        window.enterMonitorMode();
        auto *monitor = window.monitorWindow();
        monitor->findChild<QSlider *>("monitorOpacity")->setValue(40);
        QTest::qWait(150);
        const QImage image = monitor->grab().toImage();
        QVERIFY(image.hasAlphaChannel());
        const qreal dpr = image.devicePixelRatio();
        const QPoint bodyPoint = view->textEdit()->viewport()->mapTo(monitor,
            QPoint(view->textEdit()->viewport()->width() - 30, view->textEdit()->viewport()->height() - 20));
        const QColor blank = image.pixelColor(qRound(bodyPoint.x() * dpr), qRound(bodyPoint.y() * dpr));
        QVERIFY2(blank.alpha() >= 98 && blank.alpha() <= 106,
            qPrintable(QStringLiteral("Expected only background alpha 102; got %1").arg(blank.alpha())));
        bool solidText = false;
        for (int y = 0; y < image.height() && !solidText; ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y) == QColor("#AA0055")) { solidText = true; break; }
        QVERIFY2(solidText, "Text must stay opaque when the background is transparent");
        QVERIFY(image.save("artifacts/monitor-transparent.png"));
        monitor->findChild<QSlider *>("monitorOpacity")->setValue(90);
        QVERIFY(monitor->grab().save("artifacts/monitor-mode.png"));
        window.leaveMonitorMode();
        QTest::qWait(100);
        const auto restored = view->textEdit()->viewport()->grab().toImage();
        QCOMPARE(restored.pixelColor(restored.width() - 25, restored.height() - 25), QColor(Qt::white));
        QVERIFY(window.close());
    }

    void monitorCornerGeometryAndScreenBounds()
    {
        const QList<QRect> screens{QRect(0, 0, 1920, 1040), QRect(-1920, -120, 1920, 1080),
            QRect(1920, 0, 1280, 680), QRect(0, 0, 300, 170)};
        for (const auto &screen : screens) {
            for (int i = 0; i < 4; ++i) {
                const auto corner = static_cast<Qt::Corner>(i);
                const auto dock = MonitorWindow::cornerGeometry(QSize(480, 320), screen, corner);
                QVERIFY(screen.contains(dock));
                if (screen.width() > 500)
                    QCOMPARE(MonitorWindow::nearestCorner(dock, screen), corner);
            }
            QVERIFY(screen.contains(MonitorWindow::constrainedGeometry(QRect(-100000, 100000, 9000, 8000), screen)));
        }
    }

    void monitorDraggingResizeAndPersistence()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("config.json");
        MainWindow window(nullptr, path);
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
        window.enterMonitorMode();
        auto *monitor = window.monitorWindow();
        const QRect area = monitor->screen()->availableGeometry();
        auto *bar = monitor->findChild<QWidget *>("monitorDragBar");
        QVERIFY(bar);
        QCOMPARE(monitor->geometry(), MonitorWindow::cornerGeometry(monitor->size(), area, Qt::BottomRightCorner));
        const QPoint local(20, 12);
        const QPoint desired = area.topLeft() + QPoint(60, 60);
        dragWidget(bar, local, bar->mapToGlobal(local) + desired - monitor->pos());
        QTRY_COMPARE_WITH_TIMEOUT(monitor->geometry(), MonitorWindow::cornerGeometry(monitor->size(), area, Qt::TopLeftCorner), 1000);
        QCOMPARE(Settings(path).intValue("monitor_corner"), int(Qt::TopLeftCorner));
        auto *position = monitor->findChild<QToolButton *>("monitorPosition");
        QVERIFY(position && position->menu());
        for (auto *action : position->menu()->actions())
            if (action->text() == "Free position") action->trigger();
        QVERIFY(!monitor->autoSnap());
        const QPoint freePoint = area.topLeft() + QPoint(100, 110);
        dragWidget(bar, local, bar->mapToGlobal(local) + freePoint - monitor->pos());
        QTRY_COMPARE_WITH_TIMEOUT(monitor->pos(), freePoint, 1000);
        const QSize before = monitor->size();
        const QPoint edge(monitor->width() - 3, monitor->height() - 3);
        dragWidget(monitor, edge, monitor->mapToGlobal(edge) + QPoint(50, 40));
        QTRY_COMPARE_WITH_TIMEOUT(monitor->size(), before + QSize(50, 40), 1000);
        monitor->findChild<QSlider *>("monitorOpacity")->setValue(65);
        monitor->findChild<QSlider *>("monitorFont")->setValue(15);
        const QRect saved = monitor->geometry();
        window.leaveMonitorMode();
        MainWindow reopened(nullptr, path);
        reopened.setAttribute(Qt::WA_DontShowOnScreen);
        reopened.show();
        QVERIFY(!reopened.monitorWindow()); // Startup remains the normal interface.
        reopened.enterMonitorMode();
        auto *restored = reopened.monitorWindow();
        QCOMPARE(restored->geometry(), saved);
        QCOMPARE(restored->backgroundOpacity(), 65);
        QCOMPARE(restored->findChild<QSlider *>("monitorFont")->value(), 15);
        QVERIFY(!restored->autoSnap());
        reopened.leaveMonitorMode();
        QVERIFY(reopened.close());
        QVERIFY(window.close());
    }

    void monitorMissingScreenAndOwnerShutdown()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("config.json");
        Settings config(path);
        config.setValue("monitor_screen", "a disconnected monitor", false);
        config.setValue("monitor_snap", false, false);
        config.setArray("monitor_geometry", {-50000, 50000, 8000, 8000});
        auto *window = new MainWindow(nullptr, path);
        window->setAttribute(Qt::WA_DontShowOnScreen);
        window->showMaximized();
        window->enterMonitorMode();
        QPointer<MonitorWindow> monitor = window->monitorWindow();
        QPointer<ReceiveView> view = monitor->findChild<ReceiveView *>();
        QVERIFY(monitor->screen()->availableGeometry().contains(monitor->geometry()));
        window->leaveMonitorMode();
        QVERIFY(window->isMaximized());
        window->enterMonitorMode();
        delete window; // Handles application-quit paths without a normal close event.
        QVERIFY(monitor.isNull());
        QVERIFY(view.isNull());
    }

    void pbBuiltInSchemaMatchesProtoFields()
    {
        QFile source(QStringLiteral(EASYSERIAL_SOURCE_DIR "/protocols/TaskBatch.proto"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        QString proto = QString::fromUtf8(source.readAll());
        proto.remove(QRegularExpression("//[^\\n]*"));
        const QRegularExpression messages("\\bmessage\\s+(\\w+)\\s*\\{");
        const QRegularExpression fields("\\b(repeated\\s+)?(\\w+)\\s+(\\w+)\\s*=\\s*(\\d+)\\s*;");
        const auto encodeVarint = [](quint64 value) {
            QByteArray data;
            do {
                const auto byte = quint8(value & 127);
                value >>= 7;
                data.append(char(byte | (value ? 128 : 0)));
            } while (value);
            return data;
        };
        QStringList types;
        auto iterator = messages.globalMatch(proto);
        while (iterator.hasNext()) {
            const auto definition = iterator.next();
            const QString type = definition.captured(1);
            types.append(type);
            int depth = 1;
            qsizetype end = definition.capturedEnd();
            while (end < proto.size() && depth) {
                if (proto[end] == '{') ++depth;
                if (proto[end] == '}') --depth;
                ++end;
            }
            QCOMPARE(depth, 0);
            auto members = fields.globalMatch(proto.sliced(definition.capturedEnd(), end - definition.capturedEnd() - 1));
            while (members.hasNext()) {
                const auto member = members.next();
                const QString kind = member.captured(2);
                QStringList words = member.captured(3).split('_');
                QString name = words.takeFirst();
                for (auto word : words) { word[0] = word[0].toUpper(); name += word; }
                const int number = member.captured(4).toInt();
                const bool message = TaskProtoDecoder::messageTypes().contains(kind);
                const int wire = message || kind == "bytes" ? 2 : 0;
                QByteArray encoded = encodeVarint((quint64(number) << 3) | quint64(wire));
                if (message) encoded += char(0);
                else if (kind == "bytes") encoded += QByteArray::fromHex("02dead");
                else encoded += encodeVarint(kind == "int64" ? quint64{9007199254740993} : quint64{0xffffffff});
                TaskProtoDecoder::Options options;
                options.framed = false;
                options.messageType = type;
                options.includeDefaults = false;
                const auto decoded = TaskProtoDecoder::decode(encoded, options);
                QVERIFY2(decoded.ok, qPrintable(decoded.error));
                QCOMPARE(decoded.knownFields, 1);
                QVERIFY2(decoded.message.contains(name), qPrintable(type + '.' + name));
                auto value = decoded.message[name];
                if (!member.captured(1).isEmpty()) {
                    QCOMPARE(value.toArray().size(), 1);
                    value = value.toArray().first();
                }
                if (message) QVERIFY(value.isObject());
                else if (kind == "bytes") QCOMPARE(value.toString(), QString("dead"));
                else if (kind == "int64") QCOMPARE(value.toString(), QString("9007199254740993"));
                else if (kind == "uint32") QCOMPARE(value.toInteger(), qint64{0xffffffff});
                else QCOMPARE(value.toInt(), -1);
            }
        }
        types.sort();
        QCOMPARE(types, TaskProtoDecoder::messageTypes());
    }

    void pbSampleMatchesProvidedProtocol()
    {
        QFile fixture(QStringLiteral(EASYSERIAL_SOURCE_DIR "/tests/fixtures/task-batch.hex"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto bytes = HexUtils::parse(QString::fromLatin1(fixture.readAll()));
        QVERIFY(bytes);
        QCOMPARE(bytes->size(), 307);
        const auto result = TaskProtoDecoder::decode(*bytes);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.payloadOffset, 24);
        QCOMPARE(TaskProtoDecoder::markerOffsets(*bytes), QList<qsizetype>{21});
        QCOMPARE(result.skippedPrefix.toHex(), QByteArray("484d5b1100b775bf0133000e0000430512341234000000ff"));
        const auto root = result.message;
        QCOMPARE(root["time"].toInteger(), qint64{1793842647});
        QCOMPARE(root["batchId"].toString(), QString("288230376151712044"));
        QCOMPARE(root["ap"].toInt(), 1);
        QCOMPARE(root["cp"].toInt(-1), 0);
        QCOMPARE(root["ts"].toArray().size(), 1);
        const auto task = root["ts"].toArray().first().toObject();
        QCOMPARE(task["taskId"].toString(), QString("288230376151712054"));
        QCOMPARE(task["type"].toInt(), 2);
        QCOMPARE(task["label"].toInt(), 1);
        QCOMPARE(task["action"].toInt(), 1212);
        QCOMPARE(task["sts"].toInt(-1), 0);
        QCOMPARE(task["data"].toString(), QString::fromLatin1(bytes->sliced(88).toHex()));
        QCOMPARE(task["data"].toString().size(), 219 * 2);
        QVERIFY(!task.contains("onceList"));
        const auto cron = task["cronList"].toObject()["cron"].toArray();
        QCOMPARE(cron.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(cron[i].toObject()["cron"].toInt(), 2304 + 256 * i);
            QCOMPARE(cron[i].toObject()["times"].toInt(), 3);
        }
        QVERIFY(result.warnings.isEmpty());
        QVERIFY(!root.contains("extData")); // Header bytes are not schema fields.
        TaskProtoDecoder::Options options;
        options.includeDefaults = false;
        const auto sparse = TaskProtoDecoder::decode(*bytes, options);
        QVERIFY(sparse.ok);
        QVERIFY(!sparse.message.contains("cp"));
        QVERIFY(!sparse.message["ts"].toArray().first().toObject().contains("sts"));
        options.includeDefaults = true;
        options.framed = false;
        QCOMPARE(TaskProtoDecoder::decode(bytes->sliced(24), options).message, root);
        QFile output("artifacts/pb-sample.json");
        QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }

    void pbScalarAndOneofSemantics()
    {
        TaskProtoDecoder::Options options;
        options.framed = false;
        options.messageType = "CronSchedule";
        auto result = TaskProtoDecoder::decode(QByteArray::fromHex("08801210ffffffffffffffffff01"), options);
        QVERIFY(result.ok);
        QCOMPARE(result.message["cron"].toInt(), 2304);
        QCOMPARE(result.message["times"].toInt(), -1);
        options.messageType = "TaskBatchReqDTO";
        result = TaskProtoDecoder::decode(QByteArray::fromHex("08ffffffffffffffff7f10ffffffff0f18ffffffffffffffffff01"), options);
        QVERIFY(result.ok);
        QCOMPARE(result.message["batchId"].toString(), QString("9223372036854775807"));
        QCOMPARE(result.message["time"].toInteger(), qint64{4294967295});
        QCOMPARE(result.message["errCode"].toInt(), -1);
        options.messageType = "OnceSchedule";
        result = TaskProtoDecoder::decode(QByteArray::fromHex("088080808080808080800110ffffffffffffffffff01"), options);
        QVERIFY(result.ok);
        QCOMPARE(result.message["st"].toString(), QString("-9223372036854775808"));
        QCOMPARE(result.message["et"].toString(), QString("-1"));
        options.messageType = "TaskResMO";
        // Duplicate singular messages merge, repeated entries append, and
        // replacing a oneof discards all state from its previous member.
        result = TaskProtoDecoder::decode(QByteArray::fromHex("2a040a0208012a040a021002"), options);
        QVERIFY(result.ok);
        QCOMPARE(result.message["cronList"].toObject()["cron"].toArray().size(), 2);
        result = TaskProtoDecoder::decode(QByteArray::fromHex("2a040a02080132060a0408031004"), options);
        QVERIFY(result.ok);
        QVERIFY(!result.message.contains("cronList"));
        const auto once = result.message["onceList"].toObject()["items"].toArray().first().toObject();
        QCOMPARE(once["st"].toString(), QString("3"));
        QCOMPARE(once["et"].toString(), QString("4"));
        options.messageType = "ResultDetailMO";
        result = TaskProtoDecoder::decode(QByteArray::fromHex("0a0208070a021008"), options);
        QVERIFY(result.ok);
        QCOMPARE(result.message["operating"].toObject()["sn"].toString(), QString("7"));
        QCOMPARE(result.message["operating"].toObject()["rate"].toInt(), 8);
        result = TaskProtoDecoder::decode(QByteArray::fromHex("0a020807120210091a00"), options);
        QVERIFY(result.ok);
        QVERIFY(!result.message.contains("operating"));
        QCOMPARE(result.message["status"].toObject()["code"].toInt(), 9);
        QVERIFY(result.message.contains("_unknownFields"));
        options.messageType = "TaskBatchResDTO";
        result = TaskProtoDecoder::decode(QByteArray::fromHex("08010802"), options);
        QVERIFY(result.ok);
        QCOMPARE(result.message["time"].toInt(), 2);
        QCOMPARE(TaskProtoDecoder::messageTypes().size(), 12);
        for (const auto &type : TaskProtoDecoder::messageTypes()) {
            options.messageType = type;
            QVERIFY(TaskProtoDecoder::decode({}, options).ok);
        }
    }

    void pbInvalidDataAndResourceLimits()
    {
        TaskProtoDecoder::Options options;
        QVERIFY(!TaskProtoDecoder::decode(QByteArray::fromHex("0801"), options).ok);
        QVERIFY(!TaskProtoDecoder::decode(QByteArray::fromHex("0000ff"), options).ok);
        const auto ambiguous = QByteArray::fromHex("0000ff0000ff0801");
        QVERIFY(!TaskProtoDecoder::decode(ambiguous, options).ok);
        options.markerOffset = 3;
        auto result = TaskProtoDecoder::decode(ambiguous, options);
        QVERIFY(result.ok);
        QCOMPARE(result.payloadOffset, 6);
        options.markerOffset = 1;
        QVERIFY(!TaskProtoDecoder::decode(ambiguous, options).ok);
        options.framed = false;
        for (const auto &hex : {"00", "08", "0880", "08ffffffffffffffffffff02", "0e", "0a00",
                "2affffffff0f", "2a030801", "2a020a00", "f90100", "fd0100", "9b069406", "9b06", "9c06"}) {
            result = TaskProtoDecoder::decode(QByteArray::fromHex(hex), options);
            QVERIFY2(!result.ok, hex);
            QVERIFY(result.message.isEmpty()); // No partially successful JSON.
            QVERIFY(result.error.contains("byte"));
        }
        const QByteArray unknown = QByteArray::fromHex("980607990600000000000000009a06036162639d06000000009b0608019c06");
        result = TaskProtoDecoder::decode(unknown, options);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.message["_unknownFields"].toArray().size(), 5);
        QVERIFY(!result.warnings.isEmpty());
        QVERIFY(!TaskProtoDecoder::decode(QByteArray(TaskProtoDecoder::MaximumBytes + 1, 'A'), options).ok);
        QVERIFY(TaskProtoDecoder::decode(QByteArray::fromHex("0801").repeated(8193), options).error.contains("8192"));
        const QByteArray deep = QByteArray::fromHex("9b06").repeated(35) + QByteArray::fromHex("9c06").repeated(35);
        QVERIFY(TaskProtoDecoder::decode(deep, options).error.contains("32 levels"));
        options.messageType = "NotInThisSchema";
        QVERIFY(!TaskProtoDecoder::decode({}, options).ok);
        // Deterministic malformed-input smoke coverage, no network or devices.
        options.messageType = "TaskBatchResDTO";
        quint32 state = 17;
        for (int i = 0; i < 1000; ++i) {
            QByteArray bytes;
            for (int j = 0; j < i % 100; ++j) {
                state = state * 1664525U + 1013904223U;
                bytes.append(char(state >> 24));
            }
            const auto fuzzed = TaskProtoDecoder::decode(bytes, options);
            QVERIFY(fuzzed.ok || (!fuzzed.error.isEmpty() && fuzzed.message.isEmpty()));
        }
    }

    void pbToolInputOutputAndStaleResult()
    {
        QTemporaryDir dir;
        Settings config(dir.filePath("config.json"));
        ToolsDialog tools(config);
        tools.setAttribute(Qt::WA_DontShowOnScreen);
        tools.resize(1040, 720);
        auto *tabs = tools.findChild<QTabWidget *>();
        QCOMPARE(tabs->count(), 6);
        tabs->setCurrentIndex(5);
        tools.show();
        auto *input = tools.findChild<QPlainTextEdit *>("pbInput");
        auto *output = tools.findChild<QPlainTextEdit *>("pbOutput");
        auto *live = tools.findChild<QCheckBox *>("pbAutoParse");
        auto *copy = tools.findChild<QPushButton *>("pbCopy");
        auto *status = tools.findChild<QLabel *>("pbStatus");
        QVERIFY(input && output && live && copy && status);
        QVERIFY(output->isReadOnly());
        QFile fixture(QStringLiteral(EASYSERIAL_SOURCE_DIR "/tests/fixtures/task-batch.hex"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        input->setPlainText(QString::fromLatin1(fixture.readAll()));
        QTRY_VERIFY_WITH_TIMEOUT(output->toPlainText().contains("288230376151712044"), 1500);
        QVERIFY(copy->isEnabled());
        QVERIFY(status->text().contains("byte 24"));
        QVERIFY(tools.findChild<QLineEdit *>("pbSkippedPrefix")->text().startsWith("484d"));
        const QString savedClipboard = QApplication::clipboard()->text();
        copy->click();
        QCOMPARE(QApplication::clipboard()->text(), output->toPlainText());
        QApplication::clipboard()->setText(savedClipboard);
        QTest::qWait(100);
        QVERIFY(tools.grab().save("artifacts/pb-decoder.png"));
        live->setChecked(false);
        input->setPlainText("invalid");
        QVERIFY(output->toPlainText().isEmpty());
        QVERIFY(!copy->isEnabled());
        tools.findChild<QPushButton *>("pbParse")->click();
        QVERIFY(status->text().contains("Invalid HEX"));
        input->setPlainText("0x08 0x01");
        tools.findChild<QComboBox *>("pbStartMode")->setCurrentIndex(2);
        tools.findChild<QPushButton *>("pbParse")->click();
        QCOMPARE(QJsonDocument::fromJson(output->toPlainText().toUtf8()).object()["time"].toInt(), 1);
        input->setPlainText("0000ff0000ff0801");
        tools.findChild<QComboBox *>("pbStartMode")->setCurrentIndex(0);
        tools.findChild<QPushButton *>("pbParse")->click();
        QVERIFY(status->text().contains("Multiple"));
        QVERIFY(output->toPlainText().isEmpty());
        tools.findChild<QComboBox *>("pbStartMode")->setCurrentIndex(1);
        tools.findChild<QSpinBox *>("pbMarkerOffset")->setValue(3);
        tools.findChild<QPushButton *>("pbParse")->click();
        QVERIFY(copy->isEnabled());
        tools.findChild<QPushButton *>("pbClear")->click();
        QVERIFY(input->toPlainText().isEmpty());
        QVERIFY(output->toPlainText().isEmpty());
        QVERIFY(!copy->isEnabled());
        live->setChecked(true);
        input->setPlainText("0000ff0801"); // Closing with a pending debounce is safe.
        QVERIFY(tools.close());
    }

    void pbImportedSchemaMatchesExistingSample()
    {
        ProtoSchema schema;
        QString error;
        QVERIFY2(schema.load(":/protocols/TaskBatch.proto", {}, &error), qPrintable(error));
        QCOMPARE(schema.messageTypes(), TaskProtoDecoder::messageTypes());
        QFile file(QStringLiteral(EASYSERIAL_SOURCE_DIR "/tests/fixtures/task-batch.hex"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto data = HexUtils::parse(QString::fromLatin1(file.readAll()));
        QVERIFY(data);
        const auto result = schema.decode(*data, {});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.message, TaskProtoDecoder::decode(*data).message);
        QCOMPARE(result.payloadOffset, 24);
        QVERIFY(!schema.load("missing-schema.proto", {}, &error));
        QVERIFY(schema.decode(*data, {}).ok); // Core reload is transactional.
    }

    void pbImportedScalarMapsNestedAndOneof()
    {
        ProtoSchema schema;
        QString error;
        QVERIFY2(schema.load(QStringLiteral(EASYSERIAL_SOURCE_DIR "/tests/fixtures/proto/modern.proto"), {}, &error), qPrintable(error));
        QVERIFY(schema.messageTypes().contains("demo.Packet.Inner"));
        QVERIFY(schema.messageTypes().contains("demo.common.Detail"));
        QVERIFY(!schema.messageTypes().contains("demo.Packet.ReadingsEntry"));
        TaskProtoDecoder::Options options;
        options.framed = false;
        options.messageType = "demo.Packet";
        const auto bytes = QByteArray::fromHex(
            "0801"                         // sint32 -1
            "10ffffffffffffffffff01"       // sint64 min
            "18ffffffffffffffffff01"       // uint64 max
            "25ffffffff"                   // fixed32 max
            "2dfeffffff"                   // sfixed32 -2
            "31ffffffffffffffff"           // fixed64 max
            "39ffffffffffffffff"           // sfixed64 -1
            "450000c03f"                   // float 1.5
            "490000000000000440"           // double 2.5
            "52026869"                     // string hi
            "5801"                         // bool
            "6001"                         // enum READY
            "6a03010203"                   // packed sint32
            "72050a01611007"               // map a:7
            "7a030a0178"                   // Detail name:x
            "82010161880109"               // oneof text then code
            "92010200ff"                   // bytes
            "9a01020805"                   // Timestamp seconds:5
            "a00100"                       // optional zero is present
            "aa01020806");                 // nested Inner
        auto result = schema.decode(bytes, options);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto json = result.message;
        QCOMPARE(json["delta"].toInt(), -1);
        QCOMPARE(json["signed64"].toString(), QString("-9223372036854775808"));
        QCOMPARE(json["unsigned64"].toString(), QString("18446744073709551615"));
        QCOMPARE(json["count"].toDouble(), 4294967295.0);
        QCOMPARE(json["minus"].toInt(), -2);
        QCOMPARE(json["big"].toString(), QString("18446744073709551615"));
        QCOMPARE(json["negative"].toString(), QString("-1"));
        QCOMPARE(json["ratio"].toDouble(), 1.5);
        QCOMPARE(json["precise"].toDouble(), 2.5);
        QCOMPARE(json["customTitle"].toString(), QString("hi"));
        QCOMPARE(json["state"].toInt(), 1);
        QVERIFY(json["enabled"].toBool());
        QCOMPARE(json["samples"].toArray(), (QJsonArray{-1, 1, -2}));
        QCOMPARE(json["readings"].toObject()["a"].toString(), QString("7"));
        QCOMPARE(json["detail"].toObject()["name"].toString(), QString("x"));
        QVERIFY(!json.contains("text"));
        QCOMPARE(json["code"].toInt(), 9);
        QCOMPARE(json["raw"].toString(), QString("00ff"));
        QCOMPARE(json["created"].toObject()["seconds"].toString(), QString("5"));
        QVERIFY(json.contains("maybe"));
        QCOMPARE(json["inner"].toObject()["value"].toInt(), 6);
        result = schema.decode(QByteArray::fromHex("68016802"), options); // Unpacked accepted too.
        QCOMPARE(result.message["samples"].toArray(), (QJsonArray{-1, 1}));
        result = schema.decode(QByteArray::fromHex("458000807f49000000000000f07f6063"), options);
        QVERIFY(result.ok);
        QCOMPARE(result.message["ratio"].toString(), QString("NaN"));
        QCOMPARE(result.message["precise"].toString(), QString("Infinity"));
        QCOMPARE(result.message["state"].toInt(), 99);
        options.includeDefaults = false;
        QVERIFY(schema.decode({}, options).message.isEmpty());
        options.includeDefaults = true;
        QVERIFY(!schema.decode({}, options).message.contains("maybe"));
        QVERIFY(!schema.decode(QByteArray::fromHex("0a05ff"), options).ok);
        QVERIFY(!schema.decode(QByteArray(TaskProtoDecoder::MaximumBytes + 1, 'a'), options).ok);
        QVERIFY(schema.decode(QByteArray::fromHex("6801").repeated(8193), options).error.contains("8192"));
        options.messageType = "demo.Node";
        QByteArray deep = QByteArray::fromHex("1001");
        for (int i = 0; i < 34; ++i) deep = QByteArray(1, '\x0a') + QByteArray(1, char(deep.size())) + deep;
        QVERIFY(!schema.decode(deep, options).ok);
        options.messageType = "demo.Packet";
        result = schema.decode(QByteArray::fromHex("f80701"), options);
        QVERIFY(result.ok);
        QVERIFY(!result.warnings.isEmpty());
        QCOMPARE(result.message["_unknownFields"].toArray()[0].toObject()["fieldNumber"].toInt(), 127);
    }

    void pbProto2RequiredDefaultsAndExtensions()
    {
        ProtoSchema schema;
        QString error;
        QVERIFY2(schema.load(QStringLiteral(EASYSERIAL_SOURCE_DIR "/tests/fixtures/proto/legacy.proto"), {}, &error), qPrintable(error));
        TaskProtoDecoder::Options options;
        options.framed = false;
        options.messageType = "legacy.Packet";
        auto result = schema.decode({}, options);
        QVERIFY(result.ok);
        QVERIFY(result.warnings.join('\n').contains("required"));
        QCOMPARE(result.message["name"].toString(), QString("old device"));
        result = schema.decode(QByteArray::fromHex("0801a2060178"), options);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.message["[legacy.note]"].toString(), QString("x"));
        QVERIFY(result.warnings.isEmpty());
    }

    void pbImportDependenciesErrorsAndUnicode()
    {
        QTemporaryDir dir;
        const QString base = dir.filePath(QString::fromUtf8("协议"));
        QVERIFY(QDir().mkpath(base + "/nested"));
        const auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        const QString path = base + "/nested/packet.proto";
        QVERIFY(write(path, "syntax=\"proto3\"; import \"common.proto\"; message Packet { Common c=1; }"));
        ProtoSchema schema;
        QString error;
        QVERIFY(!schema.load(path, {}, &error));
        QVERIFY(error.contains("common.proto"));
        QVERIFY(write(base + "/common.proto", "syntax=\"proto3\"; message Common { string value=1; }"));
        QVERIFY2(schema.load(path, {base}, &error), qPrintable(error));
        TaskProtoDecoder::Options options;
        options.framed = false;
        options.messageType = "Packet";
        auto result = schema.decode(QByteArray::fromHex("0a030a0178"), options);
        QVERIFY(result.ok);
        QCOMPARE(result.message["c"].toObject()["value"].toString(), QString("x"));
        QVERIFY(write(path, "syntax=\"proto3\";\nmessage Broken { string name = ; }"));
        QVERIFY(!schema.load(path, {base}, &error));
        QVERIFY(error.contains("packet.proto:2:"));
        QVERIFY(schema.decode(QByteArray::fromHex("0a030a0178"), options).ok);
        QVERIFY(write(path, "syntax=\"proto3\"; import \"../common.proto\"; message Packet {}"));
        QVERIFY(!schema.load(path, {base}, &error));
        QVERIFY(write(path, QByteArray(2 * 1024 * 1024 + 1, ' ')));
        QVERIFY(!schema.load(path, {base}, &error));
        QVERIFY(error.contains("2 MiB"));
    }

    void pbImportedCatalogSwitchReloadAndPersistence()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("config.json"));
        QVERIFY(QDir().mkpath(dir.filePath("v1")));
        QVERIFY(QDir().mkpath(dir.filePath("v2")));
        const QString first = dir.filePath("v1/packet.proto"), second = dir.filePath("v2/packet.proto");
        const auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(first, "syntax=\"proto3\"; package test; message Packet { int32 old_value=1; } message Extra { int32 x=1; }"));
        QVERIFY(write(second, "syntax=\"proto3\"; package test; message Packet { int32 new_value=1; }"));
        {
            PbDecoderWidget page(settings);
            page.setAttribute(Qt::WA_DontShowOnScreen);
            page.resize(1040, 700);
            page.show();
            page.importFiles({first, second});
            auto *files = page.findChild<QComboBox *>("pbProtocolFile");
            auto *type = page.findChild<QComboBox *>("pbMessageType");
            auto *input = page.findChild<QPlainTextEdit *>("pbInput");
            auto *output = page.findChild<QPlainTextEdit *>("pbOutput");
            auto *run = page.findChild<QPushButton *>("pbParse");
            QCOMPARE(files->count(), 3);
            QCOMPARE(files->currentData().toString(), second);
            page.findChild<QComboBox *>("pbStartMode")->setCurrentIndex(2);
            input->setPlainText("0807");
            run->click();
            QVERIFY(output->toPlainText().contains("newValue"));
            files->setCurrentIndex(1);
            type->setCurrentText("test.Packet");
            QVERIFY(output->toPlainText().isEmpty());
            run->click();
            QVERIFY(output->toPlainText().contains("oldValue"));
            files->setCurrentIndex(2);
            files->setCurrentIndex(1);
            QCOMPARE(type->currentText(), QString("test.Packet"));
            QVERIFY(write(first, "syntax=\"proto3\"; package test; message Packet { int32 changed_value=1; }"));
            page.findChild<QPushButton *>("pbReloadProto")->click();
            run->click();
            QVERIFY(output->toPlainText().contains("changedValue"));
            page.importFiles({first});
            QCOMPARE(files->count(), 3);
            run->click();
            QVERIFY(output->toPlainText().contains("changedValue"));
            QVERIFY(page.grab().save("artifacts/pb-imported-proto.png"));
            QVERIFY(write(first, "syntax=\"proto3\"; message Broken {"));
            page.findChild<QPushButton *>("pbReloadProto")->click();
            run->click();
            QVERIFY(output->toPlainText().isEmpty());
            QVERIFY(!page.findChild<QPushButton *>("pbCopy")->isEnabled());
            QVERIFY(page.findChild<QLabel *>("pbSchemaStatus")->text().contains("Schema error"));
            files->setCurrentIndex(2);
            run->click();
            QVERIFY(output->toPlainText().contains("newValue"));
            page.setIncludeFolders({dir.path()});
        }
        Settings restored(dir.filePath("config.json"));
        PbDecoderWidget page(restored);
        auto *files = page.findChild<QComboBox *>("pbProtocolFile");
        QCOMPARE(files->count(), 3);
        QCOMPARE(files->currentData().toString(), second);
        QCOMPARE(page.findChild<QComboBox *>("pbMessageType")->currentText(), QString("test.Packet"));
        QCOMPARE(restored.arrayValue("pb_include_folders").size(), 1);
        QVERIFY(page.findChild<QPlainTextEdit *>("pbInput")->toPlainText().isEmpty()); // Packets are not persisted.
        page.findChild<QPushButton *>("pbRemoveProto")->click();
        QCOMPARE(files->count(), 2);
        QVERIFY(QFileInfo::exists(second));
        files->setCurrentIndex(0);
        QVERIFY(!page.findChild<QPushButton *>("pbRemoveProto")->isEnabled());
    }

    void toolsAndLightWindowScreenshot()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath("tools.json"));
        ToolsDialog tools(settings);
        tools.setAttribute(Qt::WA_DontShowOnScreen);
        auto *tabs = tools.findChild<QTabWidget *>();
        QCOMPARE(tabs->count(), 6);
        auto editors = tabs->widget(0)->findChildren<QPlainTextEdit *>();
        QCOMPARE(editors.size(), 2);
        editors[0]->setPlainText("A\\r\\n\\x01");
        auto *toHex = button(tools, QStringLiteral("ASCII → HEX"));
        auto *toAscii = button(tools, QStringLiteral("HEX → ASCII"));
        QVERIFY(toHex && toAscii);
        toHex->click();
        QCOMPARE(editors[1]->toPlainText(), QString("41 0D 0A 01"));
        toAscii->click();
        QCOMPARE(editors[0]->toPlainText(), QString("A\\r\\n\\x01"));
        tabs->widget(1)->findChild<QLineEdit *>()->setText("01 03 00 00 00 0A");
        button(tools, QStringLiteral("Calculate CRC"))->click();
        QVERIFY(tabs->widget(1)->findChild<QPlainTextEdit *>()->toPlainText().contains("C5 CD"));
        tabs->widget(2)->findChild<QPlainTextEdit *>()->setPlainText("abc");
        button(tools, QStringLiteral("Calculate SHA256"))->click();
        QCOMPARE(tabs->widget(2)->findChild<QLineEdit *>()->text(),
            QString("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

        MainWindow window(nullptr, dir.filePath("window.json"));
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.resize(1440, 960);
        window.show();
        auto *view = window.findChild<ReceiveView *>();
        QVERIFY(view);
        view->appendFrame("STATUS OK\r\n", QDateTime::currentDateTime());
        view->appendTransmit(QByteArray::fromHex("01030000000AC5CD"), QDateTime::currentDateTime());
        flush(*view);
        QTest::qWait(200);
        QVERIFY(window.grab().save("artifacts/main-light.png"));
        window.resize(1280, 800);
        QTest::qWait(150);
        QVERIFY(window.grab().save("artifacts/main-light-1280.png"));
        view->clearAll();
        for (auto *combo : window.findChild<SerialPanel *>()->findChildren<QComboBox *>()) {
            const int ascii = combo->findText(QStringLiteral("ASCII Text"));
            if (ascii >= 0) combo->setCurrentIndex(ascii);
        }
        view->appendFrame("Range: bytes=230048-231311\r\nUser-Agent: 290c13457865/3016/1\r\nConnection: keep-alive\r\n\r\nOK\r\n", QDateTime::currentDateTime());
        view->appendTransmit("AT+CIPSEND=4,172\r\n", QDateTime::currentDateTime().addMSecs(150));
        flush(*view);
        QTest::qWait(150);
        QVERIFY(window.grab().save("artifacts/main-english-packets.png"));
        tools.resize(800, 620);
        tools.show();
        QTest::qWait(50);
        QVERIFY(tools.grab().save("artifacts/tools-light.png"));
        QVERIFY(window.close());
    }
};

QTEST_MAIN(RegressionTests)
#include "RegressionTests.moc"
