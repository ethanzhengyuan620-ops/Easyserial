#include "ui/RuleEditorDialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <functional>

namespace {
class RuleDialog final : public QDialog
{
public:
    RuleDialog(const QJsonObject &rule, QWidget *parent)
        : QDialog(parent), m_color(rule.value("color").toString("#FF5252"))
    {
        setWindowTitle(QStringLiteral("Edit Color Rule"));
        setMinimumWidth(400);
        auto *root = new QVBoxLayout(this);
        root->addWidget(new QLabel(QStringLiteral("Regular expression"), this));
        m_pattern = new QLineEdit(rule.value("pattern").toString(), this);
        root->addWidget(m_pattern);
        auto *row = new QHBoxLayout;
        m_colorButton = new QPushButton(this);
        m_colorButton->setFixedSize(60, 28);
        paintColor();
        m_enabled = new QCheckBox(QStringLiteral("Enabled"), this);
        m_enabled->setChecked(rule.value("enabled").toBool(true));
        row->addWidget(new QLabel(QStringLiteral("Color"), this));
        row->addWidget(m_colorButton);
        row->addStretch();
        row->addWidget(m_enabled);
        root->addLayout(row);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        root->addWidget(buttons);
        connect(m_colorButton, &QPushButton::clicked, this, [this] {
            const QColor color = QColorDialog::getColor(QColor(m_color), this, QStringLiteral("Choose Color"));
            if (color.isValid()) { m_color = color.name(); paintColor(); }
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, this, [this] {
            const QRegularExpression regex(m_pattern->text());
            if (m_pattern->text().trimmed().isEmpty() || !regex.isValid()) {
                QMessageBox::warning(this, QStringLiteral("Invalid regex"),
                    regex.isValid() ? QStringLiteral("Regular expression cannot be empty") : regex.errorString());
                return;
            }
            accept();
        });
    }

    QJsonObject result() const
    {
        return {{"pattern", m_pattern->text()}, {"color", m_color},
                {"enabled", m_enabled->isChecked()}};
    }

private:
    void paintColor()
    {
        m_colorButton->setStyleSheet(QStringLiteral("background:%1;border:1px solid #555;").arg(m_color));
    }
    QLineEdit *m_pattern = nullptr;
    QPushButton *m_colorButton = nullptr;
    QCheckBox *m_enabled = nullptr;
    QString m_color;
};
}

RuleEditorDialog::RuleEditorDialog(const QJsonArray &rules, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Color Rules (top priority first; drag to reorder)"));
    resize(480, 400);
    auto *root = new QVBoxLayout(this);
    m_list = new QListWidget(this);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    root->addWidget(m_list);
    auto *row = new QHBoxLayout;
    for (const auto &[text, action] : QList<QPair<QString, std::function<void()>>>{
             {QStringLiteral("+ Add"), [this] { addRule(); }},
             {QStringLiteral("Edit"), [this] { editRule(); }},
             {QStringLiteral("Delete"), [this] { removeRule(); }}}) {
        auto *button = new QPushButton(text, this);
        connect(button, &QPushButton::clicked, this, action);
        row->addWidget(button);
    }
    row->addStretch();
    root->addLayout(row);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { editRule(); });
    for (const QJsonValue &value : rules) appendRule(value.toObject());
}

void RuleEditorDialog::appendRule(const QJsonObject &rule)
{
    auto *item = new QListWidgetItem;
    item->setData(Qt::UserRole, rule.toVariantMap());
    m_list->addItem(item);
    refreshItem(m_list->count() - 1);
}

void RuleEditorDialog::refreshItem(int row)
{
    const QJsonObject rule = QJsonObject::fromVariantMap(m_list->item(row)->data(Qt::UserRole).toMap());
    m_list->item(row)->setText(QStringLiteral("%1  %2")
        .arg(rule.value("enabled").toBool(true) ? QStringLiteral("●") : QStringLiteral("○"),
             rule.value("pattern").toString()));
    m_list->item(row)->setForeground(QColor(rule.value("color").toString()));
}

void RuleEditorDialog::addRule()
{
    RuleDialog dialog({}, this);
    if (dialog.exec() == QDialog::Accepted) appendRule(dialog.result());
}

void RuleEditorDialog::editRule()
{
    const int row = m_list->currentRow();
    if (row < 0) return;
    RuleDialog dialog(QJsonObject::fromVariantMap(m_list->item(row)->data(Qt::UserRole).toMap()), this);
    if (dialog.exec() == QDialog::Accepted) {
        m_list->item(row)->setData(Qt::UserRole, dialog.result().toVariantMap());
        refreshItem(row);
    }
}

void RuleEditorDialog::removeRule()
{
    const int row = m_list->currentRow();
    if (row >= 0) delete m_list->takeItem(row);
}

QJsonArray RuleEditorDialog::rules() const
{
    QJsonArray output;
    for (int row = 0; row < m_list->count(); ++row)
        output.append(QJsonObject::fromVariantMap(m_list->item(row)->data(Qt::UserRole).toMap()));
    return output;
}
