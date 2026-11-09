#include "ui/ToolsDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileDevice>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringConverter>
#include <QStringDecoder>
#include <QStringList>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include "core/HexUtils.h"
#include "ui/AutoReplyWidget.h"
#include "ui/PbDecoderWidget.h"

namespace {
struct FileShaResult
{
    QString digest;
    QString error;
    bool cancelled = false;
};

std::optional<QByteArray> encodeText(const QString &text, const QString &encoding,
                                     QString *error)
{
    if (encoding == "UTF-8") return text.toUtf8();
    if (encoding == "ASCII") {
        QByteArray output;
        output.reserve(text.size());
        for (const QChar character : text) {
            if (character.unicode() > 0x7F) {
                if (error) *error = QStringLiteral("Text contains non-ASCII characters");
                return std::nullopt;
            }
            output.append(static_cast<char>(character.unicode()));
        }
        return output;
    }
#ifdef Q_OS_WIN
    if (text.isEmpty()) return QByteArray{};
    if (text.size() > std::numeric_limits<int>::max()) {
        if (error) *error = QStringLiteral("Text is too long to encode as GBK");
        return std::nullopt;
    }
    const int inputLength = static_cast<int>(text.size());
    BOOL usedDefault = FALSE;
    const int size = WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS,
        reinterpret_cast<LPCWCH>(text.utf16()), inputLength, nullptr, 0, nullptr, &usedDefault);
    if (size <= 0 || usedDefault) {
        if (error) *error = QStringLiteral("Text cannot be encoded as GBK");
        return std::nullopt;
    }
    QByteArray output(size, Qt::Uninitialized);
    usedDefault = FALSE;
    const int converted = WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS,
        reinterpret_cast<LPCWCH>(text.utf16()), inputLength, output.data(), size,
        nullptr, &usedDefault);
    if (converted != size || usedDefault) {
        if (error) *error = QStringLiteral("GBK encoding failed");
        return std::nullopt;
    }
    return output;
#else
    return text.toLocal8Bit();
#endif
}

std::optional<QString> decodeText(const QByteArray &data, const QString &encoding,
                                  QString *error)
{
    if (encoding == "UTF-8") {
        QStringDecoder decoder(QStringDecoder::Utf8,
                               QStringConverter::Flag::Stateless);
        const QString result = decoder(data);
        if (decoder.hasError()) {
            if (error) *error = QStringLiteral("Input is not valid UTF-8");
            return std::nullopt;
        }
        return result;
    }
    if (encoding == "ASCII") {
        for (const unsigned char byte : data) {
            if (byte > 0x7F) {
                if (error) *error = QStringLiteral("Input contains non-ASCII bytes");
                return std::nullopt;
            }
        }
        return QString::fromLatin1(data);
    }
#ifdef Q_OS_WIN
    if (data.isEmpty()) return QString{};
    if (data.size() > std::numeric_limits<int>::max()) {
        if (error) *error = QStringLiteral("Input is too long to decode as GBK");
        return std::nullopt;
    }
    const int inputLength = static_cast<int>(data.size());
    const int size = MultiByteToWideChar(936, MB_ERR_INVALID_CHARS,
        data.constData(), inputLength, nullptr, 0);
    if (size <= 0) {
        if (error) *error = QStringLiteral("Input is not valid GBK");
        return std::nullopt;
    }
    QString output(size, Qt::Uninitialized);
    const int converted = MultiByteToWideChar(
        936, MB_ERR_INVALID_CHARS, data.constData(), inputLength,
        reinterpret_cast<LPWSTR>(output.data()), size);
    if (converted != size) {
        if (error) *error = QStringLiteral("GBK decoding failed");
        return std::nullopt;
    }
    return output;
#else
    return QString::fromLocal8Bit(data);
#endif
}

