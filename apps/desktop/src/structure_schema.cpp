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

enum class ExpressionTokenKind {
    End,
    Invalid,
    Number,
    Identifier,
    Plus,
    Minus,
    Multiply,
    Divide,
    LeftParen,
    RightParen,
};

struct ExpressionToken {
    ExpressionTokenKind kind = ExpressionTokenKind::End;
    QString text;
    quint64 number = 0;
};

class ExpressionParser {
public:
    ExpressionParser(const QString& expression, const QMap<QString, quint64>& context)
        : expression_(expression), context_(context) {
        next();
    }

    bool parse(quint64& value, QString& error) {
        if (!parse_expression(value, error)) {
            return false;
        }
        if (current_.kind != ExpressionTokenKind::End) {
            error = QStringLiteral("Unexpected token `%1`.").arg(current_.text);
            return false;
        }
        return true;
    }

private:
    bool parse_expression(quint64& value, QString& error) {
        if (!parse_term(value, error)) {
            return false;
        }
        while (current_.kind == ExpressionTokenKind::Plus || current_.kind == ExpressionTokenKind::Minus) {
            const ExpressionTokenKind op = current_.kind;
            next();
            quint64 rhs = 0;
            if (!parse_term(rhs, error)) {
                return false;
            }
            if (op == ExpressionTokenKind::Plus) {
                if (rhs > std::numeric_limits<quint64>::max() - value) {
                    error = QStringLiteral("Expression overflow.");
                    return false;
                }
                value += rhs;
            } else {
                if (rhs > value) {
                    error = QStringLiteral("Expression underflow.");
                    return false;
                }
                value -= rhs;
            }
        }
        return true;
    }

    bool parse_term(quint64& value, QString& error) {
        if (!parse_factor(value, error)) {
            return false;
        }
        while (current_.kind == ExpressionTokenKind::Multiply || current_.kind == ExpressionTokenKind::Divide) {
            const ExpressionTokenKind op = current_.kind;
            next();
            quint64 rhs = 0;
            if (!parse_factor(rhs, error)) {
                return false;
            }
            if (op == ExpressionTokenKind::Multiply) {
                if (value != 0 && rhs > std::numeric_limits<quint64>::max() / value) {
                    error = QStringLiteral("Expression overflow.");
                    return false;
                }
                value *= rhs;
            } else {
                if (rhs == 0) {
                    error = QStringLiteral("Division by zero.");
                    return false;
                }
                value /= rhs;
            }
        }
        return true;
    }

    bool parse_factor(quint64& value, QString& error) {
        if (current_.kind == ExpressionTokenKind::Number) {
            value = current_.number;
            next();
            return true;
        }

        if (current_.kind == ExpressionTokenKind::Identifier) {
            const auto it = context_.constFind(current_.text);
            if (it == context_.cend()) {
                error = QStringLiteral("Unknown symbol `%1`.").arg(current_.text);
                return false;
            }
            value = it.value();
            next();
            return true;
        }

        if (current_.kind == ExpressionTokenKind::LeftParen) {
            next();
            if (!parse_expression(value, error)) {
                return false;
            }
            if (current_.kind != ExpressionTokenKind::RightParen) {
                error = QStringLiteral("Missing closing `)`.");
                return false;
            }
            next();
            return true;
        }

        error = current_.kind == ExpressionTokenKind::End
            ? QStringLiteral("Unexpected end of expression.")
            : QStringLiteral("Unexpected token `%1`.").arg(current_.text);
        return false;
    }

