#include "ui/SendPanel.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <utility>

#include "core/HexUtils.h"

namespace {
class QuickCommandDialog final : public QDialog
{
public:
    QuickCommandDialog(const QJsonObject &command, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Edit Quick Command"));
        auto *form = new QFormLayout(this);
        m_name = new QLineEdit(command.value("name").toString(), this);
        m_data = new QLineEdit(command.value("data").toString(), this);
        m_hex = new QCheckBox(QStringLiteral("HEX format"), this);
        m_hex->setChecked(command.value("hex").toBool(false));
        form->addRow(QStringLiteral("Name"), m_name);
        form->addRow(QStringLiteral("Data"), m_data);
        form->addRow(QString{}, m_hex);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    QJsonObject result() const
    {
        QString name = m_name->text().trimmed();
        if (name.isEmpty()) name = QStringLiteral("Unnamed");
        return {{"name", name}, {"data", m_data->text()}, {"hex", m_hex->isChecked()}};
    }

private:
    QLineEdit *m_name = nullptr;
    QLineEdit *m_data = nullptr;
    QCheckBox *m_hex = nullptr;
};
}

SendPanel::SendPanel(Settings &settings, QWidget *parent)
    : QWidget(parent), m_settings(settings)
{
    setObjectName(QStringLiteral("sendSurface"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(6);

    auto *first = new QHBoxLayout;
    first->addWidget(new QLabel(QStringLiteral("Format"), this));
    m_format = new QButtonGroup(this);
    auto *ascii = new QRadioButton(QStringLiteral("ASCII"), this);
    auto *hex = new QRadioButton(QStringLiteral("HEX"), this);
    m_format->addButton(ascii, 0);
    m_format->addButton(hex, 1);
    (settings.boolValue("send_hex", false) ? hex : ascii)->setChecked(true);
    first->addWidget(ascii);
    first->addWidget(hex);
    first->addSpacing(18);
    first->addWidget(new QLabel(QStringLiteral("Line ending"), this));
    m_lineEnding = new QComboBox(this);
    m_lineEnding->addItems({QStringLiteral("None"), QStringLiteral("\\r\\n"),
                            QStringLiteral("\\r"), QStringLiteral("\\n")});
    m_lineEnding->setCurrentIndex(qBound(0, settings.intValue("line_ending", 1), 3));
    first->addWidget(m_lineEnding);
    first->addStretch();
    root->addLayout(first);

    m_input = new QPlainTextEdit(this);
    m_input->setObjectName(QStringLiteral("sendInput"));
    m_input->setPlaceholderText(QStringLiteral("Enter data to send…"));
    m_input->setMinimumHeight(55);
    m_input->installEventFilter(this);
    m_input->viewport()->installEventFilter(this);
    root->addWidget(m_input, 1);

    auto *actions = new QHBoxLayout;
    m_timerEnabled = new QCheckBox(QStringLiteral("Repeat"), this);
    m_interval = new QSpinBox(this);
    m_interval->setRange(10, 3600000);
    m_interval->setSuffix(QStringLiteral(" ms"));
    m_interval->setValue(settings.intValue("timer_interval", 500));
    m_interval->setVisible(false);
    m_send = new QPushButton(QStringLiteral("Send"), this);
    m_send->setObjectName(QStringLiteral("accent"));
    m_send->setFixedWidth(140);
    actions->addWidget(m_timerEnabled);
    actions->addWidget(m_interval);
    actions->addStretch();
    actions->addWidget(m_send);
    root->addLayout(actions);

    auto *quick = new QHBoxLayout;
    quick->addWidget(new QLabel(QStringLiteral("Quick"), this));
    QJsonArray commands = settings.arrayValue("quick_cmds");
    while (commands.size() < 6)
        commands.append(QJsonObject{{"name", QStringLiteral("Quick %1").arg(commands.size() + 1)},
                                    {"data", ""}, {"hex", false}});
    if (settings.arrayValue("quick_cmds").size() < 6)
        settings.setArray("quick_cmds", commands);
    for (int index = 0; index < 6; ++index) {
        auto *button = new QPushButton(commands.at(index).toObject().value("name").toString(), this);
        button->setObjectName(QStringLiteral("quick"));
        button->setToolTip(QStringLiteral("Click to send; right-click to edit"));
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QPushButton::clicked, this, [this, index] { sendQuick(index); });
        connect(button, &QPushButton::customContextMenuRequested, this,
                [this, index](const QPoint &) { editQuick(index); });
        m_quickButtons.push_back(button);
        quick->addWidget(button);
    }
    quick->addStretch();
    root->addLayout(quick);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &SendPanel::sendCurrent);
    connect(m_send, &QPushButton::clicked, this, &SendPanel::sendCurrent);
    connect(m_timerEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
        m_interval->setVisible(enabled);
        if (enabled) {
            m_timer->start(m_interval->value());
            sendCurrent();
        } else {
            m_timer->stop();
        }
    });
    connect(m_interval, &QSpinBox::valueChanged, this, [this](int value) {
        m_settings.setValue("timer_interval", value);
        if (m_timer->isActive()) m_timer->start(value);
    });
    connect(m_lineEnding, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_settings.setValue("line_ending", index);
    });
    connect(m_format, &QButtonGroup::idClicked, this, [this](int id) {
        m_settings.setValue("send_hex", id == 1);
        syncFormat();
    });

    applyAppearance();
    syncFormat();
}

