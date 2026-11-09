#include "ui/AutoReplyWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <functional>

#include "core/AutoReplyEngine.h"

namespace {
void setComboData(QComboBox *combo, const QVariant &value)
{
    const int index = combo->findData(value);
    if (index >= 0) combo->setCurrentIndex(index);
}

QString matchLabel(const QString &mode)
{
    if (mode == "contains") return QStringLiteral("Contains");
    if (mode == "regex") return QStringLiteral("Regex");
    return QStringLiteral("Exact match");
}
}

AutoReplyRuleDialog::AutoReplyRuleDialog(const QJsonObject &rule, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Auto Reply Rule"));
    setMinimumWidth(520);
    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_name = new QLineEdit(rule.value("name").toString(), this);
    m_enabled = new QCheckBox(QStringLiteral("Enable this rule"), this);
    m_enabled->setChecked(rule.value("enabled").toBool(true));
    m_receiveFormat = new QComboBox(this);
    m_receiveFormat->addItem(QStringLiteral("ASCII Text"), "ASCII");
    m_receiveFormat->addItem(QStringLiteral("HEX Bytes"), "HEX");
    setComboData(m_receiveFormat, rule.value("rx_format").toString("ASCII"));
    m_match = new QComboBox(this);
    m_match->addItem(QStringLiteral("Exact match"), "exact");
    m_match->addItem(QStringLiteral("Contains"), "contains");
    m_match->addItem(QStringLiteral("Regex"), "regex");
    setComboData(m_match, rule.value("match").toString("exact"));
    m_caseSensitive = new QCheckBox(QStringLiteral("Case sensitive"), this);
    m_caseSensitive->setChecked(rule.value("case_sensitive").toBool(true));
    m_pattern = new QLineEdit(rule.value("pattern").toString(), this);
    m_pattern->setPlaceholderText(QStringLiteral("ASCII: PING; HEX: 01 03"));
    m_transmitFormat = new QComboBox(this);
    m_transmitFormat->addItem(QStringLiteral("ASCII Text"), "ASCII");
    m_transmitFormat->addItem(QStringLiteral("HEX Bytes (no line ending)"), "HEX");
    setComboData(m_transmitFormat, rule.value("tx_format").toString("ASCII"));
    m_reply = new QPlainTextEdit(rule.value("reply").toString(), this);
    m_reply->setFixedHeight(85);
    m_lineEnding = new QComboBox(this);
    m_lineEnding->addItem(QStringLiteral("None"), 0);
    m_lineEnding->addItem(QStringLiteral("\\r\\n"), 1);
    m_lineEnding->addItem(QStringLiteral("\\r"), 2);
    m_lineEnding->addItem(QStringLiteral("\\n"), 3);
    setComboData(m_lineEnding, rule.value("line_ending").toInt(0));

    form->addRow(QStringLiteral("Rule name"), m_name);
    form->addRow(QString{}, m_enabled);
    form->addRow(QStringLiteral("Receive format"), m_receiveFormat);
    form->addRow(QStringLiteral("Match mode"), m_match);
    form->addRow(QString{}, m_caseSensitive);
    form->addRow(QStringLiteral("Receive pattern"), m_pattern);
    form->addRow(QStringLiteral("Reply format"), m_transmitFormat);
    form->addRow(QStringLiteral("Reply data"), m_reply);
    form->addRow(QStringLiteral("ASCII line ending"), m_lineEnding);
    root->addLayout(form);
    auto *tip = new QLabel(QStringLiteral("HEX matches raw bytes. HEX replies never append 0D 0A."), this);
    tip->setObjectName(QStringLiteral("dim"));
    root->addWidget(tip);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &AutoReplyRuleDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_receiveFormat, &QComboBox::currentIndexChanged, this, &AutoReplyRuleDialog::syncControls);
    connect(m_match, &QComboBox::currentIndexChanged, this, &AutoReplyRuleDialog::syncControls);
    connect(m_transmitFormat, &QComboBox::currentIndexChanged, this, &AutoReplyRuleDialog::syncControls);
    syncControls();
}

QJsonObject AutoReplyRuleDialog::resultRule() const
{
    return {{"name", m_name->text().trimmed()}, {"enabled", m_enabled->isChecked()},
            {"rx_format", m_receiveFormat->currentData().toString()},
            {"match", m_match->currentData().toString()},
            {"case_sensitive", m_caseSensitive->isChecked()},
            {"pattern", m_pattern->text()},
            {"tx_format", m_transmitFormat->currentData().toString()},
            {"reply", m_reply->toPlainText()},
            {"line_ending", m_lineEnding->currentData().toInt()}};
}

void AutoReplyRuleDialog::syncControls()
{
    m_lineEnding->setEnabled(m_transmitFormat->currentData().toString() != "HEX");
    m_caseSensitive->setEnabled(m_receiveFormat->currentData().toString() == "ASCII" ||
                                m_match->currentData().toString() == "regex");
}

void AutoReplyRuleDialog::validateAndAccept()
{
    const QString error = AutoReplyEngine::validate(resultRule());
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Cannot Save Rule"), error);
        return;
    }
    accept();
}