std::optional<QByteArray> encodeEscapes(const QString &text, const QString &encoding,
                                       bool escapes, QString *error)
{
    if (!escapes) return encodeText(text, encoding, error);
    QByteArray output;
    qsizetype chunkStart = 0;
    qsizetype index = 0;
    const auto appendChunk = [&](qsizetype start, qsizetype length) -> bool {
        const auto encoded = encodeText(text.sliced(start, length), encoding, error);
        if (!encoded) return false;
        output += *encoded;
        return true;
    };
    while (index < text.size()) {
        if (text.at(index) != '\\' || index + 1 >= text.size()) {
            ++index;
            continue;
        }
        const QChar code = text.at(index + 1);
        int consumed = 0;
        char raw = 0;
        if (code == 'r') { raw = '\r'; consumed = 2; }
        else if (code == 'n') { raw = '\n'; consumed = 2; }
        else if (code == 't') { raw = '\t'; consumed = 2; }
        else if (code == '0') { raw = '\0'; consumed = 2; }
        else if (code == '\\') { raw = '\\'; consumed = 2; }
        else if (code == 'x' && index + 3 < text.size()) {
            bool ok = false;
            const int value = text.sliced(index + 2, 2).toInt(&ok, 16);
            if (ok) { raw = static_cast<char>(value); consumed = 4; }
        }
        if (consumed == 0) { ++index; continue; }
        if (!appendChunk(chunkStart, index - chunkStart)) return std::nullopt;
        output.append(raw);
        index += consumed;
        chunkStart = index;
    }
    if (!appendChunk(chunkStart, text.size() - chunkStart)) return std::nullopt;
    return output;
}

QString displayEscapes(const QString &text)
{
    QString output;
    for (const QChar character : text) {
        switch (character.unicode()) {
        case '\\': output += QStringLiteral("\\\\"); break;
        case '\r': output += QStringLiteral("\\r"); break;
        case '\n': output += QStringLiteral("\\n"); break;
        case '\t': output += QStringLiteral("\\t"); break;
        case 0: output += QStringLiteral("\\0"); break;
        default:
            if (character.unicode() < 32 || character.unicode() == 127)
                output += QStringLiteral("\\x") + QStringLiteral("%1").arg(static_cast<uint>(character.unicode()), 2, 16, QLatin1Char('0')).toUpper();
            else
                output += character;
        }
    }
    return output;
}

quint16 crc16(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (const unsigned char byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1U) ? static_cast<quint16>((crc >> 1U) ^ 0xA001U)
                             : static_cast<quint16>(crc >> 1U);
    }
    return crc;
}

QDateTime parseDateTime(const QString &text)
{
    // Try ISO first so an explicit Z or UTC offset is preserved.
    QDateTime date = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!date.isValid())
        date = QDateTime::fromString(text, Qt::ISODate);
    if (date.isValid())
        return date;

    static const QStringList formats{
        QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm"),
        QStringLiteral("yyyy/MM/dd HH:mm:ss.zzz"),
        QStringLiteral("yyyy/MM/dd HH:mm:ss"),
        QStringLiteral("yyyy/MM/dd HH:mm"),
        QStringLiteral("yyyyMMddHHmmss")
    };
    for (const QString &format : formats) {
        date = QDateTime::fromString(text, format);
        if (date.isValid())
            return date;
    }
    return {};
}
}

ToolsDialog::ToolsDialog(Settings &settings, QWidget *parent)
    : QDialog(parent), m_settings(settings)
{
    setWindowTitle(QStringLiteral("Toolbox"));
    setWindowFlag(Qt::WindowMinMaxButtonsHint, true);
    resize(720, 560);
    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(createAsciiHexTab(), QStringLiteral("ASCII ↔ HEX"));
    tabs->addTab(createCrcTab(), QStringLiteral("Modbus CRC16"));
    tabs->addTab(createShaTab(), QStringLiteral("SHA256"));
    tabs->addTab(createTimestampTab(), QStringLiteral("Timestamps"));
    auto *autoReply = new AutoReplyWidget(settings, tabs);
    connect(autoReply, &AutoReplyWidget::framingChanged,
            this, &ToolsDialog::autoReplyFramingChanged);
    tabs->addTab(autoReply, QStringLiteral("Auto Reply"));
    tabs->addTab(new PbDecoderWidget(settings, tabs), QStringLiteral("PB Decoder"));
    root->addWidget(tabs);
}

