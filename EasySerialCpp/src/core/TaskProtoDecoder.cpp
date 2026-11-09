#include "core/TaskProtoDecoder.h"

#include <QHash>
#include <QJsonArray>
#include <bit>

namespace {
enum class Kind { Int32, UInt32, Int64, Bytes, Message };
struct Field {
    int number;
    const char *name;
    Kind kind;
    const char *message = "";
    bool repeated = false;
    int oneof = 0;
};
using Fields = QList<Field>;
const QHash<QString, Fields> &schemas()
{
    // Keep in sync with the checked-in, user-supplied TaskBatch.proto.
    static const Fields batch{{1,"time",Kind::UInt32}, {2,"batch_id",Kind::Int64},
        {3,"ap",Kind::Int32}, {4,"cp",Kind::Int32}, {5,"ts",Kind::Message,"TaskResMO",true}};
    static const QHash<QString, Fields> table{
        {"TaskBatchResDTO", batch}, {"TaskBatchQueryEventMO", batch},
        {"TaskBatchReqDTO", {{1,"batch_id",Kind::Int64}, {2,"time",Kind::UInt32},
            {3,"err_code",Kind::Int32}, {4,"cp",Kind::Int32}}},
        {"TaskResMO", {{1,"task_id",Kind::Int64}, {2,"type",Kind::Int32},
            {3,"label",Kind::Int32}, {4,"action",Kind::Int32},
            {5,"cron_list",Kind::Message,"CronScheduleList",false,1},
            {6,"once_list",Kind::Message,"OnceScheduleList",false,1},
            {7,"data",Kind::Bytes}, {8,"sts",Kind::Int32}}},
        {"CronScheduleList", {{1,"cron",Kind::Message,"CronSchedule",true}}},
        {"CronSchedule", {{1,"cron",Kind::Int32}, {2,"times",Kind::Int32}}},
        {"OnceScheduleList", {{1,"items",Kind::Message,"OnceSchedule",true}}},
        {"OnceSchedule", {{1,"st",Kind::Int64}, {2,"et",Kind::Int64}}},
        {"TaskStatusEventMO", {{1,"batch_id",Kind::Int64}, {2,"task_id",Kind::Int64},
            {3,"time",Kind::UInt32}, {4,"sts",Kind::Int32}, {5,"err_code",Kind::Int32},
            {6,"details",Kind::Message,"ResultDetailMO",true}, {7,"seq",Kind::Int32}}},
        {"ResultDetailMO", {{1,"operating",Kind::Message,"OperatingMO",false,1},
            {2,"status",Kind::Message,"StatusMO",false,1}}},
        {"OperatingMO", {{1,"sn",Kind::Int64}, {2,"rate",Kind::Int32}}},
        {"StatusMO", {{1,"sn",Kind::Int64}, {2,"code",Kind::Int32}}}
    };
    return table;
}

QString jsonName(const char *name)
{
    QString output;
    bool upper = false;
    for (const char c : QByteArray(name)) {
        if (c == '_') { upper = true; continue; }
        output += upper ? QChar::fromLatin1(c).toUpper() : QChar::fromLatin1(c);
        upper = false;
    }
    return output;
}

class Reader {
public:
    Reader(const QByteArray &bytes, qsizetype start, bool defaults, TaskProtoDecoder::Result &result)
        : m_bytes(bytes), m_pos(start), m_defaults(defaults), m_result(result) {}

