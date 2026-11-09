#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

struct AutoReplyResult
{
    bool matched = false;
    QByteArray payload;
    QString ruleName;
    QString error;
};

namespace AutoReplyEngine {
QString validate(const QJsonObject &rule);
AutoReplyResult find(const QByteArray &data, const QJsonArray &rules,
                     const QString &receiveFormat = {});
}
