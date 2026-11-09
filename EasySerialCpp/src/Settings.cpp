#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QJsonObject quickCommand(const QString &name, const QString &data, bool hex)
{
    return {{QStringLiteral("name"), name},
            {QStringLiteral("data"), data},
            {QStringLiteral("hex"), hex}};
}
}

Settings::Settings(const QString &path)
    : m_data(defaults())
{
    const QString base = path.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        : QFileInfo(path).absolutePath();
    QDir().mkpath(base);
    m_path = path.isEmpty() ? QDir(base).filePath(QStringLiteral("config.json")) : path;
    load();
}

QJsonObject Settings::defaults()
{
    QJsonArray rules{
        QJsonObject{{"pattern", R"(\bERROR\b|\bFAIL\b)"}, {"color", "#DC2626"}, {"enabled", true}},
        QJsonObject{{"pattern", R"(\bWARN(ING)?\b)"}, {"color", "#B45309"}, {"enabled", true}},
        QJsonObject{{"pattern", R"(\bOK\b|\bPASS\b)"}, {"color", "#15803D"}, {"enabled", true}}
    };
    QJsonArray quick{
        quickCommand("AT", "AT", false),
        quickCommand(QStringLiteral("Version"), "AT+VER?", false),
        quickCommand(QStringLiteral("Reset"), "AT+RST", false),
        quickCommand(QStringLiteral("Read Registers"), "01 03 00 00 00 0A C5 CD", true),
        quickCommand(QStringLiteral("Heartbeat"), "55 AA 01 00", true),
        quickCommand(QStringLiteral("Custom"), "", false)
    };
    return {
        {"port", ""}, {"baud", 115200}, {"databits", 8},
        {"parity", "None"}, {"stopbits", "1"},
        {"mode", 1}, {"autoscroll", true}, {"show_ts", true},
        {"ts_interval", 100}, {"frame_gap", 20}, {"max_lines", 5000},
        {"font_family", "Consolas"}, {"font_size", 9},
        {"receive_font_sizes", QJsonArray{}}, {"send_font_size", 0},
        {"col_text", "#1E293B"}, {"col_bg", "#FFFFFF"},
        {"col_ts", "#15803D"}, {"col_hex", "#9A3412"},
        {"col_ascii", "#0369A1"}, {"rules", rules}, {"appearance_version", 2},
        {"send_hex", false}, {"line_ending", 1},
        {"timer_interval", 500}, {"quick_cmds", quick},
        {"auto_reply_enabled", false}, {"auto_reply_cooldown", 100},
        {"auto_reply_framing", "ASCII"},
        {"auto_reply_rules", QJsonArray{}}, {"pin", false}, {"geometry", ""}, {"splitter", ""},
        {"pb_proto_files", QJsonArray{}}, {"pb_include_folders", QJsonArray{}},
        {"pb_selected_file", ":/protocols/TaskBatch.proto"},
        {"pb_selected_messages", QJsonObject{}}, {"pb_import_directory", ""},
        {"monitor_opacity", 90}, {"monitor_font_size", 0}, {"monitor_snap", true},
        {"monitor_corner", 3}, {"monitor_screen", ""}, {"monitor_geometry", QJsonArray{}}
    };
}

void Settings::load()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonObject saved = document.object();
    for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) {
        if (m_data.contains(it.key()))
            m_data.insert(it.key(), it.value());
    }
    // Translate only untouched built-in command labels, never user-authored data.
    const QStringList legacyNames{QStringLiteral("AT"), QStringLiteral("\u7248\u672c"),
        QStringLiteral("\u590d\u4f4d"), QStringLiteral("\u8bfb\u5bc4\u5b58\u5668"),
        QStringLiteral("\u5fc3\u8df3"), QStringLiteral("\u81ea\u5b9a\u4e49")};
    const auto standardCommands = defaults().value("quick_cmds").toArray();
    auto commands = m_data.value("quick_cmds").toArray();
    for (int i = 0; i < commands.size(); ++i) {
        auto command = commands[i].toObject();
        for (int j = 0; j < standardCommands.size(); ++j) {
            const auto standard = standardCommands[j].toObject();
            if (command.value("name").toString() == legacyNames[j] &&
                command.value("data") == standard.value("data") &&
                command.value("hex") == standard.value("hex"))
                command.insert("name", standard.value("name"));
        }
        commands[i] = command;
    }
    m_data.insert("quick_cmds", commands);
    // Upgrade untouched legacy defaults, without replacing custom user colors.
    if (saved.value("appearance_version").toInt() < 2) {
        const QJsonObject legacy{{"col_text", "#E2E8F0"}, {"col_bg", "#0D0D0D"},
            {"col_ts", "#00C853"}, {"col_hex", "#E0A86A"}, {"col_ascii", "#4FC3F7"}};
        const auto light = defaults();
        for (auto it = legacy.begin(); it != legacy.end(); ++it) {
            if (saved.value(it.key()).toString().compare(it.value().toString(), Qt::CaseInsensitive) == 0)
                m_data.insert(it.key(), light.value(it.key()));
        }
        auto rules = m_data.value("rules").toArray();
        const auto standardRules = light.value("rules").toArray();
        const QStringList oldColors{"#FF5252", "#FFB300", "#00E676"};
        for (int i = 0; i < rules.size(); ++i) {
            auto rule = rules[i].toObject();
            for (int j = 0; j < standardRules.size(); ++j) {
                const auto standard = standardRules[j].toObject();
                if (rule.value("pattern") == standard.value("pattern") &&
                    rule.value("color").toString().compare(oldColors[j], Qt::CaseInsensitive) == 0)
                    rule.insert("color", standard.value("color"));
            }
            rules[i] = rule;
        }
        m_data.insert("rules", rules);
        m_data.insert("appearance_version", 2);
    }
}

QVariant Settings::value(const QString &key, const QVariant &fallback) const
{
    const auto it = m_data.constFind(key);
    return it == m_data.constEnd() ? fallback : it.value().toVariant();
}

QString Settings::stringValue(const QString &key, const QString &fallback) const
{
    return value(key, fallback).toString();
}

int Settings::intValue(const QString &key, int fallback) const
{
    return value(key, fallback).toInt();
}

bool Settings::boolValue(const QString &key, bool fallback) const
{
    return value(key, fallback).toBool();
}

QJsonArray Settings::arrayValue(const QString &key) const
{
    return m_data.value(key).toArray();
}

void Settings::setValue(const QString &key, const QVariant &value, bool flush)
{
    m_data.insert(key, QJsonValue::fromVariant(value));
    if (flush)
        save();
}

void Settings::setArray(const QString &key, const QJsonArray &value, bool flush)
{
    m_data.insert(key, value);
    if (flush)
        save();
}

bool Settings::save() const
{
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(m_data).toJson(QJsonDocument::Indented));
    return file.commit();
}