ToolsDialog::~ToolsDialog()
{
    if (!m_shaThread)
        return;
    if (m_shaCancel)
        m_shaCancel->store(true);
    // Destructor fallback. Normal closeEvent() cancellation is asynchronous,
    // so this wait is only needed if the dialog is destroyed by another path.
    m_shaThread->wait();
}

void ToolsDialog::closeEvent(QCloseEvent *event)
{
    if (m_shaThread && m_shaThread->isRunning()) {
        m_closePending = true;
        if (m_shaCancel)
            m_shaCancel->store(true);
        m_shaOutput->setText(QStringLiteral("Cancelling file SHA256 calculation…"));
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

QWidget *ToolsDialog::createAsciiHexTab()
{
    auto *page = new QWidget(this);
    auto *root = new QVBoxLayout(page);
    auto *options = new QHBoxLayout;
    options->addWidget(new QLabel(QStringLiteral("Text encoding"), page));
    m_encoding = new QComboBox(page);
    m_encoding->addItems({QStringLiteral("ASCII"), QStringLiteral("UTF-8"), QStringLiteral("GBK")});
    options->addWidget(m_encoding);
    m_spaces = new QCheckBox(QStringLiteral("Spaces between HEX bytes"), page);
    m_spaces->setChecked(true);
    m_escapes = new QCheckBox(QStringLiteral("Interpret/show \\r \\n \\t \\0 \\xNN"), page);
    m_escapes->setChecked(true);
    options->addWidget(m_spaces);
    options->addWidget(m_escapes);
    options->addStretch();
    root->addLayout(options);
    root->addWidget(new QLabel(QStringLiteral("ASCII / Text"), page));
    m_ascii = new QPlainTextEdit(page);
    root->addWidget(m_ascii, 1);
    auto *buttons = new QHBoxLayout;
    auto *toHex = new QPushButton(QStringLiteral("ASCII → HEX"), page);
    toHex->setObjectName(QStringLiteral("accent"));
    auto *toAscii = new QPushButton(QStringLiteral("HEX → ASCII"), page);
    auto *copyAscii = new QPushButton(QStringLiteral("Copy ASCII"), page);
    auto *copyHex = new QPushButton(QStringLiteral("Copy HEX"), page);
    auto *clear = new QPushButton(QStringLiteral("Clear"), page);
    buttons->addStretch();
    buttons->addWidget(toHex);
    buttons->addWidget(toAscii);
    buttons->addWidget(copyAscii);
    buttons->addWidget(copyHex);
    buttons->addWidget(clear);
    buttons->addStretch();
    root->addLayout(buttons);
    root->addWidget(new QLabel(QStringLiteral("HEX"), page));
    m_hex = new QPlainTextEdit(page);
    root->addWidget(m_hex, 1);
    m_convertStatus = new QLineEdit(page);
    m_convertStatus->setReadOnly(true);
    root->addWidget(m_convertStatus);
    connect(toHex, &QPushButton::clicked, this, &ToolsDialog::asciiToHex);
    connect(toAscii, &QPushButton::clicked, this, &ToolsDialog::hexToAscii);
    connect(copyAscii, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_ascii->toPlainText());
        setConvertStatus(QStringLiteral("ASCII / text copied"));
    });
    connect(copyHex, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_hex->toPlainText());
        setConvertStatus(QStringLiteral("HEX copied"));
    });
    connect(clear, &QPushButton::clicked, this, [this] { m_ascii->clear(); m_hex->clear(); });
    return page;
}

