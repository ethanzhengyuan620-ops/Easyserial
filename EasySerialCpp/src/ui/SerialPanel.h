#pragma once

#include <QStringList>
#include <QWidget>

#include "Settings.h"
#include "core/SerialController.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

class SerialPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit SerialPanel(Settings &settings, QWidget *parent = nullptr);

    SerialParameters parameters() const;
    void setConnected(bool connected);
    void setConnecting(bool connecting);
    void setDisconnecting(bool disconnecting);
    void setRecordChecked(bool checked);
    void setAutoScrollChecked(bool checked);
    bool autoScrollEnabled() const;

signals:
    void connectToggled();
    void modeChanged(int mode);
    void autoScrollChanged(bool enabled);
    void timestampsChanged(bool enabled);
    void timestampIntervalChanged(int milliseconds);
    void clearClicked();
    void rulesClicked();
    void toolsClicked();
    void pinToggled(bool enabled);
    void recordToggled(bool enabled);

private:
    void refreshPorts();
    QPushButton *actionButton(const QString &text, bool checkable = false);

    Settings &m_settings;
    QComboBox *m_port = nullptr;
    QComboBox *m_baud = nullptr;
    QComboBox *m_dataBits = nullptr;
    QComboBox *m_parity = nullptr;
    QComboBox *m_stopBits = nullptr;
    QComboBox *m_mode = nullptr;
    QCheckBox *m_autoScroll = nullptr;
    QCheckBox *m_timestamps = nullptr;
    QSpinBox *m_timestampInterval = nullptr;
    QLabel *m_badge = nullptr;
    QPushButton *m_connect = nullptr;
    QPushButton *m_record = nullptr;
    QTimer *m_portTimer = nullptr;
    QStringList m_knownPorts;
};