    void next() {
        while (index_ < expression_.size() && expression_.at(index_).isSpace()) {
            ++index_;
        }
        if (index_ >= expression_.size()) {
            current_ = {};
            current_.kind = ExpressionTokenKind::End;
            return;
        }

        const QChar ch = expression_.at(index_);
        if (ch == QLatin1Char('+')) {
            current_ = {ExpressionTokenKind::Plus, QString(ch), 0};
            ++index_;
            return;
        }
        if (ch == QLatin1Char('-')) {
            current_ = {ExpressionTokenKind::Minus, QString(ch), 0};
            ++index_;
            return;
        }
        if (ch == QLatin1Char('*')) {
            current_ = {ExpressionTokenKind::Multiply, QString(ch), 0};
            ++index_;
            return;
        }
        if (ch == QLatin1Char('/')) {
            current_ = {ExpressionTokenKind::Divide, QString(ch), 0};
            ++index_;
            return;
        }
        if (ch == QLatin1Char('(')) {
            current_ = {ExpressionTokenKind::LeftParen, QString(ch), 0};
            ++index_;
            return;
        }
        if (ch == QLatin1Char(')')) {
            current_ = {ExpressionTokenKind::RightParen, QString(ch), 0};
            ++index_;
            return;
        }

        if (ch.isDigit()) {
            const int start = index_;
            if (ch == QLatin1Char('0') && index_ + 1 < expression_.size()
                && (expression_.at(index_ + 1) == QLatin1Char('x') || expression_.at(index_ + 1) == QLatin1Char('X'))) {
                index_ += 2;
                while (index_ < expression_.size()
                       && (expression_.at(index_).isDigit()
                           || (expression_.at(index_).toLower() >= QLatin1Char('a')
                               && expression_.at(index_).toLower() <= QLatin1Char('f')))) {
                    ++index_;
                }
            } else {
                while (index_ < expression_.size() && expression_.at(index_).isDigit()) {
                    ++index_;
                }
            }
            const QString text = expression_.mid(start, index_ - start);
            bool ok = false;
            const quint64 number = text.toULongLong(&ok, 0);
            if (!ok) {
                current_ = {ExpressionTokenKind::Invalid, text, 0};
                return;
            }
            current_ = {ExpressionTokenKind::Number, text, number};
            return;
        }

        if (ch.isLetter() || ch == QLatin1Char('_')) {
            const int start = index_;
            ++index_;
            while (index_ < expression_.size()
                   && (expression_.at(index_).isLetterOrNumber() || expression_.at(index_) == QLatin1Char('_'))) {
                ++index_;
            }
            current_ = {ExpressionTokenKind::Identifier, expression_.mid(start, index_ - start), 0};
            return;
        }

        current_ = {ExpressionTokenKind::Invalid, QString(ch), 0};
        ++index_;
    }

    QString expression_;
    const QMap<QString, quint64>& context_;
    int index_ = 0;
    ExpressionToken current_;
};

bool resolve_expression_value(const QString& expression, const QMap<QString, quint64>& context, quint64& value, QString* error = nullptr) {
    ExpressionParser parser(expression.trimmed(), context);
    QString local_error;
    if (!parser.parse(value, local_error)) {
        if (error != nullptr) {
            *error = local_error;
        }
        return false;
    }
    return true;
}

QString format_bytes_preview(const QByteArray& bytes) {
    QString text;
    const int shown = qMin(bytes.size(), 32);
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
    qint64 base_offset = 0;
    qint64 document_size = 0;
    ReadRangeCallback read_range;
    ProgressCallback progress_callback;
};

bool report_progress(const EvaluationContext& context, qint64 current_offset, int line, QString* error_message) {
    if (!context.progress_callback) {
        return true;
    }
    const qint64 covered_bytes = qMax<qint64>(0, current_offset - context.base_offset);
    if (context.progress_callback(current_offset, covered_bytes)) {
        return true;
    }
    return set_error(error_message, line, QStringLiteral("Schema run canceled."));
}

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
        QString expression_error;
        if (!resolve_expression_value(field.count_token, local_values, count, &expression_error)) {
            return set_error(error_message, field.line, QStringLiteral("Invalid count expression `%1`: %2").arg(field.count_token, expression_error));
        }
    }

    quint64 offset_value = static_cast<quint64>(cursor);
    if (!field.offset_token.isEmpty()) {
        QString expression_error;
        if (!resolve_expression_value(field.offset_token, local_values, offset_value, &expression_error)) {
            return set_error(error_message, field.line, QStringLiteral("Invalid offset expression `%1`: %2").arg(field.offset_token, expression_error));
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
        return report_progress(context, offset + size, field.line, error_message);
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
        return report_progress(context, field_offset + size, field.line, error_message);
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
            return report_progress(context, field_offset + primitive.width, field.line, error_message);
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
            if (!report_progress(context, running_offset, field.line, error_message)) {
                return false;
            }
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
        return report_progress(context, field_offset + child.size, field.line, error_message);
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
        if (!report_progress(context, running_offset, field.line, error_message)) {
            return false;
        }
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
    const QRegularExpression field_pattern(QStringLiteral(R"(^(bytes|u8|i8|u16|i16|u32|i32|u64|i64|f32|f64|[A-Za-z_][A-Za-z0-9_]*)(?:\[(.+?)\])?\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+@(.+))?$)"));

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
                match.captured(2).trimmed(),
                match.captured(4).trimmed(),
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
    const ProgressCallback& progress_callback,
    QString* error_message) {
    const auto it = schema.structs.constFind(schema.root_name);
    if (it == schema.structs.cend()) {
        return set_error(error_message, 0, QStringLiteral("Root struct `%1` is missing.").arg(schema.root_name));
    }
    if (!read_range) {
        return set_error(error_message, 0, QStringLiteral("No document reader is available."));
    }

    const EvaluationContext context{&schema, base_offset, document_size, read_range, progress_callback};
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