QWidget *ToolsDialog::createCrcTab()
{
    auto *page = new QWidget(this);
    auto *root = new QVBoxLayout(page);
    root->addWidget(new QLabel(QStringLiteral("HEX input (e.g. 01 03 00 00 00 0A):"), page));
    m_crcInput = new QLineEdit(page);
    root->addWidget(m_crcInput);
    auto *button = new QPushButton(QStringLiteral("Calculate CRC"), page);
    button->setObjectName(QStringLiteral("accent"));
    root->addWidget(button);
    m_crcOutput = new QPlainTextEdit(page);
    m_crcOutput->setReadOnly(true);
    root->addWidget(m_crcOutput, 1);
    connect(button, &QPushButton::clicked, this, &ToolsDialog::calculateCrc);
    return page;
}

QWidget *ToolsDialog::createShaTab()
{
    auto *page = new QWidget(this);
    auto *root = new QVBoxLayout(page);
    m_shaHex = new QCheckBox(QStringLiteral("Parse input as HEX"), page);
    root->addWidget(m_shaHex);
    m_shaInput = new QPlainTextEdit(page);
    root->addWidget(m_shaInput, 1);
    auto *row = new QHBoxLayout;
    m_shaCalculateButton = new QPushButton(QStringLiteral("Calculate SHA256"), page);
    m_shaCalculateButton->setObjectName(QStringLiteral("accent"));
    m_fileShaButton = new QPushButton(QStringLiteral("Hash File…"), page);
    row->addWidget(m_shaCalculateButton);
    row->addWidget(m_fileShaButton);
    row->addStretch();
    root->addLayout(row);
    m_shaOutput = new QLineEdit(page);
    m_shaOutput->setReadOnly(true);
    root->addWidget(m_shaOutput);
    connect(m_shaCalculateButton, &QPushButton::clicked, this, &ToolsDialog::calculateSha);
    connect(m_fileShaButton, &QPushButton::clicked, this, &ToolsDialog::calculateFileSha);
    return page;
}

