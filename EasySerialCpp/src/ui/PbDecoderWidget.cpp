#include "ui/PbDecoderWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFont>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QSignalBlocker>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include "core/HexUtils.h"
#include "core/TaskProtoDecoder.h"
#include "core/ProtoSchema.h"
#include "Settings.h"

PbDecoderWidget::PbDecoderWidget(Settings &settings, QWidget *parent) : QWidget(parent), m_settings(settings)
{
    setObjectName("pbDecoder");
    auto *root = new QVBoxLayout(this);
    auto *files = new QHBoxLayout;
    files->addWidget(new QLabel("Protocol", this));
    m_file = new QComboBox(this);
    m_file->setObjectName("pbProtocolFile");
    m_file->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_file->setMinimumContentsLength(18);
    m_file->addItem("TaskBatch.proto (example)", ":/protocols/TaskBatch.proto");
    for (const auto &item : m_settings.arrayValue("pb_proto_files")) {
        const QString path = item.toString();
        if (!path.isEmpty() && QFileInfo(path).isAbsolute() && m_file->findData(path) < 0 && m_file->count() < 129) {
            m_file->addItem(QFileInfo(path).fileName() + " — " + QDir::toNativeSeparators(QFileInfo(path).absolutePath()), path);
            m_file->setItemData(m_file->count() - 1, path, Qt::ToolTipRole);
        }
    }
    for (const auto &item : m_settings.arrayValue("pb_include_folders"))
        if (!item.toString().isEmpty() && m_includeFolders.size() < 16) m_includeFolders << item.toString();
    files->addWidget(m_file, 1);
    auto *import = new QPushButton("Import .proto…", this);
    import->setObjectName("pbImportProto");
    auto *reload = new QPushButton("Reload", this);
    reload->setObjectName("pbReloadProto");
    reload->setToolTip("Reload the selected protocol and its imports after editing them");
    m_remove = new QPushButton("Remove", this);
    m_remove->setObjectName("pbRemoveProto");
    m_remove->setToolTip("Remove from this list only; never deletes the file on disk");
    for (auto *button : {import, reload, m_remove}) files->addWidget(button);
    root->addLayout(files);
    auto *schema = new QHBoxLayout;
    schema->addWidget(new QLabel("Message", this));
    m_type = new QComboBox(this);
    m_type->setObjectName("pbMessageType");
    m_type->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_type->setMinimumContentsLength(18);
    schema->addWidget(m_type, 1);
    auto *include = new QPushButton("Include folders…", this);
    include->setObjectName("pbIncludeFolders");
    include->setToolTip("Add the project root used by import paths, e.g. the folder containing common/ for import common/types.proto");
    schema->addWidget(include);
    m_defaults = new QCheckBox("Include defaults", this);
    m_defaults->setObjectName("pbDefaults");
    m_defaults->setChecked(true);
    schema->addWidget(m_defaults);
    root->addLayout(schema);

    auto *framing = new QHBoxLayout;
    framing->addWidget(new QLabel("Start", this));
    m_mode = new QComboBox(this);
    m_mode->setObjectName("pbStartMode");
    m_mode->addItems({"After 00 00 FF (auto)", "After 00 00 FF (offset)", "Raw PB (no header)"});
    framing->addWidget(m_mode, 1);
    framing->addWidget(new QLabel("Marker byte offset", this));
    m_offset = new QSpinBox(this);
    m_offset->setObjectName("pbMarkerOffset");
    m_offset->setRange(0, int(TaskProtoDecoder::MaximumBytes - 3));
    m_offset->setToolTip("Zero-based byte offset of the selected 00 00 FF marker, not a HEX character index");
    m_offset->setEnabled(false);
    framing->addWidget(m_offset);
    root->addLayout(framing);

    m_schemaStatus = new QLabel(this);
    m_schemaStatus->setObjectName("pbSchemaStatus");
    m_schemaStatus->setTextFormat(Qt::PlainText);
    m_schemaStatus->setWordWrap(true);
    m_schemaStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_schemaStatus);
    auto *hint = new QLabel("Diagnostic JSON: bytes = HEX, 64-bit integers = strings, enums = numbers. "
        "Headers stay separate; bytes fields are not guessed as nested PB.", this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#64748B;");
    root->addWidget(hint);

    auto *panes = new QSplitter(Qt::Horizontal, this);
    panes->setObjectName("pbPanes");
    const auto pane = [panes](const QString &title, const QString &name, bool readOnly) {
        auto *container = new QWidget(panes);
        auto *layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(new QLabel(title, container));
        auto *edit = new QPlainTextEdit(container);
        edit->setObjectName(name);
        edit->setReadOnly(readOnly);
        edit->setFont(QFont("Consolas", 9));
        edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        layout->addWidget(edit, 1);
        panes->addWidget(container);
        return edit;
    };
    m_input = pane("Raw packet (HEX)", "pbInput", false);
    m_input->setPlaceholderText("Paste HEX here. Spaces, newlines and 0x prefixes are accepted.\nMaximum decoded size: 256 KiB.");
    m_output = pane("Decoded result (JSON)", "pbOutput", true);
    m_output->setUndoRedoEnabled(false);
    panes->setChildrenCollapsible(false);
    panes->setSizes({320, 380});
    root->addWidget(panes, 1);
    m_prefix = new QLineEdit(this);
    m_prefix->setObjectName("pbSkippedPrefix");
    m_prefix->setReadOnly(true);
    m_prefix->setMaxLength(int(TaskProtoDecoder::MaximumBytes * 2));
    m_prefix->setPlaceholderText("Skipped prefix (HEX, including marker; not a PB field)");
    auto *prefixRow = new QHBoxLayout;
    prefixRow->addWidget(new QLabel("Skipped prefix", this));
    prefixRow->addWidget(m_prefix, 1);
    root->addLayout(prefixRow);

    auto *actions = new QHBoxLayout;
    m_live = new QCheckBox("Auto parse", this);
    m_live->setObjectName("pbAutoParse");
    m_live->setChecked(true);
    actions->addWidget(m_live);
    actions->addStretch();
    auto *run = new QPushButton("Parse", this);
    run->setObjectName("pbParse");
    m_copy = new QPushButton("Copy JSON", this);
    m_copy->setObjectName("pbCopy");
    auto *clear = new QPushButton("Clear", this);
    clear->setObjectName("pbClear");
    for (auto *button : {run, m_copy, clear}) actions->addWidget(button);
    root->addLayout(actions);
    m_status = new QLabel(this);
    m_status->setObjectName("pbStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_status);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, &PbDecoderWidget::parse);
    connect(run, &QPushButton::clicked, this, &PbDecoderWidget::parse);
    connect(m_input, &QPlainTextEdit::textChanged, this, &PbDecoderWidget::scheduleParse);
    connect(m_type, &QComboBox::currentTextChanged, this, [this] {
        if (!m_type->currentText().isEmpty()) {
            auto messages = m_settings.value("pb_selected_messages").toMap();
            messages[m_file->currentData().toString()] = m_type->currentText();
            m_settings.setValue("pb_selected_messages", messages);
        }
        scheduleParse();
    });
    connect(import, &QPushButton::clicked, this, [this] {
        const auto paths = QFileDialog::getOpenFileNames(this, "Import Protobuf schemas",
            m_settings.stringValue("pb_import_directory"), "Protocol Buffers (*.proto)");
        if (!paths.isEmpty()) importFiles(paths);
    });
    connect(include, &QPushButton::clicked, this, &PbDecoderWidget::editIncludeFolders);
    connect(reload, &QPushButton::clicked, this, &PbDecoderWidget::loadSchema);
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const int index = m_file->currentIndex();
        if (index <= 0) return;
        auto messages = m_settings.value("pb_selected_messages").toMap();
        messages.remove(m_file->currentData().toString());
        m_settings.setValue("pb_selected_messages", messages, false);
        m_file->removeItem(index);
        saveCatalog();
    });
    connect(m_file, &QComboBox::currentIndexChanged, this, &PbDecoderWidget::loadSchema);
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_offset->setEnabled(index == 1);
        scheduleParse();
    });
    connect(m_offset, &QSpinBox::valueChanged, this, &PbDecoderWidget::scheduleParse);
    connect(m_defaults, &QCheckBox::toggled, this, &PbDecoderWidget::scheduleParse);
    connect(m_live, &QCheckBox::toggled, this, &PbDecoderWidget::scheduleParse);
    connect(m_copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_output->toPlainText());
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        m_input->clear();
        parse();
    });
    const int selected = m_file->findData(m_settings.stringValue("pb_selected_file"));
    if (selected > 0) m_file->setCurrentIndex(selected);
    else loadSchema();
}

