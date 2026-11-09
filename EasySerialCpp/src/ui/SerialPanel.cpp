#include "ui/SerialPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "Theme.h"

namespace {
QLabel *dimLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("dim"));
    return label;
}

QFrame *line()
{
    auto *frame = new QFrame;
    frame->setFrameShape(QFrame::HLine);
    frame->setStyleSheet(QStringLiteral("color:#E2E8F0;"));
    return frame;
}
}

SerialPanel::SerialPanel(Settings &settings, QWidget *parent)
    : QWidget(parent), m_settings(settings)
{
    setObjectName(QStringLiteral("sidebar"));
    setFixedWidth(280);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(10);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("Connection"), this);
    title->setObjectName(QStringLiteral("sectionTitle"));
    m_badge = new QLabel(QStringLiteral("Offline"), this);
    m_badge->setStyleSheet(QStringLiteral("color:#64748B;border:1px solid #DCE3ED;"
                                           "border-radius:9px;padding:2px 10px;"));
    header->addWidget(title);
    header->addStretch();
    header->addWidget(m_badge);
    root->addLayout(header);

    root->addWidget(dimLabel(QStringLiteral("Port")));
    m_port = new QComboBox(this);
    m_port->setObjectName(QStringLiteral("serialPort"));
    m_port->setEditable(true);
    root->addWidget(m_port);

    auto *grid = new QGridLayout;
    m_baud = new QComboBox(this);
    m_baud->setEditable(true);
    m_baud->addItems({"1200", "2400", "4800", "9600", "14400", "19200",
                      "38400", "57600", "115200", "230400", "460800",
                      "921600", "1000000", "2000000"});
    m_dataBits = new QComboBox(this);
    m_dataBits->addItems({"5", "6", "7", "8"});
    m_parity = new QComboBox(this);
    m_parity->addItems({"None", "Odd", "Even", "Mark", "Space"});
    m_stopBits = new QComboBox(this);
    m_stopBits->addItems({"1", "1.5", "2"});
    grid->addWidget(dimLabel(QStringLiteral("Baud rate")), 0, 0);
    grid->addWidget(dimLabel(QStringLiteral("Data bits")), 0, 1);
    grid->addWidget(m_baud, 1, 0);
    grid->addWidget(m_dataBits, 1, 1);
    grid->addWidget(dimLabel(QStringLiteral("Parity")), 2, 0);
    grid->addWidget(dimLabel(QStringLiteral("Stop bits")), 2, 1);
    grid->addWidget(m_parity, 3, 0);
    grid->addWidget(m_stopBits, 3, 1);
    root->addLayout(grid);

    m_baud->setCurrentText(QString::number(settings.intValue("baud", 115200)));
    m_dataBits->setCurrentText(QString::number(settings.intValue("databits", 8)));
    m_parity->setCurrentText(settings.stringValue("parity", "None"));
    m_stopBits->setCurrentText(settings.stringValue("stopbits", "1"));

    m_connect = new QPushButton(QStringLiteral("Connect"), this);
    m_connect->setObjectName(QStringLiteral("accent"));
    m_connect->setMinimumHeight(38);
    root->addWidget(m_connect);
    root->addWidget(line());

    auto *displayTitle = new QLabel(QStringLiteral("Display"), this);
    displayTitle->setObjectName(QStringLiteral("sectionTitle"));
    root->addWidget(displayTitle);
    m_mode = new QComboBox(this);
    m_mode->addItems({QStringLiteral("ASCII Text"), QStringLiteral("HEX + ASCII"),
                      QStringLiteral("HEX Only")});
    m_mode->setCurrentIndex(qBound(0, settings.intValue("mode", 1), 2));
    root->addWidget(m_mode);
    root->addWidget(line());

    auto *toolTitle = new QLabel(QStringLiteral("Options"), this);
    toolTitle->setObjectName(QStringLiteral("sectionTitle"));
    root->addWidget(toolTitle);
    m_autoScroll = new QCheckBox(QStringLiteral("Auto scroll"), this);
    m_autoScroll->setObjectName(QStringLiteral("autoScroll"));
    m_autoScroll->setToolTip(QStringLiteral("Always follow incoming data when enabled. Turn off to browse history."));
    m_autoScroll->setChecked(settings.boolValue("autoscroll", true));
    m_timestamps = new QCheckBox(QStringLiteral("Show timestamps"), this);
    m_timestamps->setChecked(settings.boolValue("show_ts", true));
    m_timestampInterval = new QSpinBox(this);
    m_timestampInterval->setRange(0, 60000);
    m_timestampInterval->setSuffix(QStringLiteral(" ms"));
    m_timestampInterval->setValue(settings.intValue("ts_interval", 100));
    root->addWidget(m_autoScroll);
    root->addWidget(m_timestamps);
    root->addWidget(dimLabel(QStringLiteral("ASCII timestamp interval")));
    root->addWidget(m_timestampInterval);

    auto *clear = actionButton(QStringLiteral("Clear Monitor"));
    auto *rules = actionButton(QStringLiteral("Color Rules"));
    auto *tools = actionButton(QStringLiteral("Toolbox"));
    auto *pin = actionButton(QStringLiteral("Always on Top"), true);
    pin->setChecked(settings.boolValue("pin", false));
    m_record = actionButton(QStringLiteral("Record Log"), true);
    root->addStretch();

    connect(m_connect, &QPushButton::clicked, this, &SerialPanel::connectToggled);
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_settings.setValue("mode", index);
        emit modeChanged(index);
    });
    connect(m_autoScroll, &QCheckBox::toggled, this, [this](bool value) {
        m_settings.setValue("autoscroll", value);
        emit autoScrollChanged(value);
    });
    connect(m_timestamps, &QCheckBox::toggled, this, [this](bool value) {
        m_settings.setValue("show_ts", value);
        emit timestampsChanged(value);
    });
    connect(m_timestampInterval, &QSpinBox::valueChanged, this, [this](int value) {
        m_settings.setValue("ts_interval", value);
        emit timestampIntervalChanged(value);
    });
    connect(clear, &QPushButton::clicked, this, &SerialPanel::clearClicked);
    connect(rules, &QPushButton::clicked, this, &SerialPanel::rulesClicked);
    connect(tools, &QPushButton::clicked, this, &SerialPanel::toolsClicked);
    connect(pin, &QPushButton::toggled, this, &SerialPanel::pinToggled);
    connect(m_record, &QPushButton::toggled, this, &SerialPanel::recordToggled);

    m_portTimer = new QTimer(this);
    m_portTimer->setInterval(1500);
    connect(m_portTimer, &QTimer::timeout, this, &SerialPanel::refreshPorts);
    m_portTimer->start();
    refreshPorts();
    const QString savedPort = settings.stringValue("port");
    if (!savedPort.isEmpty())
        m_port->setCurrentText(savedPort);
    else if (m_port->count() > 0)
        m_port->setCurrentIndex(0);
}