std::optional<QByteArray> SendPanel::buildPayload() const
{
    if (m_format->checkedId() == 1)
        return HexUtils::parse(m_input->toPlainText());
    return m_input->toPlainText().toUtf8() + HexUtils::lineEnding(m_lineEnding->currentIndex());
}

void SendPanel::sendCurrent()
{
    const auto payload = buildPayload();
    if (!payload) {
        emit hint(QStringLiteral("Invalid HEX: enter an even number of hexadecimal digits, e.g. 01 03 0A."));
        stopTimer();
        return;
    }
    if (!payload->isEmpty())
        emit sendBytes(*payload);
}

void SendPanel::sendQuick(int index)
{
    const QJsonArray commands = m_settings.arrayValue("quick_cmds");
    if (index < 0 || index >= commands.size())
        return;
    const QJsonObject command = commands.at(index).toObject();
    QByteArray data;
    if (command.value("hex").toBool()) {
        const auto parsed = HexUtils::parse(command.value("data").toString());
        if (!parsed) {
            emit hint(QStringLiteral("Invalid HEX in quick command [%1]")
                      .arg(command.value("name").toString()));
            return;
        }
        data = *parsed;
    } else {
        data = command.value("data").toString().toUtf8();
    }
    if (data.isEmpty()) {
        emit hint(QStringLiteral("Quick command is empty. Right-click to edit."));
        return;
    }
    if (!command.value("hex").toBool())
        data += HexUtils::lineEnding(m_lineEnding->currentIndex());
    emit sendBytes(data);
}

void SendPanel::editQuick(int index)
{
    QJsonArray commands = m_settings.arrayValue("quick_cmds");
    if (index < 0 || index >= commands.size())
        return;
    QuickCommandDialog dialog(commands.at(index).toObject(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    commands.replace(index, dialog.result());
    m_settings.setArray("quick_cmds", commands);
    m_quickButtons.at(index)->setText(dialog.result().value("name").toString());
}

void SendPanel::syncFormat()
{
    const bool hex = m_format->checkedId() == 1;
    m_lineEnding->setEnabled(!hex);
    m_lineEnding->setToolTip(hex ? QStringLiteral("HEX sends raw bytes without appending CR/LF") : QString{});
}

void SendPanel::setSendEnabled(bool enabled)
{
    if (!enabled) stopTimer();
    m_send->setEnabled(enabled);
    m_timerEnabled->setEnabled(enabled);
    for (QPushButton *button : std::as_const(m_quickButtons))
        button->setEnabled(enabled);
}

void SendPanel::stopTimer()
{
    m_timerEnabled->setChecked(false);
    m_timer->stop();
}

void SendPanel::applyInputFont()
{
    QFont font(m_settings.stringValue("font_family", "Consolas"), m_inputFontSize);
    font.setFamilies({m_settings.stringValue("font_family", "Consolas"), QStringLiteral("Microsoft YaHei UI")});
    font.setStyleHint(QFont::Monospace);
    m_input->setFont(font);
    emit fontSizeChanged(m_inputFontSize);
    m_input->setStyleSheet(QStringLiteral("QPlainTextEdit{background:%1;color:%2;}")
        .arg(m_settings.stringValue("col_bg", "#FFFFFF"),
             m_settings.stringValue("col_text", "#1E293B")));
}

void SendPanel::applyAppearance()
{
    const int saved = m_settings.intValue("send_font_size", 0);
    m_inputFontSize = qBound(6, saved > 0 ? saved : m_settings.intValue("font_size", 9), 36);
    applyInputFont();
}

bool SendPanel::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_input || watched == m_input->viewport()) && event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers().testFlag(Qt::ControlModifier)) {
            const int delta = wheel->angleDelta().y();
            if (delta != 0) {
                m_inputFontSize = qBound(6, m_inputFontSize +
                    (delta > 0 ? 1 : -1), 36);
                m_settings.setValue("send_font_size", m_inputFontSize);
                applyInputFont();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