PbDecoderWidget::~PbDecoderWidget() = default;

void PbDecoderWidget::saveCatalog()
{
    QJsonArray paths, folders;
    for (int i = 1; i < m_file->count(); ++i) paths.append(m_file->itemData(i).toString());
    for (const auto &path : m_includeFolders) folders.append(path);
    m_settings.setArray("pb_proto_files", paths, false);
    m_settings.setArray("pb_include_folders", folders, false);
    m_settings.setValue("pb_selected_file", m_file->currentData(), false);
    m_settings.save();
}

void PbDecoderWidget::importFiles(const QStringList &paths)
{
    int selected = m_file->currentIndex();
    bool limit = false;
    {
        const QSignalBlocker blocker(m_file);
        for (const auto &input : paths) {
            if (!input.endsWith(".proto", Qt::CaseInsensitive)) continue;
            const QString path = QFileInfo(input).absoluteFilePath();
            const int existing = m_file->findData(path);
            if (existing >= 0) { selected = existing; continue; }
            if (m_file->count() >= 129) { limit = true; break; }
            m_file->addItem(QFileInfo(path).fileName() + " — " + QDir::toNativeSeparators(QFileInfo(path).absolutePath()), path);
            selected = m_file->count() - 1;
            m_file->setItemData(selected, path, Qt::ToolTipRole);
        }
        m_file->setCurrentIndex(selected);
    }
    if (selected > 0) m_settings.setValue("pb_import_directory", QFileInfo(m_file->currentData().toString()).absolutePath(), false);
    loadSchema();
    if (limit) m_schemaStatus->setText(m_schemaStatus->text() + "\nFile list limit: 128. Remove unused entries first.");
}

