#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/unknown_field_set.h>

#include "core/ProtoSchema.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <algorithm>

namespace proto = google::protobuf;
namespace {
QString text(absl::string_view value) { return QString::fromUtf8(value.data(), qsizetype(value.size())); }
constexpr int MaximumFields = 8192;

class Errors final : public proto::compiler::MultiFileErrorCollector
{
public:
    QStringList errors, warnings;
    void RecordError(absl::string_view file, int line, int column, absl::string_view message) override
    { append(errors, file, line, column, message); }
    void RecordWarning(absl::string_view file, int line, int column, absl::string_view message) override
    { append(warnings, file, line, column, message); }
private:
    static void append(QStringList &list, absl::string_view file, int line, int column, absl::string_view message)
    {
        if (list.size() < 20) list << QString("%1:%2:%3: %4").arg(text(file)).arg(line + 1).arg(column + 1).arg(text(message));
    }
};

// QFile handles Windows Unicode paths. Imports may only read .proto files
// beneath the explicitly selected roots (no traversal, network, or execution).
class Sources final : public proto::compiler::SourceTree
{
public:
    QStringList roots;
    QString entryName, entryPath, error;
    std::vector<std::unique_ptr<QByteArray>> buffers;
    qsizetype total = 0;
    proto::io::ZeroCopyInputStream *Open(absl::string_view name) override
    {
        const QString relative = text(name);
        if (relative.isEmpty() || relative.contains('\\') || relative.contains(':')
            || QDir::isAbsolutePath(relative) || !relative.endsWith(".proto", Qt::CaseInsensitive)
            || relative.split('/').contains("..") || relative.split('/').contains(".")) {
            error = "Imports must be relative .proto paths without '..'.";
            return nullptr;
        }
        QString path;
        if (relative == entryName) path = entryPath;
        else {
            for (const auto &root : roots) {
                const QString candidate = QFileInfo(QDir(root).filePath(relative)).canonicalFilePath();
                const QString prefix = root.endsWith('/') ? root : root + '/';
                if (!candidate.isEmpty() && candidate.startsWith(prefix, Qt::CaseInsensitive)) {
                    path = candidate;
                    break;
                }
            }
            if (path.isEmpty() && relative.startsWith("google/protobuf/")) path = ":/protocols/" + relative;
        }
        QFile file(path);
        if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
            error = "File not found. Add its import root in Include folders.";
            return nullptr;
        }
        if (buffers.size() >= 128 || file.size() > 2 * 1024 * 1024 || total + file.size() > 8 * 1024 * 1024) {
            error = "Schema limit: 128 files, 2 MiB per file, 8 MiB total.";
            return nullptr;
        }
        auto buffer = std::make_unique<QByteArray>(file.read(2 * 1024 * 1024 + 1));
        if (file.error() != QFileDevice::NoError || buffer->size() > 2 * 1024 * 1024 || total + buffer->size() > 8 * 1024 * 1024) {
            error = "Unable to read schema, or schema exceeds 2 MiB.";
            return nullptr;
        }
        total += buffer->size();
        auto *stream = new proto::io::ArrayInputStream(buffer->constData(), int(buffer->size()));
        buffers.push_back(std::move(buffer));
        return stream;
    }
    std::string GetLastErrorMessage() override { return error.toStdString(); }
};

bool addTypes(const proto::Descriptor *descriptor, QStringList &types, int depth = 0)
{
    if (depth > 32 || types.size() >= 4096) return false;
    if (!descriptor->options().map_entry()) types << text(descriptor->full_name());
    for (int i = 0; i < descriptor->nested_type_count(); ++i)
        if (!addTypes(descriptor->nested_type(i), types, depth + 1)) return false;
    return true;
}
bool addFile(const proto::FileDescriptor *file, QStringList &types, QSet<QString> &seen)
{
    const QString name = text(file->name());
    if (seen.contains(name)) return true;
    seen.insert(name);
    for (int i = 0; i < file->message_type_count(); ++i)
        if (!addTypes(file->message_type(i), types)) return false;
    for (int i = 0; i < file->dependency_count(); ++i)
        if (!text(file->dependency(i)->name()).startsWith("google/protobuf/") && !addFile(file->dependency(i), types, seen)) return false;
    return true;
}

