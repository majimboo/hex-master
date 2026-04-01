#include "structure_schema.hpp"

#include <QDateTime>
#include <QRegularExpression>
#include <QStringList>

#include <array>
#include <cstring>
#include <limits>

namespace StructureSchema {
namespace {

struct PrimitiveInfo {
    int width = 0;
    bool is_signed = false;
    bool is_float = false;
};

bool lookup_primitive(const QString& type_name, PrimitiveInfo& info) {
    static const QMap<QString, PrimitiveInfo> kPrimitives{
        {QStringLiteral("u8"), {1, false, false}},
        {QStringLiteral("i8"), {1, true, false}},
        {QStringLiteral("u16"), {2, false, false}},
        {QStringLiteral("i16"), {2, true, false}},
        {QStringLiteral("u32"), {4, false, false}},
        {QStringLiteral("i32"), {4, true, false}},
        {QStringLiteral("u64"), {8, false, false}},
        {QStringLiteral("i64"), {8, true, false}},
        {QStringLiteral("f32"), {4, true, true}},
        {QStringLiteral("f64"), {8, true, true}},
    };
    const auto it = kPrimitives.constFind(type_name);
    if (it == kPrimitives.cend()) {
        return false;
    }
    info = it.value();
    return true;
}

QString strip_comments(const QString& input) {
    QString line = input;
    const int slash_comment = line.indexOf(QStringLiteral("//"));
    const int hash_comment = line.indexOf(QLatin1Char('#'));
    int cut = -1;
    if (slash_comment >= 0) {
        cut = slash_comment;
    }
    if (hash_comment >= 0 && (cut < 0 || hash_comment < cut)) {
        cut = hash_comment;
    }
    if (cut >= 0) {
        line = line.left(cut);
    }
    return line.trimmed();
}

bool set_error(QString* error_message, int line, const QString& message) {
    if (error_message != nullptr) {
        *error_message = line > 0
            ? QStringLiteral("Line %1: %2").arg(line).arg(message)
            : message;
    }
    return false;
}

bool resolve_token_value(const QString& token, const QMap<QString, quint64>& context, quint64& value) {
    bool ok = false;
    const quint64 numeric = token.toULongLong(&ok, 0);
    if (ok) {
        value = numeric;
        return true;
    }
    const auto it = context.constFind(token);
    if (it == context.cend()) {
        return false;
    }
    value = it.value();
    return true;
}

QString format_bytes_preview(const QByteArray& bytes) {
    QString text;
    const int shown = qMin(bytes.size(), 16);
    for (int index = 0; index < shown; ++index) {
        if (index > 0) {
            text += QLatin1Char(' ');
        }
        text += QStringLiteral("%1")
                    .arg(static_cast<quint8>(bytes.at(index)), 2, 16, QChar(u'0'))
                    .toUpper();
    }
    if (bytes.size() > shown) {
        text += QStringLiteral(" ...");
    }
    return text;
}

quint64 decode_unsigned(const QByteArray& bytes, Endianness endianness) {
    quint64 value = 0;
    if (endianness == Endianness::Little) {
        for (int index = 0; index < bytes.size(); ++index) {
            value |= static_cast<quint64>(static_cast<quint8>(bytes.at(index))) << (index * 8);
        }
        return value;
    }

    for (int index = 0; index < bytes.size(); ++index) {
        value = (value << 8) | static_cast<quint64>(static_cast<quint8>(bytes.at(index)));
    }
    return value;
}

qint64 decode_signed(const QByteArray& bytes, Endianness endianness) {
    const quint64 value = decode_unsigned(bytes, endianness);
    const int bits = bytes.size() * 8;
    if (bits <= 0 || bits >= 64) {
        return static_cast<qint64>(value);
    }
    const quint64 sign_mask = 1ULL << (bits - 1);
    if ((value & sign_mask) == 0) {
        return static_cast<qint64>(value);
    }
    const quint64 extend_mask = ~((1ULL << bits) - 1);
    return static_cast<qint64>(value | extend_mask);
}

double decode_float(const QByteArray& bytes, Endianness endianness) {
    std::array<unsigned char, 8> raw{};
    for (int index = 0; index < bytes.size(); ++index) {
        const int source_index = endianness == Endianness::Little ? index : (bytes.size() - 1 - index);
        raw[static_cast<std::size_t>(source_index)] = static_cast<unsigned char>(bytes.at(index));
    }

    if (bytes.size() == 4) {
        float value = 0.0F;
        std::memcpy(&value, raw.data(), sizeof(float));
        return static_cast<double>(value);
    }

    double value = 0.0;
    std::memcpy(&value, raw.data(), sizeof(double));
    return value;
}

struct EvaluationContext {
    const SchemaDefinition* schema = nullptr;
    qint64 document_size = 0;
    ReadRangeCallback read_range;
};

bool evaluate_struct(
    const EvaluationContext& context,
    const StructDefinition& definition,
    qint64 start_offset,
    ParsedNode& node,
    QString* error_message,
    int line_hint);

bool evaluate_field(
    const EvaluationContext& context,
    const FieldDefinition& field,
    qint64& cursor,
    QMap<QString, quint64>& local_values,
    ParsedNode& node,
    QString* error_message) {
    PrimitiveInfo primitive;
    const bool is_primitive = lookup_primitive(field.type_name, primitive);
    const bool is_bytes = field.type_name == QStringLiteral("bytes");
    const auto struct_it = context.schema->structs.constFind(field.type_name);
    const bool is_struct = struct_it != context.schema->structs.cend();
    if (!is_primitive && !is_bytes && !is_struct) {
        return set_error(error_message, field.line, QStringLiteral("Unknown type `%1`.").arg(field.type_name));
    }

    quint64 count = 1;
    if (!field.count_token.isEmpty()) {
        if (!resolve_token_value(field.count_token, local_values, count)) {
            return set_error(error_message, field.line, QStringLiteral("Unknown count source `%1`.").arg(field.count_token));
        }
    }

    quint64 offset_value = static_cast<quint64>(cursor);
    if (!field.offset_token.isEmpty()) {
        if (!resolve_token_value(field.offset_token, local_values, offset_value)) {
            return set_error(error_message, field.line, QStringLiteral("Unknown offset source `%1`.").arg(field.offset_token));
        }
    }
    const qint64 field_offset = static_cast<qint64>(offset_value);
    node.name = field.name;
    node.type_name = field.count_token.isEmpty()
        ? field.type_name
        : QStringLiteral("%1[%2]").arg(field.type_name, field.count_token);
    node.offset = field_offset;

    auto ensure_read = [&](qint64 offset, qint64 size, QByteArray& bytes) -> bool {
        if (offset < 0 || size < 0 || offset + size > context.document_size) {
            return set_error(error_message, field.line, QStringLiteral("Field `%1` extends past the end of the document.").arg(field.name));
        }
        bytes = context.read_range(offset, size);
        if (bytes.size() != size) {
            return set_error(error_message, field.line, QStringLiteral("Failed to read `%1` from the document.").arg(field.name));
        }
        return true;
    };

    if (is_bytes) {
        const qint64 size = static_cast<qint64>(count);
        QByteArray bytes;
        if (!ensure_read(field_offset, size, bytes)) {
            return false;
        }
        node.size = size;
        node.value = format_bytes_preview(bytes);
        if (field.count_token.isEmpty()) {
            local_values.insert(field.name, static_cast<quint64>(size));
        }
        if (field.offset_token.isEmpty()) {
            cursor += size;
        }
        return true;
    }

    if (is_primitive) {
        if (field.count_token.isEmpty()) {
            QByteArray bytes;
            if (!ensure_read(field_offset, primitive.width, bytes)) {
                return false;
            }
            node.size = primitive.width;
            if (primitive.is_float) {
                node.value = primitive.width == 4
                    ? QString::number(decode_float(bytes, context.schema->endianness), 'g', 9)
                    : QString::number(decode_float(bytes, context.schema->endianness), 'g', 17);
            } else if (primitive.is_signed) {
                const qint64 value = decode_signed(bytes, context.schema->endianness);
                node.value = QStringLiteral("%1 (0x%2)")
                                 .arg(value)
                                 .arg(static_cast<qulonglong>(decode_unsigned(bytes, context.schema->endianness)), primitive.width * 2, 16, QChar(u'0'))
                                 .toUpper();
                local_values.insert(field.name, static_cast<quint64>(value));
            } else {
                const quint64 value = decode_unsigned(bytes, context.schema->endianness);
                node.value = QStringLiteral("%1 (0x%2)")
                                 .arg(value)
                                 .arg(static_cast<qulonglong>(value), primitive.width * 2, 16, QChar(u'0'))
                                 .toUpper();
                local_values.insert(field.name, value);
            }

            if (!primitive.is_signed && !primitive.is_float) {
                local_values.insert(field.name, decode_unsigned(bytes, context.schema->endianness));
            }
            if (field.offset_token.isEmpty()) {
                cursor += primitive.width;
            }
            return true;
        }

        qint64 running_offset = field_offset;
        for (quint64 index = 0; index < count; ++index) {
            QByteArray bytes;
            if (!ensure_read(running_offset, primitive.width, bytes)) {
                return false;
            }

            ParsedNode child;
            child.name = QStringLiteral("[%1]").arg(index);
            child.type_name = field.type_name;
            child.offset = running_offset;
            child.size = primitive.width;
            if (primitive.is_float) {
                child.value = primitive.width == 4
                    ? QString::number(decode_float(bytes, context.schema->endianness), 'g', 9)
                    : QString::number(decode_float(bytes, context.schema->endianness), 'g', 17);
            } else if (primitive.is_signed) {
                const qint64 value = decode_signed(bytes, context.schema->endianness);
                child.value = QString::number(value);
            } else {
                child.value = QString::number(decode_unsigned(bytes, context.schema->endianness));
            }
            node.children.push_back(child);
            running_offset += primitive.width;
        }
        node.size = static_cast<qint64>(count) * primitive.width;
        node.value = QStringLiteral("%1 item(s)").arg(count);
        if (field.offset_token.isEmpty()) {
            cursor += node.size;
        }
        return true;
    }

    const StructDefinition& child_definition = struct_it.value();
    if (field.count_token.isEmpty()) {
        ParsedNode child;
        if (!evaluate_struct(context, child_definition, field_offset, child, error_message, field.line)) {
            return false;
        }
        node.children.push_back(child);
        node.size = child.size;
        node.value = QStringLiteral("%1 bytes").arg(child.size);
        if (field.offset_token.isEmpty()) {
            cursor += child.size;
        }
        return true;
    }

    qint64 running_offset = field_offset;
    for (quint64 index = 0; index < count; ++index) {
        ParsedNode child;
        if (!evaluate_struct(context, child_definition, running_offset, child, error_message, field.line)) {
            return false;
        }
        child.name = QStringLiteral("[%1]").arg(index);
        node.children.push_back(child);
        running_offset += child.size;
        node.size += child.size;
    }
    node.value = QStringLiteral("%1 item(s)").arg(count);
    if (field.offset_token.isEmpty()) {
        cursor += node.size;
    }
    return true;
}

bool evaluate_struct(
    const EvaluationContext& context,
    const StructDefinition& definition,
    qint64 start_offset,
    ParsedNode& node,
    QString* error_message,
    int line_hint) {
    node.name = definition.name;
    node.type_name = definition.name;
    node.offset = start_offset;
    node.size = 0;
    node.value.clear();
    node.children.clear();

    qint64 cursor = start_offset;
    QMap<QString, quint64> local_values;
    for (const FieldDefinition& field : definition.fields) {
        ParsedNode child;
        if (!evaluate_field(context, field, cursor, local_values, child, error_message)) {
            if (error_message != nullptr && error_message->isEmpty()) {
                *error_message = QStringLiteral("Line %1: Failed to evaluate `%2`.").arg(line_hint).arg(field.name);
            }
            return false;
        }
        node.children.push_back(child);
    }

    qint64 end_offset = start_offset;
    for (const ParsedNode& child : node.children) {
        end_offset = qMax(end_offset, child.offset + child.size);
    }
    node.size = qMax<qint64>(0, end_offset - start_offset);
    node.value = QStringLiteral("%1 bytes").arg(node.size);
    return true;
}

} // namespace

bool parse_schema(const QString& text, SchemaDefinition& schema, QString* error_message) {
    schema = SchemaDefinition{};
    StructDefinition current_struct;
    bool in_struct = false;

    const auto lines = text.split(QLatin1Char('\n'));
    const QRegularExpression struct_pattern(QStringLiteral(R"(^struct\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{$)"));
    const QRegularExpression root_pattern(QStringLiteral(R"(^root\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{$)"));
    const QRegularExpression endian_pattern(QStringLiteral(R"(^endian\s+(little|big)$)"));
    const QRegularExpression field_pattern(QStringLiteral(R"(^(bytes|u8|i8|u16|i16|u32|i32|u64|i64|f32|f64|[A-Za-z_][A-Za-z0-9_]*)(?:\[(\d+|[A-Za-z_][A-Za-z0-9_]*)\])?\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+@(\d+|[A-Za-z_][A-Za-z0-9_]*))?$)"));

    for (int index = 0; index < lines.size(); ++index) {
        const int line_number = index + 1;
        const QString line = strip_comments(lines.at(index));
        if (line.isEmpty()) {
            continue;
        }

        if (in_struct) {
            if (line == QStringLiteral("}")) {
                if (schema.structs.contains(current_struct.name)) {
                    return set_error(error_message, line_number, QStringLiteral("Duplicate struct `%1`.").arg(current_struct.name));
                }
                schema.structs.insert(current_struct.name, current_struct);
                current_struct = StructDefinition{};
                in_struct = false;
                continue;
            }

            const auto match = field_pattern.match(line);
            if (!match.hasMatch()) {
                return set_error(error_message, line_number, QStringLiteral("Invalid field definition."));
            }

            current_struct.fields.push_back(FieldDefinition{
                match.captured(1),
                match.captured(3),
                match.captured(2),
                match.captured(4),
                line_number,
            });
            continue;
        }

        const auto endian_match = endian_pattern.match(line);
        if (endian_match.hasMatch()) {
            schema.endianness = endian_match.captured(1) == QStringLiteral("big") ? Endianness::Big : Endianness::Little;
            continue;
        }

        auto match = struct_pattern.match(line);
        if (match.hasMatch()) {
            current_struct = StructDefinition{};
            current_struct.name = match.captured(1);
            in_struct = true;
            continue;
        }

        match = root_pattern.match(line);
        if (match.hasMatch()) {
            if (!schema.root_name.isEmpty()) {
                return set_error(error_message, line_number, QStringLiteral("Only one root block is allowed."));
            }
            schema.root_name = match.captured(1);
            current_struct = StructDefinition{};
            current_struct.name = schema.root_name;
            in_struct = true;
            continue;
        }

        return set_error(error_message, line_number, QStringLiteral("Unexpected statement."));
    }

    if (in_struct) {
        return set_error(error_message, lines.size(), QStringLiteral("Missing closing `}`."));
    }
    if (schema.root_name.isEmpty()) {
        return set_error(error_message, 0, QStringLiteral("A `root Name { ... }` block is required."));
    }
    if (!schema.structs.contains(schema.root_name)) {
        return set_error(error_message, 0, QStringLiteral("Root definition `%1` was not parsed.").arg(schema.root_name));
    }
    return true;
}

bool evaluate_schema(
    const SchemaDefinition& schema,
    qint64 base_offset,
    qint64 document_size,
    const ReadRangeCallback& read_range,
    ParsedNode& root_node,
    QString* error_message) {
    const auto it = schema.structs.constFind(schema.root_name);
    if (it == schema.structs.cend()) {
        return set_error(error_message, 0, QStringLiteral("Root struct `%1` is missing.").arg(schema.root_name));
    }
    if (!read_range) {
        return set_error(error_message, 0, QStringLiteral("No document reader is available."));
    }

    const EvaluationContext context{&schema, document_size, read_range};
    return evaluate_struct(context, it.value(), base_offset, root_node, error_message, 0);
}

QString default_schema_template() {
    return QStringLiteral(
        "endian little\n"
        "\n"
        "struct Entry {\n"
        "  u32 id\n"
        "  u16 flags\n"
        "  u16 value\n"
        "}\n"
        "\n"
        "root Header {\n"
        "  bytes[4] magic\n"
        "  u16 version\n"
        "  u16 entry_count\n"
        "  u32 entries_offset\n"
        "  Entry[entry_count] entries @entries_offset\n"
        "}\n");
}

} // namespace StructureSchema
