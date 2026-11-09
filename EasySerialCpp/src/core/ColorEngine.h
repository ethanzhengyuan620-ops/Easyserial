#pragma once

#include <QColor>
#include <QJsonArray>
#include <QRegularExpression>
#include <QString>
#include <QVector>

struct ColoredSegment
{
    QString text;
    QColor color;
};

class ColorEngine final
{
public:
    void setRules(const QJsonArray &rules);
    QVector<ColoredSegment> split(const QString &text, const QColor &fallback) const;

private:
    struct Rule { QRegularExpression regex; QColor color; };
    void applyRules(const QString &text, const QColor &fallback,
                    QVector<ColoredSegment> &output) const;
    static QColor namedColor(const QString &name);

    QVector<Rule> m_rules;
};