QWidget *ToolsDialog::createTimestampTab()
{
    auto *page = new QWidget(this);
    auto *root = new QVBoxLayout(page);
    auto *toDate = new QGroupBox(QStringLiteral("Unix Timestamp → Date & Time"), page);
    auto *first = new QFormLayout(toDate);
    m_timestampInput = new QLineEdit(toDate);
    m_timestampInput->setPlaceholderText(QStringLiteral("Seconds or milliseconds (auto-detected)"));
    auto *timestampInputRow = new QWidget(toDate);
    auto *timestampInputLayout = new QHBoxLayout(timestampInputRow);
    timestampInputLayout->setContentsMargins(0, 0, 0, 0);
    timestampInputLayout->addWidget(m_timestampInput, 1);
    auto *currentTimestamp = new QPushButton(QStringLiteral("Now"), timestampInputRow);
    timestampInputLayout->addWidget(currentTimestamp);
    auto *convert1 = new QPushButton(QStringLiteral("Convert"), toDate);
    m_localTime = new QLineEdit(toDate); m_localTime->setReadOnly(true);
    m_utcTime = new QLineEdit(toDate); m_utcTime->setReadOnly(true);
    first->addRow(QStringLiteral("Timestamp"), timestampInputRow);
    first->addRow(convert1);
    first->addRow(QStringLiteral("Local time"), m_localTime);
    first->addRow(QStringLiteral("UTC time"), m_utcTime);
    root->addWidget(toDate);
    auto *toTimestamp = new QGroupBox(QStringLiteral("Date & Time → Unix Timestamp"), page);
    auto *second = new QFormLayout(toTimestamp);
    m_dateInput = new QLineEdit(toTimestamp);
    m_dateInput->setPlaceholderText(QStringLiteral("YYYY-MM-DD HH:MM:SS[.mmm]; ISO and slash formats also supported"));
    auto *dateInputRow = new QWidget(toTimestamp);
    auto *dateInputLayout = new QHBoxLayout(dateInputRow);
    dateInputLayout->setContentsMargins(0, 0, 0, 0);
    dateInputLayout->addWidget(m_dateInput, 1);
    auto *currentDate = new QPushButton(QStringLiteral("Now"), dateInputRow);
    dateInputLayout->addWidget(currentDate);
    auto *convert2 = new QPushButton(QStringLiteral("Convert"), toTimestamp);
    m_secondsOutput = new QLineEdit(toTimestamp); m_secondsOutput->setReadOnly(true);
    m_millisecondsOutput = new QLineEdit(toTimestamp); m_millisecondsOutput->setReadOnly(true);
    second->addRow(QStringLiteral("Date & time"), dateInputRow);
    second->addRow(convert2);
    second->addRow(QStringLiteral("Seconds"), m_secondsOutput);
    second->addRow(QStringLiteral("Milliseconds"), m_millisecondsOutput);
    root->addWidget(toTimestamp);
    root->addStretch();
    connect(convert1, &QPushButton::clicked, this, &ToolsDialog::timestampToDate);
    connect(convert2, &QPushButton::clicked, this, &ToolsDialog::dateToTimestamp);
    connect(currentTimestamp, &QPushButton::clicked, this, [this] {
        m_timestampInput->setText(QString::number(QDateTime::currentMSecsSinceEpoch()));
        timestampToDate();
    });
    connect(currentDate, &QPushButton::clicked, this, [this] {
        m_dateInput->setText(
            QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
        dateToTimestamp();
    });
    return page;
}

void ToolsDialog::asciiToHex()
{
    QString error;
    const auto data = encodeEscapes(m_ascii->toPlainText(), m_encoding->currentText(),
                                    m_escapes->isChecked(), &error);
    if (!data) { setConvertStatus(error, false); return; }
    QString text = QString::fromLatin1(data->toHex(m_spaces->isChecked() ? ' ' : '\0').toUpper());
    if (!m_spaces->isChecked()) text.remove(QChar('\0'));
    m_hex->setPlainText(text);
    setConvertStatus(QStringLiteral("ASCII → HEX: %1 bytes").arg(data->size()));
}

void ToolsDialog::hexToAscii()
{
    const auto data = HexUtils::parse(m_hex->toPlainText());
    if (!data) { setConvertStatus(QStringLiteral("Invalid HEX format"), false); return; }
    QString error;
    const auto text = decodeText(*data, m_encoding->currentText(), &error);
    if (!text) { setConvertStatus(error, false); return; }
    m_ascii->setPlainText(m_escapes->isChecked() ? displayEscapes(*text) : *text);
    setConvertStatus(QStringLiteral("HEX → ASCII: %1 bytes").arg(data->size()));
}

void ToolsDialog::setConvertStatus(const QString &text, bool success)
{
    m_convertStatus->setText(text);
    m_convertStatus->setStyleSheet(success ? QStringLiteral("color:#15803D;")
                                           : QStringLiteral("color:#DC2626;"));
}

void ToolsDialog::calculateCrc()
{
    const auto data = HexUtils::parse(m_crcInput->text());
    if (!data || data->isEmpty()) { m_crcOutput->setPlainText(QStringLiteral("Invalid input")); return; }
    const quint16 crc = crc16(*data);
    const quint8 low = crc & 0xFFU;
    const quint8 high = (crc >> 8U) & 0xFFU;
    QByteArray full = *data;
    full.append(static_cast<char>(low));
    full.append(static_cast<char>(high));
    m_crcOutput->setPlainText(QStringLiteral("CRC16    : 0x%1\nLow byte : 0x%2\nHigh byte: 0x%3\nFull frame: %4")
        .arg(crc, 4, 16, QLatin1Char('0')).arg(low, 2, 16, QLatin1Char('0'))
        .arg(high, 2, 16, QLatin1Char('0')).arg(HexUtils::spaced(full)).toUpper());
}

void ToolsDialog::calculateSha()
{
    QByteArray data;
    if (m_shaHex->isChecked()) {
        const auto parsed = HexUtils::parse(m_shaInput->toPlainText());
        if (!parsed) { m_shaOutput->setText(QStringLiteral("Invalid hexadecimal input")); return; }
        data = *parsed;
    } else {
        data = m_shaInput->toPlainText().toUtf8();
    }
    m_shaOutput->setText(QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()));
}

