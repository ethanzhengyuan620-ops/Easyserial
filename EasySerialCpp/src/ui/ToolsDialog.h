#pragma once

#include <QDialog>
#include <atomic>
#include <memory>

#include "Settings.h"

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QThread;

class ToolsDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit ToolsDialog(Settings &settings, QWidget *parent = nullptr);
    ~ToolsDialog() override;

signals:
    void autoReplyFramingChanged(const QString &format);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *createAsciiHexTab();
    QWidget *createCrcTab();
    QWidget *createShaTab();
    QWidget *createTimestampTab();
    void asciiToHex();
    void hexToAscii();
    void calculateCrc();
    void calculateSha();
    void calculateFileSha();
    void timestampToDate();
    void dateToTimestamp();
    void setConvertStatus(const QString &text, bool success = true);

    Settings &m_settings;
    QComboBox *m_encoding = nullptr;
    QCheckBox *m_spaces = nullptr;
    QCheckBox *m_escapes = nullptr;
    QPlainTextEdit *m_ascii = nullptr;
    QPlainTextEdit *m_hex = nullptr;
    QLineEdit *m_convertStatus = nullptr;
    QLineEdit *m_crcInput = nullptr;
    QPlainTextEdit *m_crcOutput = nullptr;
    QPlainTextEdit *m_shaInput = nullptr;
    QCheckBox *m_shaHex = nullptr;
    QLineEdit *m_shaOutput = nullptr;
    QPushButton *m_shaCalculateButton = nullptr;
    QPushButton *m_fileShaButton = nullptr;
    QThread *m_shaThread = nullptr;
    std::shared_ptr<std::atomic_bool> m_shaCancel;
    bool m_closePending = false;
    QLineEdit *m_timestampInput = nullptr;
    QLineEdit *m_localTime = nullptr;
    QLineEdit *m_utcTime = nullptr;
    QLineEdit *m_dateInput = nullptr;
    QLineEdit *m_secondsOutput = nullptr;
    QLineEdit *m_millisecondsOutput = nullptr;
};
