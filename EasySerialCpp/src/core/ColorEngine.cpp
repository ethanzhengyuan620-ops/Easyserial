#include "core/ColorEngine.h"

#include <QJsonObject>
#include <QMap>
#include <utility>

void ColorEngine::setRules(const QJsonArray &rules)
{
    m_rules.clear();
    for (const QJsonValue &value : rules) {
        const QJsonObject object = value.toObject();
        if (!object.value("enabled").toBool(true))
            continue;
        QRegularExpression regex(object.value("pattern").toString(), QRegularExpression::UseUnicodePropertiesOption);
        const QColor color(object.value("color").toString());
        if (regex.isValid() && color.isValid())
            m_rules.push_back({std::move(regex), color});
    }
}

QColor ColorEngine::namedColor(const QString &name)
{
    static const QMap<QString, QColor> colors{
        {QStringLiteral("red"), QColor(QStringLiteral("#FF5252"))},
        {QStringLiteral("green"), QColor(QStringLiteral("#00E676"))},
        {QStringLiteral("yellow"), QColor(QStringLiteral("#FFD600"))},
        {QStringLiteral("blue"), QColor(QStringLiteral("#448AFF"))},
        {QStringLiteral("cyan"), QColor(QStringLiteral("#00BCD4"))},
        {QStringLiteral("magenta"), QColor(QStringLiteral("#E040FB"))},
        {QStringLiteral("white"), QColor(QStringLiteral("#FFFFFF"))},
        {QStringLiteral("gray"), QColor(QStringLiteral("#9E9E9E"))},
        {QStringLiteral("orange"), QColor(QStringLiteral("#FF9100"))}
    };
    const auto it = colors.constFind(name.toLower());
    return it == colors.constEnd() ? QColor(name) : it.value();
}

void ColorEngine::applyRules(const QString &text, const QColor &fallback,
                             QVector<ColoredSegment> &output) const
{
    if (text.isEmpty())
        return;
    if (m_rules.isEmpty()) {
        output.push_back({text, fallback});
        return;
    }
    qsizetype position = 0;
    while (position < text.size()) {
        QRegularExpressionMatch best;
        QColor bestColor;
        for (const Rule &rule : m_rules) {
            const auto match = rule.regex.match(text, position);
            if (match.hasMatch() && match.capturedStart() < match.capturedEnd() &&
                (!best.hasMatch() || match.capturedStart() < best.capturedStart())) {
                best = match;
                bestColor = rule.color;
            }
        }
        if (!best.hasMatch()) {
            output.push_back({text.sliced(position), fallback});
            break;
        }
        if (best.capturedStart() > position)
            output.push_back({text.sliced(position, best.capturedStart() - position), fallback});
        output.push_back({best.captured(), bestColor});
        position = best.capturedEnd();
    }
}

QVector<ColoredSegment> ColorEngine::split(const QString &text, const QColor &fallback) const
{
    QVector<ColoredSegment> output;
    static const QRegularExpression tagRegex(QStringLiteral(
        R"TAG(\[(red|green|yellow|blue|cyan|magenta|white|gray|orange|#[0-9A-Fa-f]{6}):"((?:\\.|[^"\\])*)"\])TAG"));
    qsizetype position = 0;
    auto iterator = tagRegex.globalMatch(text);
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        applyRules(text.sliced(position, match.capturedStart() - position), fallback, output);
        QString inner = match.captured(2);
        inner.replace(QStringLiteral("\\\""), QStringLiteral("\""));
        inner.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        if (!inner.isEmpty())
            output.push_back({inner, namedColor(match.captured(1))});
        position = match.capturedEnd();
    }
    applyRules(text.sliced(position), fallback, output);
    return output;
}
