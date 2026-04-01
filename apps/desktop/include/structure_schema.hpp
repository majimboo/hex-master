#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

namespace StructureSchema {

enum class Endianness {
    Little,
    Big,
};

struct FieldDefinition {
    QString type_name;
    QString name;
    QString count_token;
    QString offset_token;
    int line = 0;
};

struct RepeatDefinition;

struct StructItemDefinition {
    enum class Kind {
        Field,
        Repeat,
    };

    Kind kind = Kind::Field;
    FieldDefinition field;
    std::shared_ptr<RepeatDefinition> repeat;
};

struct RepeatDefinition {
    QString alias;
    QString source_path;
    QVector<StructItemDefinition> items;
    int line = 0;
};

struct StructDefinition {
    QString name;
    QVector<StructItemDefinition> items;
};

struct SchemaDefinition {
    Endianness endianness = Endianness::Little;
    QString root_name;
    QMap<QString, StructDefinition> structs;
};

struct ParsedNode {
    QString name;
    QString type_name;
    QString value;
    qint64 offset = 0;
    qint64 size = 0;
    QVector<ParsedNode> children;
    QMap<QString, quint64> symbols;
};

using ReadRangeCallback = std::function<QByteArray(qint64, qint64)>;
using ProgressCallback = std::function<bool(qint64 current_offset, qint64 covered_bytes)>;

bool parse_schema(const QString& text, SchemaDefinition& schema, QString* error_message = nullptr);
bool evaluate_schema(
    const SchemaDefinition& schema,
    qint64 base_offset,
    qint64 document_size,
    const ReadRangeCallback& read_range,
    ParsedNode& root_node,
    const ProgressCallback& progress_callback = {},
    QString* error_message = nullptr);

QString default_schema_template();

} // namespace StructureSchema