void ToolsDialog::calculateFileSha()
{
    if (m_shaThread)
        return;

    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose File"));
    if (path.isEmpty()) return;

    auto result = std::make_shared<FileShaResult>();
    m_shaCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_shaCancel;
    m_shaOutput->setText(QStringLiteral("Calculating…"));
    m_shaCalculateButton->setEnabled(false);
    m_fileShaButton->setEnabled(false);

    auto *worker = QThread::create([path, result, cancel] {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            result->error = file.errorString();
            return;
        }

        QCryptographicHash hash(QCryptographicHash::Sha256);
        constexpr qint64 ChunkSize = 1024 * 1024;
        while (!file.atEnd()) {
            if (cancel->load()) {
                result->cancelled = true;
                return;
            }
            const QByteArray chunk = file.read(ChunkSize);
            if (chunk.isEmpty()) {
                if (file.error() != QFileDevice::NoError)
                    result->error = file.errorString();
                break;
            }
            hash.addData(chunk);
        }
        if (cancel->load()) {
            result->cancelled = true;
            return;
        }
        if (result->error.isEmpty())
            result->digest = QString::fromLatin1(hash.result().toHex());
    });
    worker->setParent(this);
    m_shaThread = worker;

    connect(worker, &QThread::finished, this, [this, worker, result] {
        const bool closeAfterFinish = m_closePending;
        if (m_shaThread == worker) {
            m_shaThread = nullptr;
            m_shaCancel.reset();
        }
        m_shaCalculateButton->setEnabled(true);
        m_fileShaButton->setEnabled(true);
        if (!result->cancelled) {
            m_shaOutput->setText(result->error.isEmpty() ? result->digest
                                                         : result->error);
        }
        worker->deleteLater();
        if (closeAfterFinish)
            QTimer::singleShot(0, this, [this] { close(); });
    });
    worker->start();
}

void ToolsDialog::timestampToDate()
{
    bool ok = false;
    const double value = m_timestampInput->text().trimmed().toDouble(&ok);
    const long double scaled = std::abs(value) >= 1.0e11
        ? static_cast<long double>(value)
        : static_cast<long double>(value) * 1000.0L;
    if (!ok || !std::isfinite(value)
        || scaled < static_cast<long double>(std::numeric_limits<qint64>::min())
        || scaled > static_cast<long double>(std::numeric_limits<qint64>::max())) {
        m_localTime->setText(QStringLiteral("Invalid input"));
        m_utcTime->clear();
        return;
    }
    const qint64 milliseconds = static_cast<qint64>(scaled);
    const QDateTime date = QDateTime::fromMSecsSinceEpoch(milliseconds);
    if (!date.isValid()) {
        m_localTime->setText(QStringLiteral("Timestamp is outside the supported range"));
        m_utcTime->clear();
        return;
    }
    m_localTime->setText(date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    m_utcTime->setText(date.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
}

void ToolsDialog::dateToTimestamp()
{
    const QString text = m_dateInput->text().trimmed();
    const QDateTime date = parseDateTime(text);
    if (!date.isValid()) { m_secondsOutput->setText(QStringLiteral("Invalid input")); m_millisecondsOutput->clear(); return; }
    const qint64 milliseconds = date.toMSecsSinceEpoch();
    m_secondsOutput->setText(QString::number(date.toSecsSinceEpoch()));
    m_millisecondsOutput->setText(QString::number(milliseconds));
}