void PbDecoderWidget::setIncludeFolders(const QStringList &paths)
{
    m_includeFolders.clear();
    for (const auto &path : paths) {
        const QString folder = QDir(path).absolutePath();
        if (!path.isEmpty() && !m_includeFolders.contains(folder) && m_includeFolders.size() < 16) m_includeFolders << folder;
    }
    loadSchema();
}

void PbDecoderWidget::editIncludeFolders()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Proto include folders");
    dialog.resize(640, 320);
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel("Imports are resolved from these roots in order, then from imported files' folders.\n"
        "For import \"common/types.proto\", add the folder containing common.\n"
        "Standard google/protobuf imports are included in EasySerial.", &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *list = new QListWidget(&dialog);
    list->addItems(m_includeFolders);
    layout->addWidget(list, 1);
    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton("Add folder…", &dialog);
    auto *remove = new QPushButton("Remove", &dialog);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addLayout(buttons);
    auto *confirm = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(confirm);
    connect(add, &QPushButton::clicked, &dialog, [&] {
        if (list->count() >= 16) return;
        const QString path = QFileDialog::getExistingDirectory(&dialog, "Select proto import root");
        if (!path.isEmpty() && list->findItems(path, Qt::MatchExactly).isEmpty()) list->addItem(path);
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] { delete list->takeItem(list->currentRow()); });
    connect(confirm, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(confirm, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        QStringList paths;
        for (int i = 0; i < list->count(); ++i) paths << list->item(i)->text();
        setIncludeFolders(paths);
    }
}

void PbDecoderWidget::loadSchema()
{
    m_timer->stop();
    m_schema.reset();
    m_schemaError.clear();
    const QString path = m_file->currentData().toString();
    m_file->setToolTip(path);
    m_remove->setEnabled(m_file->currentIndex() > 0);
    QStringList roots = m_includeFolders;
    if (m_file->currentIndex() > 0) roots << QFileInfo(path).absolutePath();
    for (int i = 1; i < m_file->count(); ++i) roots << QFileInfo(m_file->itemData(i).toString()).absolutePath();
    roots.removeDuplicates();
    auto schema = std::make_unique<ProtoSchema>();
    const bool ok = schema->load(path, roots, &m_schemaError);
    {
        const QSignalBlocker blocker(m_type);
        m_type->clear();
        if (ok) {
            m_type->addItems(schema->messageTypes());
            const QString previous = m_settings.value("pb_selected_messages").toMap().value(path,
                path.startsWith(":/") ? "TaskBatchResDTO" : "").toString();
            const int index = m_type->findText(previous);
            if (index >= 0) m_type->setCurrentIndex(index);
            m_schema = std::move(schema);
            if (m_type->count() == 0) m_schemaError = "This schema defines no messages. Import a file containing a message.";
        }
    }
    m_type->setEnabled(m_type->count() > 0);
    m_schemaStatus->setText(m_schemaError.isEmpty()
        ? QString("Loaded %1 messages. Import more .proto files to switch protocols; Reload after editing.").arg(m_type->count())
        : "Schema error: " + m_schemaError.first(1200));
    m_schemaStatus->setToolTip(m_schemaError.isEmpty() ? path + '\n' + m_schema->warnings().join('\n') : m_schemaError);
    m_schemaStatus->setStyleSheet(m_schemaError.isEmpty() ? "color:#64748B;" : "color:#B91C1C;");
    saveCatalog();
    scheduleParse();
}

void PbDecoderWidget::scheduleParse()
{
    m_timer->stop();
    // Never leave an apparently valid result from different/previous input.
    m_output->clear();
    m_prefix->clear();
    m_prefix->setToolTip({});
    m_copy->setEnabled(false);
    m_status->setStyleSheet("color:#64748B;");
    m_status->setText(m_live->isChecked() ? "Waiting for input to settle…" : "Input changed. Click Parse.");
    if (m_live->isChecked()) m_timer->start();
}

void PbDecoderWidget::parse()
{
    m_timer->stop();
    m_output->clear();
    m_prefix->clear();
    m_prefix->setToolTip({});
    m_copy->setEnabled(false);
    const auto error = [this](const QString &text) {
        m_status->setText(text);
        m_status->setStyleSheet("color:#B91C1C;");
    };
    if (!m_schemaError.isEmpty() || !m_schema) {
        error("Select/import a valid protocol before parsing. See the schema error above.");
        return;
    }
    if (m_input->document()->characterCount() - 1 > TaskProtoDecoder::MaximumInputCharacters) {
        error("Input text exceeds 1 MiB of characters. Paste a smaller packet.");
        return;
    }
    const auto data = HexUtils::parse(m_input->toPlainText());
    if (!data) { error("Invalid HEX: use complete byte pairs (0-9, A-F)."); return; }
    if (data->isEmpty()) {
        m_status->setText("Paste a packet to decode. All processing is local.");
        m_status->setStyleSheet("color:#64748B;");
        return;
    }
    TaskProtoDecoder::Options options;
    options.messageType = m_type->currentText();
    options.includeDefaults = m_defaults->isChecked();
    options.framed = m_mode->currentIndex() != 2;
    options.markerOffset = m_mode->currentIndex() == 1 ? m_offset->value() : -1;
    const auto result = m_schema->decode(*data, options);
    if (!result.ok) {
        QString message = result.error;
        if (result.error.startsWith("Multiple")) {
            QStringList candidates;
            const auto offsets = TaskProtoDecoder::markerOffsets(*data);
            for (auto offset : offsets.first(qMin<qsizetype>(12, offsets.size()))) candidates << QString::number(offset);
            message += " Byte offsets: " + candidates.join(", ") + (offsets.size() > 12 ? ", …" : "");
        }
        error(message);
        return;
    }
    m_output->setPlainText(QString::fromUtf8(QJsonDocument(result.message).toJson(QJsonDocument::Indented)));
    m_prefix->setText(QString::fromLatin1(result.skippedPrefix.toHex()));
    m_prefix->setCursorPosition(0);
    m_prefix->setToolTip("Skipped prefix (not part of the PB schema): " + m_prefix->text().first(256)
        + (m_prefix->text().size() > 256 ? "…" : ""));
    QString status = QString("Parsed %1: %2 payload bytes, starting at byte %3; %4 known fields.")
        .arg(options.messageType).arg(data->size() - result.payloadOffset).arg(result.payloadOffset).arg(result.knownFields);
    if (!result.warnings.isEmpty()) status += '\n' + result.warnings.join('\n');
    m_status->setText(status);
    m_status->setStyleSheet(result.warnings.isEmpty() ? "color:#15803D;" : "color:#92400E;");
    m_copy->setEnabled(true);
}
