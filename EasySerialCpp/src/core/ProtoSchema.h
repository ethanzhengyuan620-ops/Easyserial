#pragma once

#include <memory>
#include "core/TaskProtoDecoder.h"

// A separate descriptor pool per entry file also allows incompatible versions
// of the same fully-qualified message name to coexist in the file selector.
class ProtoSchema final
{
public:
    ProtoSchema();
    ~ProtoSchema();
    ProtoSchema(const ProtoSchema &) = delete;
    ProtoSchema &operator=(const ProtoSchema &) = delete;
    bool load(const QString &entryFile, const QStringList &includeFolders, QString *error);
    QStringList messageTypes() const;
    QStringList warnings() const;
    TaskProtoDecoder::Result decode(const QByteArray &data, const TaskProtoDecoder::Options &options) const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
