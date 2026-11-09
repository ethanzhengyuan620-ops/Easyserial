#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>

// Schema-specific decoder for protocols/TaskBatch.proto, not a general .proto
// compiler. Diagnostic JSON uses lowerCamelCase, decimal strings for int64,
// and HEX (not standard ProtoJSON base64) for bytes.
namespace TaskProtoDecoder {
constexpr qsizetype MaximumBytes = 256 * 1024;
constexpr qsizetype MaximumInputCharacters = 1024 * 1024;
struct Options {
    QString messageType = QStringLiteral("TaskBatchResDTO");
    bool framed = true;
    qsizetype markerOffset = -1; // Auto requires exactly one 00 00 FF marker.
    bool includeDefaults = true;
};
struct Result {
    bool ok = false;
    QJsonObject message;
    QString error;
    QStringList warnings;
    qsizetype payloadOffset = 0;
    QByteArray skippedPrefix;
    int knownFields = 0;
};
QStringList messageTypes();
QList<qsizetype> markerOffsets(const QByteArray &data);
Result decode(const QByteArray &data, const Options &options = {});
}
