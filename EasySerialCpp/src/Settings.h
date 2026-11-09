#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariant>

class Settings final
{
public:
    explicit Settings(const QString &path = {});

    QVariant value(const QString &key, const QVariant &fallback = {}) const;
    QString stringValue(const QString &key, const QString &fallback = {}) const;
    int intValue(const QString &key, int fallback = 0) const;
    bool boolValue(const QString &key, bool fallback = false) const;
    QJsonArray arrayValue(const QString &key) const;

    void setValue(const QString &key, const QVariant &value, bool flush = true);
    void setArray(const QString &key, const QJsonArray &value, bool flush = true);
    bool save() const;
    QString filePath() const { return m_path; }

private:
    static QJsonObject defaults();
    void load();

    QString m_path;
    QJsonObject m_data;
};