class JsonWriter
{
public:
    bool defaults;
    TaskProtoDecoder::Result &result;
    int fields = 0;
    bool unknown = false;
    bool count(int n = 1)
    {
        fields += n;
        if (fields <= MaximumFields) return true;
        result.error = "Decoded output exceeds 8192 fields/items. Use a smaller packet or disable defaults.";
        return false;
    }
    QJsonValue scalar(const proto::Message &message, const proto::FieldDescriptor *field, int index, int depth)
    {
        const auto *r = message.GetReflection();
        const bool repeated = index >= 0;
#define PB_VALUE(Type) (repeated ? r->GetRepeated##Type(message, field, index) : r->Get##Type(message, field))
        switch (field->cpp_type()) {
        case proto::FieldDescriptor::CPPTYPE_INT32: return PB_VALUE(Int32);
        case proto::FieldDescriptor::CPPTYPE_UINT32: return double(PB_VALUE(UInt32));
        case proto::FieldDescriptor::CPPTYPE_INT64: return QString::number(PB_VALUE(Int64));
        case proto::FieldDescriptor::CPPTYPE_UINT64: return QString::number(PB_VALUE(UInt64));
        case proto::FieldDescriptor::CPPTYPE_BOOL: return PB_VALUE(Bool);
        case proto::FieldDescriptor::CPPTYPE_ENUM: return PB_VALUE(EnumValue); // Numeric enums, including unknown values.
        case proto::FieldDescriptor::CPPTYPE_FLOAT:
        case proto::FieldDescriptor::CPPTYPE_DOUBLE: {
            const double number = field->cpp_type() == proto::FieldDescriptor::CPPTYPE_FLOAT ? PB_VALUE(Float) : PB_VALUE(Double);
            if (std::isnan(number)) return "NaN";
            if (std::isinf(number)) return number > 0 ? "Infinity" : "-Infinity";
            return number;
        }
        case proto::FieldDescriptor::CPPTYPE_STRING: {
            const auto value = PB_VALUE(String);
            return field->type() == proto::FieldDescriptor::TYPE_BYTES
                ? QString::fromLatin1(QByteArray(value.data(), qsizetype(value.size())).toHex()) : text(value);
        }
        case proto::FieldDescriptor::CPPTYPE_MESSAGE: return object(PB_VALUE(Message), depth + 1);
        }
#undef PB_VALUE
        return {};
    }
    QJsonObject object(const proto::Message &message, int depth = 0)
    {
        QJsonObject out;
        if (depth > 32) { result.error = "Nesting exceeds 32 levels."; return out; }
        const auto *r = message.GetReflection();
        const auto *d = message.GetDescriptor();
        std::vector<const proto::FieldDescriptor *> present;
        r->ListFields(message, &present);
        auto selected = present;
        if (defaults) {
            for (int i = 0; i < d->field_count(); ++i) {
                const auto *field = d->field(i);
                if ((field->is_repeated() || field->cpp_type() != proto::FieldDescriptor::CPPTYPE_MESSAGE)
                    && !field->containing_oneof() && std::find(selected.begin(), selected.end(), field) == selected.end())
                    selected.push_back(field);
            }
        }
        for (const auto *field : selected) {
            if (!count()) return {};
            result.knownFields += int(std::find(present.begin(), present.end(), field) != present.end());
            const QString key = field->is_extension() ? '[' + text(field->full_name()) + ']' : text(field->json_name());
            if (field->is_map()) {
                QJsonObject map;
                const auto *entry = field->message_type();
                const int size = r->FieldSize(message, field);
                if (!count(size)) return {};
                for (int i = 0; i < size; ++i) {
                    const auto &item = r->GetRepeatedMessage(message, field, i);
                    const auto mapKey = scalar(item, entry->map_key(), -1, depth);
                    const QString name = mapKey.isBool() ? (mapKey.toBool() ? "true" : "false") : mapKey.toVariant().toString();
                    map.insert(name, scalar(item, entry->map_value(), -1, depth));
                }
                out.insert(key, map);
            } else if (field->is_repeated()) {
                QJsonArray values;
                const int size = r->FieldSize(message, field);
                if (!count(size)) return {};
                for (int i = 0; i < size; ++i) values.append(scalar(message, field, i, depth));
                out.insert(key, values);
            } else out.insert(key, scalar(message, field, -1, depth));
            if (!result.error.isEmpty()) return {};
        }
        const auto &extra = r->GetUnknownFields(message);
        if (extra.field_count()) {
            unknown = true;
            if (!count(extra.field_count())) return {};
            QJsonArray values;
            for (int i = 0; i < extra.field_count(); ++i) {
                const auto &field = extra.field(i);
                proto::UnknownFieldSet one;
                one.AddField(field);
                std::string encoded;
                if (!one.SerializeToString(&encoded)) {
                    result.error = "Unable to serialize unknown-field diagnostics.";
                    return {};
                }
                const int wireTypes[] = {0, 5, 1, 2, 3};
                values.append(QJsonObject{{"fieldNumber", field.number()}, {"wireType", wireTypes[field.type()]},
                    {"encodedHex", QString::fromLatin1(QByteArray(encoded.data(), qsizetype(encoded.size())).toHex())}});
            }
            if (!out.contains("_unknownFields")) out.insert("_unknownFields", values);
            else result.warnings << "Unknown-field details omitted: schema uses reserved JSON name _unknownFields.";
        }
        return out;
    }
};
}

