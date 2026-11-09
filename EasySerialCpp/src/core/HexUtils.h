#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

namespace HexUtils {
std::optional<QByteArray> parse(const QString &text);
QString spaced(const QByteArray &data);
QString spaced(const QByteArray &data, qsizetype offset, qsizetype length);
QByteArray lineEnding(int index);
}

