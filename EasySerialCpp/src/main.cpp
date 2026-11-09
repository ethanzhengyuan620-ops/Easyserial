#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QFile>
#include <QIcon>
#include <QLocale>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

#include "Theme.h"
#include "ui/MainWindow.h"
#include "ui/MonitorWindow.h"
#include "core/ProtoSchema.h"

int main(int argc, char *argv[])
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    // Keep application-owned file/color dialogs in English on every Windows locale.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("EasySerial"));
    QCoreApplication::setApplicationName(QStringLiteral("EasySerialCpp"));
    // Retain the internal ID so existing configuration is not lost after renaming.
    QGuiApplication::setApplicationDisplayName(QStringLiteral("EasySerial"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    Theme::apply(app);

    // A deployment check must not use saved ports, pin state or user settings.
    const bool smokeTest = app.arguments().contains(QStringLiteral("--smoke-test"));
    if (smokeTest && QApplication::windowIcon().pixmap(32, 32).isNull()) return 3;
    std::unique_ptr<QTemporaryDir> temporarySettings;
    if (smokeTest) {
        temporarySettings = std::make_unique<QTemporaryDir>();
        if (!temporarySettings->isValid()) return 2;
        // Verify the shipped parser and embedded standard imports, without
        // depending on source-tree files or an installed protoc executable.
        ProtoSchema schema;
        QString error;
        if (!schema.load(":/protocols/TaskBatch.proto", {}, &error)) return 6;
        const auto sample = schema.decode(QByteArray::fromHex("0000ff0801"), {});
        if (!sample.ok || sample.message["time"].toInt() != 1) return 7;
        QFile protoFile(temporarySettings->filePath("probe.proto"));
        if (!protoFile.open(QIODevice::WriteOnly)) return 8;
        const QByteArray protoData = "syntax=\"proto3\"; package probe; import \"google/protobuf/timestamp.proto\"; message Packet { google.protobuf.Timestamp time=1; }";
        if (protoFile.write(protoData) != protoData.size()) return 8;
        protoFile.close();
        if (!schema.load(protoFile.fileName(), {}, &error)) return 9;
        TaskProtoDecoder::Options options;
        options.messageType = "probe.Packet";
        options.framed = false;
        const auto imported = schema.decode(QByteArray::fromHex("0a020801"), options);
        if (!imported.ok || imported.message["time"].toObject()["seconds"].toString() != "1") return 10;
    }
    MainWindow window(nullptr, smokeTest ? temporarySettings->filePath(QStringLiteral("config.json")) : QString{});
    if (smokeTest) window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    if (smokeTest) {
        // Exercise the deployed floating-window code too, without showing any
        // windows, opening a serial port or changing the real configuration.
        QTimer::singleShot(100, &window, [&] {
            window.enterMonitorMode();
            if (window.isVisible() || !window.monitorWindow() || !window.monitorWindow()->isVisible()) app.exit(4);
        });
        QTimer::singleShot(200, &window, [&] {
            window.leaveMonitorMode();
            if (!window.isVisible() || (window.monitorWindow() && window.monitorWindow()->isVisible())) app.exit(5);
        });
        QTimer::singleShot(300, &window, [&] { app.exit(window.close() ? 0 : 1); });
    }
    return app.exec();
}