struct ProtoSchema::Impl
{
    Sources sources;
    Errors errors;
    proto::compiler::Importer importer{&sources, &errors};
    mutable proto::DynamicMessageFactory factory{importer.pool()};
    QStringList types;
};

ProtoSchema::ProtoSchema() = default;
ProtoSchema::~ProtoSchema() = default;

bool ProtoSchema::load(const QString &entryFile, const QStringList &includeFolders, QString *error)
{
    if (error) error->clear();
    auto candidate = std::make_unique<Impl>();
    auto &sources = candidate->sources;
    sources.entryPath = entryFile.startsWith(":/") ? entryFile : QFileInfo(entryFile).canonicalFilePath();
    if (sources.entryPath.isEmpty()) {
        if (error) *error = "Schema file not found: " + entryFile;
        return false;
    }
    for (const auto &folder : includeFolders + QStringList{QFileInfo(sources.entryPath).absolutePath()}) {
        const QString canonical = QFileInfo(folder).canonicalFilePath();
        if (!canonical.isEmpty() && QFileInfo(canonical).isDir() && !sources.roots.contains(canonical)) sources.roots << canonical;
    }
    sources.entryName = QFileInfo(sources.entryPath).fileName();
    for (const auto &root : sources.roots) {
        const QString prefix = root.endsWith('/') ? root : root + '/';
        if (sources.entryPath.startsWith(prefix, Qt::CaseInsensitive)) {
            sources.entryName = QDir(root).relativeFilePath(sources.entryPath);
            break;
        }
    }
    const auto *file = candidate->importer.Import(sources.entryName.toStdString());
    if (!file) {
        if (error) *error = candidate->errors.errors.isEmpty() ? sources.error : candidate->errors.errors.join('\n');
        return false;
    }
    QSet<QString> seen;
    if (!addFile(file, candidate->types, seen)) {
        if (error) *error = "Schema exceeds 4096 messages or 32 levels of nested declarations.";
        return false;
    }
    candidate->types.sort();
    m_impl = std::move(candidate); // Transactional: failed reload leaves the previous schema valid.
    return true;
}

QStringList ProtoSchema::messageTypes() const { return m_impl ? m_impl->types : QStringList{}; }
QStringList ProtoSchema::warnings() const { return m_impl ? m_impl->errors.warnings : QStringList{}; }

TaskProtoDecoder::Result ProtoSchema::decode(const QByteArray &data, const TaskProtoDecoder::Options &options) const
{
    TaskProtoDecoder::Result result;
    if (!m_impl) { result.error = "Import a valid .proto file first."; return result; }
    if (data.size() > TaskProtoDecoder::MaximumBytes) { result.error = "Input exceeds 256 KiB."; return result; }
    if (options.framed) {
        const auto offsets = TaskProtoDecoder::markerOffsets(data);
        qsizetype offset = options.markerOffset;
        if (offset < 0) {
            if (offsets.size() != 1) {
                result.error = offsets.isEmpty() ? "Marker 00 00 FF not found. Use Raw PB for a payload without a header."
                    : "Multiple 00 00 FF markers found. Select an explicit marker offset.";
                return result;
            }
            offset = offsets.first();
        } else if (!offsets.contains(offset)) {
            result.error = "Selected marker offset does not point to 00 00 FF.";
            return result;
        }
        result.payloadOffset = offset + 3;
        result.skippedPrefix = data.first(result.payloadOffset);
        if (result.payloadOffset == data.size()) { result.error = "Empty PB payload after marker."; return result; }
    }
    const auto *descriptor = m_impl->importer.pool()->FindMessageTypeByName(options.messageType.toStdString());
    if (!descriptor || descriptor->options().map_entry()) { result.error = "Unknown message type: " + options.messageType; return result; }
    std::unique_ptr<proto::Message> message(m_impl->factory.GetPrototype(descriptor)->New());
    proto::io::CodedInputStream stream(reinterpret_cast<const uint8_t *>(data.constData() + result.payloadOffset), int(data.size() - result.payloadOffset));
    stream.SetTotalBytesLimit(int(TaskProtoDecoder::MaximumBytes));
    stream.SetRecursionLimit(32);
    if (!message->ParsePartialFromCodedStream(&stream) || !stream.ConsumedEntireMessage()) {
        result.error = QString("Invalid/truncated PB or nesting exceeds 32 levels (near payload byte %1).").arg(stream.CurrentPosition());
        return result;
    }
    if (!message->IsInitialized()) result.warnings << "Missing required fields: " + text(message->InitializationErrorString());
    JsonWriter writer{options.includeDefaults, result};
    const auto json = writer.object(*message);
    if (!result.error.isEmpty()) return result;
    result.message = json;
    result.ok = true;
    if (writer.unknown) result.warnings << "Unknown fields preserved in _unknownFields; check the message type/schema version.";
    if (result.knownFields == 0 && data.size() > result.payloadOffset)
        result.warnings << "No populated fields matched this message type (or all known values equal defaults).";
    return result;
}