    bool message(const QString &type, qsizetype end, int depth, QJsonObject &output)
    {
        if (depth > 32) return fail("Nesting exceeds 32 levels");
        const auto schema = schemas().constFind(type);
        if (schema == schemas().cend()) return fail("Unknown message type: " + type);
        QHash<QString, QJsonArray> repeated;
        for (const auto &field : *schema) if (field.repeated) {
            const QString name = jsonName(field.name);
            repeated.insert(name, output.take(name).toArray());
        }
        QJsonArray unknown = output.take("_unknownFields").toArray();
        while (m_pos < end) {
            const qsizetype start = m_pos;
            int number = 0, wire = 0;
            if (!key(end, number, wire)) return false;
            const Field *field = nullptr;
            for (const auto &candidate : *schema)
                if (candidate.number == number) { field = &candidate; break; }
            if (!field) {
                if (!skip(number, wire, end, depth)) return false;
                unknown.append(QJsonObject{{"fieldNumber", number}, {"wireType", wire},
                    {"offset", qint64(start)}, {"encodedHex", QString::fromLatin1(m_bytes.sliced(start, m_pos - start).toHex())}});
                continue;
            }
            const bool lengthDelimited = field->kind == Kind::Message || field->kind == Kind::Bytes;
            if (wire != (lengthDelimited ? 2 : 0))
                return fail(QString("Wrong wire type for %1.%2 (field %3)").arg(type, field->name).arg(number));
            ++m_result.knownFields;
            const QString name = jsonName(field->name);
            QJsonValue value;
            if (lengthDelimited) {
                qsizetype limit = 0;
                if (!lengthEnd(end, limit)) return false;
                if (field->kind == Kind::Bytes) {
                    value = QString::fromLatin1(m_bytes.sliced(m_pos, limit - m_pos).toHex());
                    m_pos = limit;
                } else {
                    // Singular embedded messages merge across duplicate tags.
                    // A different oneof member starts fresh and clears the old.
                    QJsonObject child = field->repeated ? QJsonObject{} : output.value(name).toObject();
                    if (!message(QString::fromLatin1(field->message), limit, depth + 1, child)) return false;
                    value = child;
                }
            } else {
                quint64 integer = 0;
                if (!varint(end, integer)) return false;
                if (field->kind == Kind::Int64) value = QString::number(std::bit_cast<qint64>(integer));
                else if (field->kind == Kind::UInt32) value = qint64(quint32(integer));
                else value = std::bit_cast<qint32>(quint32(integer));
            }
            if (field->oneof) for (const auto &other : *schema)
                if (other.oneof == field->oneof && other.number != number) output.remove(jsonName(other.name));
            if (field->repeated) repeated[name].append(value);
            else output.insert(name, value); // Last scalar occurrence wins.
        }
        for (const auto &field : *schema) {
            const QString name = jsonName(field.name);
            if (field.repeated) {
                if (m_defaults || !repeated[name].isEmpty()) output.insert(name, repeated[name]);
            } else if (m_defaults && !field.oneof && field.kind != Kind::Message && !output.contains(name)) {
                if (field.kind == Kind::Int64) output.insert(name, "0");
                else if (field.kind == Kind::Bytes) output.insert(name, "");
                else output.insert(name, 0);
            }
        }
        if (!unknown.isEmpty()) {
            output.insert("_unknownFields", unknown);
            m_hasUnknown = true;
        }
        return true;
    }
    bool hasUnknown() const { return m_hasUnknown; }

private:
    bool fail(const QString &reason)
    {
        m_result.error = QString("%1 at byte %2 (offset from start of input).").arg(reason).arg(m_pos);
        return false;
    }
    bool varint(qsizetype end, quint64 &value)
    {
        value = 0;
        for (int i = 0; i < 10; ++i) {
            if (m_pos >= end) return fail("Truncated varint");
            const auto byte = static_cast<quint8>(m_bytes[m_pos++]);
            if (i == 9 && byte > 1) return fail("Varint exceeds 64 bits");
            value |= quint64(byte & 0x7f) << (i * 7);
            if (!(byte & 0x80)) return true;
        }
        return fail("Invalid varint");
    }
    bool key(qsizetype end, int &number, int &wire)
    {
        if (++m_fields > 8192) return fail("Too many fields (limit 8192)");
        quint64 tag = 0;
        if (!varint(end, tag)) return false;
        if ((tag >> 3) == 0 || (tag >> 3) > 0x1fffffff) return fail("Invalid field number");
        number = int(tag >> 3);
        wire = int(tag & 7);
        if (wire > 5) return fail("Invalid wire type");
        return true;
    }
    bool lengthEnd(qsizetype end, qsizetype &limit)
    {
        quint64 length = 0;
        if (!varint(end, length)) return false;
        if (length > quint64(end - m_pos)) return fail("Length-delimited field extends past the message");
        limit = m_pos + qsizetype(length);
        return true;
    }
    bool skip(int field, int wire, qsizetype end, int depth)
    {
        if (depth > 32) return fail("Nesting exceeds 32 levels");
        quint64 ignored = 0;
        qsizetype limit = 0;
        if (wire == 0) return varint(end, ignored);
        if (wire == 2) {
            if (!lengthEnd(end, limit)) return false;
            m_pos = limit;
            return true;
        }
        if (wire == 1 || wire == 5) {
            const int count = wire == 1 ? 8 : 4;
            if (end - m_pos < count) return fail("Truncated fixed-width field");
            m_pos += count;
            return true;
        }
        if (wire == 3) { // Preserve valid unknown legacy groups without guessing types.
            while (m_pos < end) {
                int nested = 0, kind = 0;
                if (!key(end, nested, kind)) return false;
                if (kind == 4) return nested == field ? true : fail("Mismatched end-group tag");
                if (!skip(nested, kind, end, depth + 1)) return false;
            }
            return fail("Unterminated group");
        }
        return fail("Unexpected end-group tag");
    }
    const QByteArray &m_bytes;
    qsizetype m_pos;
    bool m_defaults;
    TaskProtoDecoder::Result &m_result;
    int m_fields = 0;
    bool m_hasUnknown = false;
};
}

QStringList TaskProtoDecoder::messageTypes()
{
    auto types = schemas().keys();
    types.sort();
    return types;
}

QList<qsizetype> TaskProtoDecoder::markerOffsets(const QByteArray &data)
{
    QList<qsizetype> result;
    if (data.size() > MaximumBytes) return result;
    const QByteArray marker = QByteArray::fromHex("0000ff");
    for (qsizetype pos = data.indexOf(marker); pos >= 0; pos = data.indexOf(marker, pos + 1)) result.append(pos);
    return result;
}

TaskProtoDecoder::Result TaskProtoDecoder::decode(const QByteArray &data, const Options &options)
{
    Result result;
    if (data.size() > MaximumBytes) { result.error = "Input exceeds 256 KiB."; return result; }
    if (options.framed) {
        const auto offsets = markerOffsets(data);
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
    Reader reader(data, result.payloadOffset, options.includeDefaults, result);
    QJsonObject message;
    result.ok = reader.message(options.messageType, data.size(), 0, message);
    if (result.ok) {
        result.message = message;
        if (reader.hasUnknown()) result.warnings.append("Unknown fields preserved in _unknownFields; check the selected message type/schema version.");
        if (!data.sliced(result.payloadOffset).isEmpty() && result.knownFields == 0)
            result.warnings.append("No fields matched the selected message type.");
    }
    return result;
}
