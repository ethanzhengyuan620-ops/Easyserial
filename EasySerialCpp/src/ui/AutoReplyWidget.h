#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QWidget>

#include "Settings.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;

class AutoReplyRuleDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit AutoReplyRuleDialog(const QJsonObject &rule = {}, QWidget *parent = nullptr);
    QJsonObject resultRule() const;

private:
    void syncControls();
    void validateAndAccept();

    QLineEdit *m_name = nullptr;
    QCheckBox *m_enabled = nullptr;
    QComboBox *m_receiveFormat = nullptr;
    QComboBox *m_match = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QLineEdit *m_pattern = nullptr;
    QComboBox *m_transmitFormat = nullptr;
    QPlainTextEdit *m_reply = nullptr;
    QComboBox *m_lineEnding = nullptr;
};

class AutoReplyWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit AutoReplyWidget(Settings &settings, QWidget *parent = nullptr);

signals:
    void framingChanged(const QString &format);

private:
    void appendRule(const QJsonObject &rule, int row = -1);
    void refreshItem(int row);
    void saveRules();
    void addRule();
    void editRule();
    void removeRule();
    void moveRule(int delta);

    Settings &m_settings;
    QCheckBox *m_enabled = nullptr;
    QComboBox *m_framing = nullptr;
    QSpinBox *m_cooldown = nullptr;
    QListWidget *m_list = nullptr;
};
