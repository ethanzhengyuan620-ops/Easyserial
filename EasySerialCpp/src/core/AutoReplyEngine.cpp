#include "core/AutoReplyEngine.h"

#include <QJsonObject>
#include <QRegularExpression>

#include "core/HexUtils.h"

namespace {
QString asciiSource(const QByteArray &data)
{
    QString result = QString::fromUtf8(data);
    while (result.endsWith('\r') || result.endsWith('\n'))
        result.chop(1);
    return result;
}

bool matches(const QByteArray &data, const QJsonObject &rule)
{
    const QString format = rule.value("rx_format").toString("ASCII").toUpper();
    const QString mode = rule.value("match").toString("exact");
    QString pattern = rule.value("pattern").toString();

    if (format == "HEX" && mode != "regex") {
        const auto expected = HexUtils::parse(pattern);
        if (!expected)
            return false;
        return mode == "exact" ? data == *expected : data.contains(*expected);
    }

    QString source = format == "ASCII" ? asciiSource(data) : HexUtils::spaced(data);
    const bool caseSensitive = rule.value("case_sensitive").toBool(true);
    if (mode == "regex") {
        QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
        if (!caseSensitive)
            options |= QRegularExpression::CaseInsensitiveOption;
        return QRegularExpression(pattern, options).match(source).hasMatch();
    }
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    return mode == "exact" ? source.compare(pattern, cs) == 0 : source.contains(pattern, cs);
}

QByteArray payload(const QJsonObject &rule)
{
    const QString reply = rule.value("reply").toString();
    if (rule.value("tx_format").toString("ASCII").toUpper() == "HEX")
        return HexUtils::parse(reply).value_or(QByteArray{});
    return reply.toUtf8() + HexUtils::lineEnding(rule.value("line_ending").toInt(0));
}
}

QString AutoReplyEngine::validate(const QJsonObject &rule)
{
    const QString rx = rule.value("rx_format").toString("ASCII").toUpper();
    const QString tx = rule.value("tx_format").toString("ASCII").toUpper();
    const QString mode = rule.value("match").toString("exact");
    const QString pattern = rule.value("pattern").toString();
    const QString reply = rule.value("reply").toString();
    if (rx != "ASCII" && rx != "HEX") return QStringLiteral("Receive format must be ASCII or HEX");
    if (tx != "ASCII" && tx != "HEX") return QStringLiteral("Reply format must be ASCII or HEX");
    if (mode != "exact" && mode != "contains" && mode != "regex")
        return QStringLiteral("Unsupported match mode");
    if (pattern.isEmpty()) return QStringLiteral("Receive pattern cannot be empty");
    if (reply.isEmpty()) return QStringLiteral("Reply cannot be empty");
    if (mode == "regex") {
        const QRegularExpression regex(pattern);
        if (!regex.isValid()) return QStringLiteral("Invalid regular expression: %1").arg(regex.errorString());
    } else if (rx == "HEX") {
        const auto parsed = HexUtils::parse(pattern);
        if (!parsed || parsed->isEmpty()) return QStringLiteral("Invalid HEX receive pattern");
    }
    if (tx == "HEX") {
        const auto parsed = HexUtils::parse(reply);
        if (!parsed || parsed->isEmpty()) return QStringLiteral("Invalid HEX reply");
    }
    return {};
}

AutoReplyResult AutoReplyEngine::find(const QByteArray &data, const QJsonArray &rules,
                                      const QString &receiveFormat)
{
    AutoReplyResult result;
    const QString formatFilter = receiveFormat.trimmed().toUpper();
    for (qsizetype index = 0; index < rules.size(); ++index) {
        const QJsonObject rule = rules.at(index).toObject();
        if (!rule.value("enabled").toBool(true))
            continue;
        if (!formatFilter.isEmpty() &&
            rule.value("rx_format").toString("ASCII").toUpper() != formatFilter) {
            continue;
        }
        const QString error = validate(rule);
        if (!error.isEmpty()) {
            if (result.error.isEmpty())
                result.error = QStringLiteral("Rule %1: %2").arg(index + 1).arg(error);
            continue;
        }
        if (!matches(data, rule))
            continue;
        result.matched = true;
        result.payload = payload(rule);
        result.ruleName = rule.value("name").toString().trimmed();
        if (result.ruleName.isEmpty())
            result.ruleName = QStringLiteral("Rule %1").arg(index + 1);
        return result;
    }
    return result;
}