QPushButton *SerialPanel::actionButton(const QString &text, bool checkable)
{
    auto *button = new QPushButton(text, this);
    button->setCheckable(checkable);
    button->setStyleSheet(QStringLiteral("text-align:left;padding-left:12px;"));
    layout()->addWidget(button);
    return button;
}

void SerialPanel::refreshPorts()
{
    QStringList ports;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
        ports.push_back(info.portName());
    if (ports == m_knownPorts)
        return;
    m_knownPorts = ports;
    const QString current = m_port->currentText();
    m_port->clear();
    m_port->addItems(ports);
    // The combo box is editable on purpose: users may enter a port that is
    // temporarily absent from enumeration.  Never replace that choice with
    // the first detected port when the device list changes.
    m_port->setCurrentText(current);
}

SerialParameters SerialPanel::parameters() const
{
    SerialParameters result;
    result.port = m_port->currentText().trimmed();
    bool ok = false;
    result.baud = m_baud->currentText().toInt(&ok);
    if (!ok || result.baud <= 0) result.baud = 115200;
    result.dataBits = m_dataBits->currentText().toInt();
    result.parity = m_parity->currentText();
    result.stopBits = m_stopBits->currentText();
    result.frameGapMs = m_settings.intValue("frame_gap", 20);
    result.splitNewline = false;
    return result;
}

void SerialPanel::setConnected(bool connected)
{
    m_connect->setEnabled(true);
    m_connect->setText(connected ? QStringLiteral("Disconnect") : QStringLiteral("Connect"));
    m_connect->setObjectName(connected ? QStringLiteral("danger") : QStringLiteral("accent"));
    m_badge->setText(connected ? QStringLiteral("Online") : QStringLiteral("Offline"));
    m_badge->setStyleSheet(connected
        ? QStringLiteral("color:#15803D;background:#F0FDF4;border:1px solid #BBF7D0;border-radius:9px;padding:2px 10px;")
        : QStringLiteral("color:#64748B;border:1px solid #DCE3ED;border-radius:9px;padding:2px 10px;"));
    m_connect->style()->unpolish(m_connect);
    m_connect->style()->polish(m_connect);
    const QList<QWidget *> controls{m_port, m_baud, m_dataBits, m_parity, m_stopBits};
    for (QWidget *widget : controls)
        widget->setEnabled(!connected);
    if (connected) {
        m_portTimer->stop();
    } else {
        refreshPorts();
        if (!m_portTimer->isActive())
            m_portTimer->start();
    }
}

void SerialPanel::setConnecting(bool connecting)
{
    m_connect->setEnabled(!connecting);
    if (connecting) {
        m_connect->setText(QStringLiteral("Connecting…"));
        m_portTimer->stop();
    }
}

void SerialPanel::setDisconnecting(bool disconnecting)
{
    m_connect->setEnabled(!disconnecting);
    if (disconnecting) m_connect->setText(QStringLiteral("Disconnecting…"));
}

void SerialPanel::setRecordChecked(bool checked)
{
    const QSignalBlocker blocker(m_record);
    m_record->setChecked(checked);
}

void SerialPanel::setAutoScrollChecked(bool checked)
{
    // Use the same persistence + notification path as a click in the sidebar.
    m_autoScroll->setChecked(checked);
}

bool SerialPanel::autoScrollEnabled() const
{
    return m_autoScroll->isChecked();
}
