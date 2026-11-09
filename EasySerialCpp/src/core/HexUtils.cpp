#include "core/HexUtils.h"

#include <QRegularExpression>

std::optional<QByteArray> HexUtils::parse(const QString &text)
{
    QString value = text;
    value.replace(QRegularExpression(QStringLiteral("0[xX]")), QStringLiteral(" "));
    value.remove(QRegularExpression(QStringLiteral(R"([\s,;:\-]+)")));
    if (value.isEmpty())
        return QByteArray{};
    if ((value.size() % 2) != 0 ||
        !QRegularExpression(QStringLiteral("^[0-9A-Fa-f]+$")).match(value).hasMatch())
        return std::nullopt;
    return QByteArray::fromHex(value.toLatin1());
}

QString HexUtils::spaced(const QByteArray &data)
{
    return spaced(data, 0, data.size());
}

QString HexUtils::spaced(const QByteArray &data, qsizetype offset, qsizetype length)
{
    const QByteArray slice = data.sliced(offset, length);
    return QString::fromLatin1(slice.toHex(' ').toUpper());
}

QByteArray HexUtils::lineEnding(int index)
{
    switch (index) {
    case 1: return QByteArrayLiteral("\r\n");
    case 2: return QByteArrayLiteral("\r");
    case 3: return QByteArrayLiteral("\n");
    default: return {};
    }
}

