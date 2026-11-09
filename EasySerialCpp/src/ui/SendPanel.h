#pragma once

#include <QByteArray>
#include <QVector>
#include <QWidget>
#include <optional>

#include "Settings.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QEvent;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class SendPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit SendPanel(Settings &settings, QWidget *parent = nullptr);
    void setSendEnabled(bool enabled);
    void stopTimer();
    void applyAppearance();

signals:
    void fontSizeChanged(int size);
    void sendBytes(const QByteArray &data);
    void hint(const QString &message);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    std::optional<QByteArray> buildPayload() const;
    void sendCurrent();
    void sendQuick(int index);
    void editQuick(int index);
    void syncFormat();
    void applyInputFont();

    Settings &m_settings;
    QPlainTextEdit *m_input = nullptr;
    QButtonGroup *m_format = nullptr;
    QComboBox *m_lineEnding = nullptr;
    QCheckBox *m_timerEnabled = nullptr;
    QSpinBox *m_interval = nullptr;
    QPushButton *m_send = nullptr;
    QVector<QPushButton *> m_quickButtons;
    QTimer *m_timer = nullptr;
    int m_inputFontSize = 10;
};
