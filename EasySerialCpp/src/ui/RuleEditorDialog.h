#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class QListWidget;

class RuleEditorDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit RuleEditorDialog(const QJsonArray &rules, QWidget *parent = nullptr);
    QJsonArray rules() const;

private:
    void appendRule(const QJsonObject &rule);
    void refreshItem(int row);
    void addRule();
    void editRule();
    void removeRule();

    QListWidget *m_list = nullptr;
};