AutoReplyWidget::AutoReplyWidget(Settings &settings, QWidget *parent)
    : QWidget(parent), m_settings(settings)
{
    auto *root = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    m_enabled = new QCheckBox(QStringLiteral("Enable auto reply"), this);
    m_enabled->setChecked(settings.boolValue("auto_reply_enabled", false));
    top->addWidget(m_enabled);
    top->addStretch();
    root->addLayout(top);

    auto *framingRow = new QHBoxLayout;
    m_framing = new QComboBox(this);
    m_framing->addItem(QStringLiteral("Newline (ASCII)"), "ASCII");
    m_framing->addItem(QStringLiteral("Idle gap (HEX / binary)"), "HEX");
    setComboData(m_framing, settings.stringValue("auto_reply_framing", "ASCII"));
    m_cooldown = new QSpinBox(this);
    m_cooldown->setRange(0, 60000);
    m_cooldown->setSuffix(QStringLiteral(" ms"));
    m_cooldown->setValue(settings.intValue("auto_reply_cooldown", 100));
    framingRow->addWidget(new QLabel(QStringLiteral("Framing"), this));
    framingRow->addWidget(m_framing);
    framingRow->addStretch();
    framingRow->addWidget(new QLabel(QStringLiteral("Reply cooldown"), this));
    framingRow->addWidget(m_cooldown);
    root->addLayout(framingRow);
    root->addWidget(new QLabel(QStringLiteral(
        "All rules share the framing mode above. ASCII/HEX defines how each pattern is interpreted.\n"
        "Rules are checked from top to bottom. Only the first matching rule replies to each frame."), this));
    m_list = new QListWidget(this);
    root->addWidget(m_list, 1);
    auto *buttons = new QHBoxLayout;
    const QList<QPair<QString, std::function<void()>>> actions{
        {QStringLiteral("+ Add"), [this] { addRule(); }},
        {QStringLiteral("Edit"), [this] { editRule(); }},
        {QStringLiteral("Delete"), [this] { removeRule(); }},
        {QStringLiteral("↑"), [this] { moveRule(-1); }},
        {QStringLiteral("↓"), [this] { moveRule(1); }}
    };
    for (const auto &[text, action] : actions) {
        auto *button = new QPushButton(text, this);
        connect(button, &QPushButton::clicked, this, action);
        buttons->addWidget(button);
    }
    buttons->addStretch();
    root->addLayout(buttons);
    for (const QJsonValue &value : settings.arrayValue("auto_reply_rules"))
        appendRule(value.toObject());

    connect(m_enabled, &QCheckBox::toggled, this,
            [this](bool value) { m_settings.setValue("auto_reply_enabled", value); });
    connect(m_framing, &QComboBox::currentIndexChanged, this, [this] {
        const QString format = m_framing->currentData().toString();
        m_settings.setValue("auto_reply_framing", format);
        emit framingChanged(format);
    });
    connect(m_cooldown, &QSpinBox::valueChanged, this,
            [this](int value) { m_settings.setValue("auto_reply_cooldown", value); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { editRule(); });
}

void AutoReplyWidget::appendRule(const QJsonObject &rule, int row)
{
    auto *item = new QListWidgetItem;
    item->setData(Qt::UserRole, rule.toVariantMap());
    if (row < 0) m_list->addItem(item); else m_list->insertItem(row, item);
    refreshItem(m_list->row(item));
}

void AutoReplyWidget::refreshItem(int row)
{
    if (row < 0 || row >= m_list->count()) return;
    QListWidgetItem *item = m_list->item(row);
    const QJsonObject rule = QJsonObject::fromVariantMap(item->data(Qt::UserRole).toMap());
    const QString mark = rule.value("enabled").toBool(true) ? QStringLiteral("●") : QStringLiteral("○");
    QString name = rule.value("name").toString().trimmed();
    if (name.isEmpty()) name = QStringLiteral("Unnamed rule");
    item->setText(QStringLiteral("%1 %2  [%3 %4] %5  →  [%6] %7")
        .arg(mark, name, rule.value("rx_format").toString("ASCII"),
             matchLabel(rule.value("match").toString("exact")),
             rule.value("pattern").toString(), rule.value("tx_format").toString("ASCII"),
             rule.value("reply").toString().replace('\n', QStringLiteral("\\n"))));
}

void AutoReplyWidget::saveRules()
{
    QJsonArray rules;
    for (int row = 0; row < m_list->count(); ++row)
        rules.append(QJsonObject::fromVariantMap(m_list->item(row)->data(Qt::UserRole).toMap()));
    m_settings.setArray("auto_reply_rules", rules);
}

void AutoReplyWidget::addRule()
{
    AutoReplyRuleDialog dialog({}, this);
    if (dialog.exec() == QDialog::Accepted) {
        appendRule(dialog.resultRule());
        m_list->setCurrentRow(m_list->count() - 1);
        saveRules();
    }
}

void AutoReplyWidget::editRule()
{
    const int row = m_list->currentRow();
    if (row < 0) return;
    const QJsonObject rule = QJsonObject::fromVariantMap(m_list->item(row)->data(Qt::UserRole).toMap());
    AutoReplyRuleDialog dialog(rule, this);
    if (dialog.exec() == QDialog::Accepted) {
        m_list->item(row)->setData(Qt::UserRole, dialog.resultRule().toVariantMap());
        refreshItem(row);
        saveRules();
    }
}

void AutoReplyWidget::removeRule()
{
    const int row = m_list->currentRow();
    if (row >= 0) {
        delete m_list->takeItem(row);
        saveRules();
    }
}

void AutoReplyWidget::moveRule(int delta)
{
    const int row = m_list->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_list->count()) return;
    QListWidgetItem *item = m_list->takeItem(row);
    m_list->insertItem(target, item);
    m_list->setCurrentRow(target);
    saveRules();
}
