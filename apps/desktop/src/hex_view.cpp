#include "hex_view.hpp"

#include <QFontDatabase>
#include <QDateTime>
#include <QCryptographicHash>
#include <QColor>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtMath>
#include <array>
#include <algorithm>
#include <cstring>

namespace {
constexpr int kScrollbarResolution = 1000000;
constexpr qint64 kDirectScrollbarMax = std::numeric_limits<int>::max();

QColor next_bookmark_color(int index) {
    static const std::array<QColor, 6> colors = {
        QColor(220, 68, 55),
        QColor(244, 135, 30),
        QColor(234, 179, 8),
        QColor(34, 197, 94),
        QColor(59, 130, 246),
        QColor(168, 85, 247),
    };
    return colors[static_cast<std::size_t>(index % static_cast<int>(colors.size()))];
}

quint32 crc32_update(quint32 crc, const QByteArray& bytes) {
    static quint32 table[256];
    static bool initialized = false;
    if (!initialized) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1U) ? (0xEDB88320U ^ (value >> 1U)) : (value >> 1U);
            }
            table[i] = value;
        }
        initialized = true;
    }

    quint32 next = crc;
    for (const char byte : bytes) {
        const auto index = static_cast<quint8>((next ^ static_cast<quint8>(byte)) & 0xFFU);
        next = table[index] ^ (next >> 8U);
    }
    return next;
}
}

HexView::HexView(QWidget* parent) : QAbstractScrollArea(parent) {
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setFamily(QStringLiteral("Consolas"));
    mono.setPointSize(10);
    setFont(mono);
    viewport()->setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        first_visible_row_ = qBound<qint64>(0, slider_to_row(value), max_first_visible_row());
        viewport()->update();
    });

    refresh_metrics();
    refresh_scrollbar();
}

bool HexView::open_file(const QString& path) {
    if (!source_.open(path)) {
        return false;
    }

    first_visible_row_ = 0;
    caret_offset_ = 0;
    selection_anchor_ = -1;
    selection_active_ = false;
    high_nibble_pending_ = true;
    active_pane_ = ActivePane::Hex;
    bookmarks_.clear();
    ++render_generation_;
    refresh_metrics();
    invalidate_row_cache();
    refresh_scrollbar();
    viewport()->update();
    emit document_loaded(source_.display_name(), static_cast<qulonglong>(source_.size()));
    emit_status();
    emit_bookmarks();
    emit_inspector();
    return true;
}

bool HexView::has_document() const {
    return source_.is_open();
}

HexView::ActivePane HexView::active_pane() const {
    return active_pane_;
}

bool HexView::bookmark_gutter_visible() const {
    return show_bookmark_gutter_;
}

void HexView::set_bookmark_gutter_visible(bool visible) {
    if (show_bookmark_gutter_ == visible) {
        return;
    }
    show_bookmark_gutter_ = visible;
    refresh_metrics();
    viewport()->update();
}

bool HexView::row_numbers_visible() const {
    return show_row_numbers_;
}

void HexView::set_row_numbers_visible(bool visible) {
    if (show_row_numbers_ == visible) {
        return;
    }
    show_row_numbers_ = visible;
    refresh_metrics();
    viewport()->update();
}

bool HexView::offsets_visible() const {
    return show_offsets_;
}

void HexView::set_offsets_visible(bool visible) {
    if (show_offsets_ == visible) {
        return;
    }
    show_offsets_ = visible;
    refresh_metrics();
    viewport()->update();
}

int HexView::row_number_column_width() const {
    return custom_row_number_width_;
}

void HexView::set_row_number_column_width(int width) {
    custom_row_number_width_ = width > 0 ? width : -1;
    refresh_metrics();
    viewport()->update();
}

int HexView::offset_column_width() const {
    return custom_address_width_;
}

void HexView::set_offset_column_width(int width) {
    custom_address_width_ = width > 0 ? width : -1;
    refresh_metrics();
    viewport()->update();
}

void HexView::reset_view_layout() {
    custom_row_number_width_ = -1;
    custom_address_width_ = -1;
    refresh_metrics();
    viewport()->update();
}

QString HexView::document_title() const {
    return source_.display_name();
}

QString HexView::document_path() const {
    return source_.file_path();
}

qint64 HexView::document_size() const {
    return source_.size();
}

qint64 HexView::bytes_per_row() const {
    return bytes_per_row_;
}

bool HexView::is_dirty() const {
    return source_.is_dirty();
}

bool HexView::save() {
    const bool saved = source_.save();
    if (saved) {
        ++render_generation_;
        invalidate_row_cache();
        emit document_loaded(source_.display_name(), static_cast<qulonglong>(source_.size()));
        emit_status();
        viewport()->update();
    }
    return saved;
}

bool HexView::save_as(const QString& path) {
    const bool saved = source_.save_as(path);
    if (saved) {
        ++render_generation_;
        invalidate_row_cache();
        emit document_loaded(source_.display_name(), static_cast<qulonglong>(source_.size()));
        emit_status();
        viewport()->update();
    }
    return saved;
}

bool HexView::save_with_progress(const std::function<bool(qint64, qint64)>& progress_callback) {
    const bool saved = source_.save_with_progress(progress_callback);
    if (saved) {
        ++render_generation_;
        invalidate_row_cache();
        emit document_loaded(source_.display_name(), static_cast<qulonglong>(source_.size()));
        emit_status();
        viewport()->update();
    }
    return saved;
}

bool HexView::save_as_with_progress(const QString& path, const std::function<bool(qint64, qint64)>& progress_callback) {
    const bool saved = source_.save_as_with_progress(path, progress_callback);
    if (saved) {
        ++render_generation_;
        invalidate_row_cache();
        emit document_loaded(source_.display_name(), static_cast<qulonglong>(source_.size()));
        emit_status();
        viewport()->update();
    }
    return saved;
}

bool HexView::undo() {
    const bool undone = source_.undo();
    if (undone) {
        ++render_generation_;
        invalidate_row_cache();
        emit_status();
        viewport()->update();
    }
    return undone;
}

bool HexView::redo() {
    const bool redone = source_.redo();
    if (redone) {
        ++render_generation_;
        invalidate_row_cache();
        emit_status();
        viewport()->update();
    }
    return redone;
}

bool HexView::has_selection() const {
    return selection_active_ && selection_anchor_ >= 0 && selection_size() > 0;
}

QByteArray HexView::selected_bytes() const {
    if (!has_selection()) {
        return {};
    }

    return source_.read_range(selection_start(), selection_size());
}

QByteArray HexView::read_bytes(qint64 offset, qint64 length) const {
    if (!has_document() || length <= 0) {
        return {};
    }

    return source_.read_range(offset, length);
}

QString HexView::selected_hex_text() const {
    const QByteArray bytes = selected_bytes();
    return bytes.isEmpty() ? QString() : QString::fromLatin1(bytes.toHex().toUpper());
}

QString HexView::selected_text_text() const {
    const QByteArray bytes = selected_bytes();
    if (bytes.isEmpty()) {
        return QString();
    }

    QString text;
    text.reserve(bytes.size());
    for (const char byte : bytes) {
        text.append(printable_char(static_cast<quint8>(byte)));
    }
    return text;
}

bool HexView::insert_at_caret(const QByteArray& bytes) {
    if (!has_document() || is_read_only() || bytes.isEmpty()) {
        return false;
    }

    qint64 start = caret_offset_;
    if (has_selection()) {
        start = selection_start();
        if (!source_.delete_range(start, selection_size())) {
            return false;
        }
        caret_offset_ = start;
        selection_anchor_ = -1;
        selection_active_ = false;
    }

    if (!source_.insert_range(start, bytes)) {
        return false;
    }

    selection_anchor_ = start;
    selection_active_ = bytes.size() > 1;
    caret_offset_ = start + qMax(0, bytes.size() - 1);
    high_nibble_pending_ = true;
    pending_insert_high_nibble_ = -1;
    refresh_after_edit();
    return true;
}

bool HexView::delete_selection() {
    if (!has_document() || is_read_only() || !has_selection()) {
        return false;
    }

    const qint64 start = selection_start();
    const qint64 length = selection_size();
    if (!source_.delete_range(start, length)) {
        return false;
    }

    selection_anchor_ = -1;
    selection_active_ = false;
    caret_offset_ = qMin(start, qMax<qint64>(0, document_size() - 1));
    high_nibble_pending_ = true;
    pending_insert_high_nibble_ = -1;
    refresh_after_edit();
    return true;
}

bool HexView::delete_at_caret() {
    if (!has_document() || is_read_only() || document_size() <= 0) {
        return false;
    }

    if (has_selection()) {
        return delete_selection();
    }

    if (!source_.delete_range(caret_offset_, 1)) {
        return false;
    }

    selection_anchor_ = -1;
    selection_active_ = false;
    caret_offset_ = qMin(caret_offset_, qMax<qint64>(0, document_size() - 1));
    high_nibble_pending_ = true;
    pending_insert_high_nibble_ = -1;
    refresh_after_edit();
    return true;
}

bool HexView::backspace_at_caret() {
    if (!has_document() || is_read_only() || document_size() <= 0) {
        return false;
    }

    if (has_selection()) {
        return delete_selection();
    }

    if (caret_offset_ <= 0) {
        return false;
    }

    const qint64 delete_offset = caret_offset_ - 1;
    if (!source_.delete_range(delete_offset, 1)) {
        return false;
    }

    selection_anchor_ = -1;
    selection_active_ = false;
    caret_offset_ = delete_offset;
    high_nibble_pending_ = true;
    pending_insert_high_nibble_ = -1;
    refresh_after_edit();
    return true;
}

bool HexView::overwrite_at_caret(const QByteArray& bytes) {
    if (!has_document() || is_read_only() || bytes.isEmpty()) {
        return false;
    }

    const qint64 start = has_selection() ? selection_start() : caret_offset_;
    if (start < 0 || start + bytes.size() > document_size()) {
        return false;
    }

    if (!source_.overwrite_range(start, bytes)) {
        return false;
    }

    selection_anchor_ = start;
    selection_active_ = bytes.size() > 1;
    caret_offset_ = start + qMax(0, bytes.size() - 1);
    high_nibble_pending_ = true;
    pending_insert_high_nibble_ = -1;
    refresh_after_edit();
    return true;
}

bool HexView::overwrite_selection(const QByteArray& bytes) {
    if (!has_selection() || bytes.isEmpty() || bytes.size() != selection_size()) {
        return false;
    }

    return overwrite_at_caret(bytes);
}

bool HexView::fill_selection(quint8 value) {
    if (!has_selection() || is_read_only()) {
        return false;
    }

    return overwrite_selection(QByteArray(static_cast<int>(selection_size()), static_cast<char>(value)));
}

bool HexView::find_pattern(
    const QByteArray& pattern,
    bool forward,
    bool from_caret,
    qint64* found_offset,
    bool* canceled,
    const SearchProgressCallback& progress_callback) {
    if (!has_document() || pattern.isEmpty()) {
        return false;
    }

    if (canceled != nullptr) {
        *canceled = false;
    }

    qint64 start_offset = 0;
    if (from_caret) {
        start_offset = forward ? qMin(document_size(), caret_offset_ + 1) : qMax<qint64>(0, caret_offset_ - 1);
    } else {
        start_offset = forward ? 0 : document_size() - 1;
    }

    const qint64 range_start = 0;
    const qint64 range_end = document_size();
    const qint64 first_pass_total = forward ? qMax<qint64>(0, range_end - qBound(range_start, start_offset, range_end))
                                            : qMax<qint64>(0, qBound(range_start, start_offset, range_end - 1) - range_start + 1);
    const qint64 second_pass_total = from_caret ? qMax<qint64>(0, (range_end - range_start) - first_pass_total) : 0;

    bool search_canceled = false;
    const auto report_first_pass = [&](qint64 completed, qint64 total) -> bool {
        Q_UNUSED(total);
        if (!progress_callback) {
            return true;
        }
        const bool keep_going = progress_callback(completed, first_pass_total + second_pass_total);
        search_canceled = !keep_going;
        return keep_going;
    };

    qint64 match = search_from(pattern, start_offset, forward, range_start, range_end, report_first_pass);
    if (match < 0 && from_caret && !search_canceled) {
        const auto report_second_pass = [&](qint64 completed, qint64 total) -> bool {
            Q_UNUSED(total);
            if (!progress_callback) {
                return true;
            }
            const bool keep_going = progress_callback(first_pass_total + completed, first_pass_total + second_pass_total);
            search_canceled = !keep_going;
            return keep_going;
        };
        match = search_from(pattern, forward ? 0 : document_size() - 1, forward, range_start, range_end, report_second_pass);
    }
    if (progress_callback && !search_canceled) {
        progress_callback(first_pass_total + second_pass_total, first_pass_total + second_pass_total);
    }
    if (match < 0) {
        if (canceled != nullptr && search_canceled) {
            *canceled = true;
        }
        return false;
    }

    if (found_offset != nullptr) {
        *found_offset = match;
    }

    selection_anchor_ = match;
    selection_active_ = pattern.size() > 1;
    caret_offset_ = match + qMax(0, pattern.size() - 1);
    high_nibble_pending_ = true;
    ensure_caret_visible();
    emit_status();
    viewport()->update();
    return true;
}

bool HexView::find_pattern_in_selection(
    const QByteArray& pattern,
    bool forward,
    bool from_caret,
    qint64* found_offset,
    bool* canceled,
    const SearchProgressCallback& progress_callback) {
    if (!has_document() || pattern.isEmpty() || !has_selection()) {
        return false;
    }

    if (canceled != nullptr) {
        *canceled = false;
    }

    const qint64 range_start = selection_start();
    const qint64 range_end = selection_end();
    if (pattern.size() > range_end - range_start) {
        return false;
    }

    qint64 start_offset = forward ? range_start : range_end - 1;
    if (from_caret) {
        start_offset = forward
            ? qBound(range_start, caret_offset_ + 1, range_end)
            : qBound(range_start, caret_offset_ - 1, range_end - 1);
    }

    const qint64 first_pass_total = forward ? qMax<qint64>(0, range_end - qBound(range_start, start_offset, range_end))
                                            : qMax<qint64>(0, qBound(range_start, start_offset, range_end - 1) - range_start + 1);
    const qint64 second_pass_total = from_caret ? qMax<qint64>(0, (range_end - range_start) - first_pass_total) : 0;

    bool search_canceled = false;
    const auto report_first_pass = [&](qint64 completed, qint64 total) -> bool {
        Q_UNUSED(total);
        if (!progress_callback) {
            return true;
        }
        const bool keep_going = progress_callback(completed, first_pass_total + second_pass_total);
        search_canceled = !keep_going;
        return keep_going;
    };

    qint64 match = search_from(pattern, start_offset, forward, range_start, range_end, report_first_pass);
    if (match < 0 && from_caret && !search_canceled) {
        const auto report_second_pass = [&](qint64 completed, qint64 total) -> bool {
            Q_UNUSED(total);
            if (!progress_callback) {
                return true;
            }
            const bool keep_going = progress_callback(first_pass_total + completed, first_pass_total + second_pass_total);
            search_canceled = !keep_going;
            return keep_going;
        };
        match = search_from(pattern, forward ? range_start : range_end - 1, forward, range_start, range_end, report_second_pass);
    }
    if (progress_callback && !search_canceled) {
        progress_callback(first_pass_total + second_pass_total, first_pass_total + second_pass_total);
    }
    if (match < 0) {
        if (canceled != nullptr && search_canceled) {
            *canceled = true;
        }
        return false;
    }

    if (found_offset != nullptr) {
        *found_offset = match;
    }

    selection_anchor_ = match;
    selection_active_ = pattern.size() > 1;
    caret_offset_ = match + qMax(0, pattern.size() - 1);
    high_nibble_pending_ = true;
    ensure_caret_visible();
    emit_status();
    viewport()->update();
    return true;
}

QVector<qint64> HexView::find_all_patterns(
    const QByteArray& pattern,
    bool selection_only,
    bool* canceled,
    const SearchProgressCallback& progress_callback) const {
    QVector<qint64> matches;
    if (!has_document() || pattern.isEmpty()) {
        return matches;
    }

    if (canceled != nullptr) {
        *canceled = false;
    }

    const qint64 range_start = selection_only && has_selection() ? selection_start() : 0;
    const qint64 range_end = selection_only && has_selection() ? selection_end() : document_size();
    if (range_end - range_start < pattern.size()) {
        return matches;
    }

    qint64 cursor = range_start;
    bool search_canceled = false;
    while (cursor <= range_end - pattern.size()) {
        const qint64 base_completed = qMax<qint64>(0, cursor - range_start);
        const auto report_progress = [&](qint64 completed, qint64 total) -> bool {
            Q_UNUSED(total);
            if (!progress_callback) {
                return true;
            }
            const bool keep_going = progress_callback(base_completed + completed, range_end - range_start);
            search_canceled = !keep_going;
            return keep_going;
        };
        const qint64 found = search_from(pattern, cursor, true, range_start, range_end, report_progress);
        if (found < 0) {
            break;
        }

        matches.push_back(found);
        cursor = found + 1;
    }

    if (progress_callback && !search_canceled) {
        progress_callback(range_end - range_start, range_end - range_start);
    }
    if (canceled != nullptr && search_canceled) {
        *canceled = true;
    }

    return matches;
}

QString HexView::format_search_result(qint64 found_offset, const QByteArray& pattern, bool hex_mode) const {
    if (found_offset < 0) {
        return QStringLiteral("Search Results\n\nNo matches found.");
    }

    const QByteArray preview = source_.read_range(found_offset, qMax<qint64>(pattern.size(), 16));
    QString preview_text;
    for (int index = 0; index < preview.size(); ++index) {
        if (index > 0) {
            preview_text += QLatin1Char(' ');
        }
        preview_text += hex_byte(static_cast<quint8>(preview.at(index)));
    }

    return QStringLiteral("Search Results\n\nMode: %1\nPattern: %2\nOffset: 0x%3\nPreview: %4")
        .arg(hex_mode ? QStringLiteral("Hex") : QStringLiteral("Text"))
        .arg(hex_mode ? QString::fromLatin1(pattern.toHex(' ').toUpper()) : QString::fromUtf8(pattern))
        .arg(found_offset, 8, 16, QChar(u'0'))
        .arg(preview_text);
}

QString HexView::build_hash_report(bool selection_only) const {
    QString text = QStringLiteral("Analysis\n\n");
    QString current_section;
    const QVector<AnalysisRow> rows = build_analysis_rows(selection_only);
    for (const AnalysisRow& row : rows) {
        if (row.section != current_section) {
            if (!current_section.isEmpty()) {
                text += QLatin1Char('\n');
            }
            current_section = row.section;
        }
        text += QStringLiteral("%1: %2\n").arg(row.field, row.value);
    }
    return text;
}

QVector<HexView::AnalysisRow> HexView::analysis_rows(bool selection_only) const {
    return build_analysis_rows(selection_only);
}

QVector<HexView::AnalysisRow> HexView::build_analysis_rows(bool selection_only) const {
    QVector<AnalysisRow> rows;
    auto add_row = [&rows](const QString& section, const QString& field, const QString& value) {
        rows.push_back(AnalysisRow{section, field, value});
    };

    if (!has_document()) {
        add_row(QStringLiteral("Overview"), QStringLiteral("Status"), QStringLiteral("Open a file to compute hashes."));
        return rows;
    }

    const qint64 range_start = selection_only && has_selection() ? selection_start() : 0;
    const qint64 range_len = selection_only && has_selection() ? selection_size() : document_size();
    if (range_len <= 0) {
        add_row(QStringLiteral("Overview"), QStringLiteral("Status"), QStringLiteral("No bytes available for hashing."));
        return rows;
    }

    constexpr qint64 kChunkSize = 1024 * 1024;
    QCryptographicHash md5(QCryptographicHash::Md5);
    QCryptographicHash sha1(QCryptographicHash::Sha1);
    QCryptographicHash sha256(QCryptographicHash::Sha256);
    quint32 crc = 0xFFFFFFFFU;

    for (qint64 offset = range_start; offset < range_start + range_len;) {
        const qint64 to_read = qMin(kChunkSize, range_start + range_len - offset);
        const QByteArray chunk = source_.read_range(offset, to_read);
        if (chunk.isEmpty()) {
            break;
        }

        md5.addData(chunk);
        sha1.addData(chunk);
        sha256.addData(chunk);
        crc = crc32_update(crc, chunk);
        offset += chunk.size();
    }

    add_row(QStringLiteral("Overview"), QStringLiteral("Scope"), selection_only && has_selection() ? QStringLiteral("Selection") : QStringLiteral("Whole Document"));
    add_row(
        QStringLiteral("Overview"),
        QStringLiteral("Range"),
        QStringLiteral("0x%1 - 0x%2")
            .arg(range_start, 8, 16, QChar(u'0'))
            .arg(range_start + range_len - 1, 8, 16, QChar(u'0'))
            .toUpper());
    add_row(QStringLiteral("Overview"), QStringLiteral("Length"), QStringLiteral("%1 bytes").arg(range_len));
    add_row(QStringLiteral("Checksums"), QStringLiteral("CRC32"), QStringLiteral("%1").arg(~crc, 8, 16, QChar(u'0')).toUpper());
    add_row(QStringLiteral("Digests"), QStringLiteral("MD5"), QString::fromLatin1(md5.result().toHex()).toUpper());
    add_row(QStringLiteral("Digests"), QStringLiteral("SHA-1"), QString::fromLatin1(sha1.result().toHex()).toUpper());
    add_row(QStringLiteral("Digests"), QStringLiteral("SHA-256"), QString::fromLatin1(sha256.result().toHex()).toUpper());
    return rows;
}

bool HexView::replace_range(qint64 offset, const QByteArray& before, const QByteArray& after) {
    if (!has_document() || is_read_only() || before.isEmpty()) {
        return false;
    }

    if (edit_mode_ == EditMode::Overwrite && before.size() != after.size()) {
        return false;
    }

    const QByteArray current = source_.read_range(offset, before.size());
    if (current != before) {
        return false;
    }

    if (before.size() == after.size()) {
        if (!source_.overwrite_range(offset, after)) {
            return false;
        }
    } else {
        if (!source_.delete_range(offset, before.size())) {
            return false;
        }
        if (!after.isEmpty() && !source_.insert_range(offset, after)) {
            return false;
        }
    }

    selection_anchor_ = offset;
    selection_active_ = after.size() > 1;
    caret_offset_ = offset + qMax(0, after.size() - 1);
    high_nibble_pending_ = true;
    pending_insert_high_nibble_ = -1;
    refresh_after_edit();
    return true;
}

qint64 HexView::replace_all(
    const QByteArray& before,
    const QByteArray& after,
    bool selection_only,
    QVector<qint64>* replaced_offsets,
    bool* canceled,
    const SearchProgressCallback& progress_callback) {
    if (!has_document() || is_read_only() || before.isEmpty()) {
        return 0;
    }

    if (canceled != nullptr) {
        *canceled = false;
    }

    if (edit_mode_ == EditMode::Overwrite && before.size() != after.size()) {
        return 0;
    }

    const qint64 range_start = selection_only && has_selection() ? selection_start() : 0;
    qint64 range_end = selection_only && has_selection() ? selection_end() : document_size();
    if (range_end - range_start < before.size()) {
        return 0;
    }

    qint64 replacements = 0;
    qint64 cursor = range_start;
    qint64 last_replaced = -1;
    bool replace_canceled = false;
    while (cursor <= range_end - before.size()) {
        const qint64 base_completed = qMax<qint64>(0, cursor - range_start);
        const auto report_progress = [&](qint64 completed, qint64 total) -> bool {
            Q_UNUSED(total);
            if (!progress_callback) {
                return true;
            }
            const bool keep_going = progress_callback(base_completed + completed, range_end - range_start);
            replace_canceled = !keep_going;
            return keep_going;
        };
        const qint64 found = search_from(before, cursor, true, range_start, range_end, report_progress);
        if (found < 0) {
            break;
        }

        if (!replace_range(found, before, after)) {
            break;
        }

        ++replacements;
        if (replaced_offsets != nullptr) {
            replaced_offsets->push_back(found);
        }
        last_replaced = found;
        cursor = found + after.size();
        range_end += after.size() - before.size();
    }

    if (progress_callback && !replace_canceled) {
        progress_callback(range_end - range_start, range_end - range_start);
    }
    if (canceled != nullptr && replace_canceled) {
        *canceled = true;
    }

    if (replacements > 0) {
        selection_anchor_ = last_replaced;
        selection_active_ = after.size() > 1;
        caret_offset_ = last_replaced + qMax(0, after.size() - 1);
        high_nibble_pending_ = true;
        pending_insert_high_nibble_ = -1;
        refresh_after_edit();
    }

    return replacements;
}

HexView::InspectorEndian HexView::inspector_endian() const {
    return inspector_endian_;
}

void HexView::set_inspector_endian(InspectorEndian endian) {
    if (inspector_endian_ == endian) {
        return;
    }

    inspector_endian_ = endian;
    emit_inspector();
}

QVector<HexView::InspectorRow> HexView::inspector_rows() const {
    return build_inspector_rows();
}

bool HexView::apply_inspector_edit(const QString& section, const QString& field, const QString& value, QString* error_message) {
    auto fail = [&](const QString& message) {
        if (error_message != nullptr) {
            *error_message = message;
        }
        return false;
    };

    if (!has_document()) {
        return fail(QStringLiteral("Open a file before editing inspector values."));
    }
    if (is_read_only()) {
        return fail(QStringLiteral("Document is read-only."));
    }

    const bool use_selection = has_selection();
    const qint64 start_offset = use_selection ? selection_start() : caret_offset_;
    const qint64 visible_length = qMin<qint64>(use_selection ? selection_size() : 8, 8);
    if (visible_length <= 0) {
        return fail(QStringLiteral("No bytes available at the current location."));
    }

    QByteArray replacement;
    if (section == QStringLiteral("Overview") && field == QStringLiteral("Bytes")) {
        if (!try_parse_hex_string(value, replacement)) {
            return fail(QStringLiteral("Enter bytes as hex pairs such as DE AD BE EF."));
        }
        if (replacement.size() != visible_length) {
            return fail(QStringLiteral("Byte edit must match the visible inspector width."));
        }
    } else if (section == QStringLiteral("Overview") && field == QStringLiteral("ASCII")) {
        const QByteArray ascii = value.toLatin1();
        if (ascii.size() != visible_length) {
            return fail(QStringLiteral("ASCII edit must match the visible inspector width."));
        }
        replacement = ascii;
    } else if (section == QStringLiteral("Integers")) {
        bool ok = false;
        if (field.startsWith(QLatin1Char('u'))) {
            const int bits = field.mid(1).toInt(&ok);
            if (!ok || bits % 8 != 0) {
                return fail(QStringLiteral("Unsupported unsigned integer width."));
            }
            const int width = bits / 8;
            bool parse_ok = false;
            const qulonglong parsed = value.trimmed().toULongLong(&parse_ok, 0);
            if (!parse_ok) {
                return fail(QStringLiteral("Invalid unsigned integer value."));
            }
            if (bits < 64) {
                const qulonglong max_value = (1ULL << bits) - 1ULL;
                if (parsed > max_value) {
                    return fail(QStringLiteral("Value is out of range for %1.").arg(field));
                }
            }
            replacement.resize(width);
            for (int i = 0; i < width; ++i) {
                const int index = inspector_endian_ == InspectorEndian::Little ? i : (width - 1 - i);
                replacement[index] = static_cast<char>((parsed >> (i * 8)) & 0xFFU);
            }
        } else if (field.startsWith(QLatin1Char('i'))) {
            const int bits = field.mid(1).toInt(&ok);
            if (!ok || bits % 8 != 0) {
                return fail(QStringLiteral("Unsupported signed integer width."));
            }
            const int width = bits / 8;
            bool parse_ok = false;
            const qlonglong parsed = value.trimmed().toLongLong(&parse_ok, 0);
            if (!parse_ok) {
                return fail(QStringLiteral("Invalid signed integer value."));
            }
            if (bits < 64) {
                const qlonglong min_value = -(1LL << (bits - 1));
                const qlonglong max_value = (1LL << (bits - 1)) - 1LL;
                if (parsed < min_value || parsed > max_value) {
                    return fail(QStringLiteral("Value is out of range for %1.").arg(field));
                }
            }
            const quint64 raw = static_cast<quint64>(parsed);
            replacement.resize(width);
            for (int i = 0; i < width; ++i) {
                const int index = inspector_endian_ == InspectorEndian::Little ? i : (width - 1 - i);
                replacement[index] = static_cast<char>((raw >> (i * 8)) & 0xFFU);
            }
        }
    } else if (section == QStringLiteral("Floating Point")) {
        bool ok = false;
        const double parsed = value.trimmed().toDouble(&ok);
        if (!ok) {
            return fail(QStringLiteral("Invalid floating-point value."));
        }
        if (field == QStringLiteral("f32")) {
            float number = static_cast<float>(parsed);
            replacement.resize(4);
            std::memcpy(replacement.data(), &number, sizeof(float));
            if (inspector_endian_ == InspectorEndian::Big) {
                std::reverse(replacement.begin(), replacement.end());
            }
        } else if (field == QStringLiteral("f64")) {
            double number = parsed;
            replacement.resize(8);
            std::memcpy(replacement.data(), &number, sizeof(double));
            if (inspector_endian_ == InspectorEndian::Big) {
                std::reverse(replacement.begin(), replacement.end());
            }
        } else {
            return fail(QStringLiteral("Unsupported floating-point field."));
        }
    } else if (section == QStringLiteral("Network") && field == QStringLiteral("IPv4")) {
        const QStringList parts = value.trimmed().split(QLatin1Char('.'));
        if (parts.size() != 4) {
            return fail(QStringLiteral("IPv4 must contain four dot-separated octets."));
        }

        replacement.resize(4);
        for (int index = 0; index < 4; ++index) {
            bool ok = false;
            const int octet = parts.at(index).toInt(&ok);
            if (!ok || octet < 0 || octet > 255) {
                return fail(QStringLiteral("IPv4 octets must be between 0 and 255."));
            }
            replacement[index] = static_cast<char>(octet);
        }
    } else {
        return fail(QStringLiteral("This inspector field is read-only."));
    }

    if (replacement.isEmpty()) {
        return fail(QStringLiteral("No writable bytes were produced for this field."));
    }

    if (!source_.overwrite_range(start_offset, replacement)) {
        return fail(QStringLiteral("Failed to write the updated bytes."));
    }

    selection_anchor_ = start_offset;
    selection_active_ = replacement.size() > 1;
    caret_offset_ = start_offset + qMax(0, replacement.size() - 1);
    high_nibble_pending_ = true;
    pending_insert_high_nibble_ = -1;
    refresh_after_edit();
    return true;
}

void HexView::toggle_bookmark_at_caret() {
    if (!has_document()) {
        return;
    }

    if (bookmarks_.contains(caret_offset_)) {
        bookmarks_.remove(caret_offset_);
    } else {
        bookmarks_.insert(caret_offset_, BookmarkEntry{next_bookmark_color(bookmarks_.size()), QString()});
    }

    emit_bookmarks();
    viewport()->update();
}

void HexView::next_bookmark() {
    if (!has_document() || bookmarks_.isEmpty()) {
        return;
    }

    const QList<qint64> sorted = bookmarks_.keys();
    for (const qint64 bookmark : sorted) {
        if (bookmark > caret_offset_) {
            move_caret(bookmark, false);
            return;
        }
    }

    move_caret(sorted.first(), false);
}

void HexView::previous_bookmark() {
    if (!has_document() || bookmarks_.isEmpty()) {
        return;
    }

    const QList<qint64> sorted = bookmarks_.keys();
    for (auto it = sorted.crbegin(); it != sorted.crend(); ++it) {
        if (*it < caret_offset_) {
            move_caret(*it, false);
            return;
        }
    }

    move_caret(sorted.last(), false);
}

QVector<HexView::BookmarkRow> HexView::bookmark_rows() const {
    QVector<BookmarkRow> rows;
    rows.reserve(bookmarks_.size());
    for (auto it = bookmarks_.cbegin(); it != bookmarks_.cend(); ++it) {
        rows.push_back(BookmarkRow{
            it.key(),
            it.key() / bytes_per_row_,
            it.value().color,
            it.value().label,
        });
    }
    return rows;
}

bool HexView::set_bookmark_label(qint64 offset, const QString& label) {
    auto it = bookmarks_.find(offset);
    if (it == bookmarks_.end()) {
        return false;
    }
    it->label = label.trimmed();
    emit_bookmarks();
    return true;
}

bool HexView::set_bookmark_color(qint64 offset, const QColor& color) {
    auto it = bookmarks_.find(offset);
    if (it == bookmarks_.end() || !color.isValid()) {
        return false;
    }
    it->color = color;
    emit_bookmarks();
    viewport()->update();
    return true;
}

bool HexView::remove_bookmark(qint64 offset) {
    if (bookmarks_.remove(offset) == 0) {
        return false;
    }
    emit_bookmarks();
    viewport()->update();
    return true;
}

void HexView::set_bytes_per_row(qint64 bytes_per_row) {
    const qint64 clamped = qBound<qint64>(4LL, bytes_per_row, 64LL);
    if (clamped == bytes_per_row_) {
        return;
    }

    const qint64 caret_row = caret_offset_ / bytes_per_row_;
    bytes_per_row_ = clamped;
    invalidate_row_cache();
    refresh_metrics();
    first_visible_row_ = caret_row;
    refresh_scrollbar();
    ensure_caret_visible();
    emit_status();
    viewport()->update();
}

void HexView::go_to_offset(qint64 offset) {
    if (!has_document()) {
        return;
    }

    clear_selection();
    move_caret(offset, false);
}

void HexView::select_range(qint64 offset, qint64 length) {
    if (!has_document()) {
        return;
    }

    if (length <= 0) {
        go_to_offset(offset);
        return;
    }

    const qint64 start = clamp_offset(offset);
    const qint64 end = clamp_offset(offset + length - 1);
    selection_anchor_ = start;
    selection_active_ = start != end;
    caret_offset_ = end;
    high_nibble_pending_ = true;
    pending_insert_high_nibble_ = -1;
    ensure_caret_visible();
    emit_status();
    viewport()->update();
}

HexView::EditMode HexView::edit_mode() const {
    return edit_mode_;
}

void HexView::set_edit_mode(EditMode mode) {
    if (edit_mode_ == mode) {
        return;
    }

    edit_mode_ = mode;
    pending_insert_high_nibble_ = -1;
    high_nibble_pending_ = true;
    emit_status();
}

void HexView::toggle_edit_mode() {
    set_edit_mode(edit_mode_ == EditMode::Insert ? EditMode::Overwrite : EditMode::Insert);
}

bool HexView::is_read_only() const {
    return source_.is_read_only();
}

void HexView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(viewport());
    const QColor base_color(250, 250, 246);
    const QColor text_color(29, 38, 52);
    const QColor secondary_text(92, 105, 123);
    const QColor header_background(222, 229, 238);
    const QColor gutter_background(232, 237, 243);
    const QColor grid_line(191, 200, 214);
    const QColor current_row_fill(243, 247, 253);
    const QColor hover_fill(230, 238, 248);
    const QColor selection_color(82, 120, 176);
    const QColor selection_text_color(255, 255, 255);
    const QColor caret_fill(172, 196, 227);

    painter.fillRect(rect(), base_color);

    if (!has_document()) {
        painter.setPen(text_color);
        painter.drawText(rect().adjusted(24, 24, -24, -24),
            Qt::AlignLeft | Qt::AlignTop,
            QStringLiteral("Open a file to begin browsing.\n\nThe viewport renders only the visible byte range."));
        return;
    }

    first_visible_row_ = qBound<qint64>(0, first_visible_row_, max_first_visible_row());
    const qint64 first_row = first_visible_row_;
    const qint64 rows = visible_row_count();
    ensure_row_cache(first_row, rows);

    const qint64 selected_start = selection_start();
    const qint64 selected_end = selection_end();

    painter.fillRect(QRect(0, 0, viewport()->width(), metrics_.row_header), header_background);
    painter.fillRect(QRect(0, metrics_.row_header, metrics_.hex_start - 12, viewport()->height() - metrics_.row_header), gutter_background);
    if (metrics_.marker_width > 0) {
        painter.fillRect(QRect(metrics_.marker_start, 0, metrics_.marker_width, viewport()->height()), QColor(224, 230, 239));
    }
    painter.fillRect(QRect(metrics_.hex_start - 6, 0, 1, viewport()->height()), grid_line);
    painter.fillRect(QRect(metrics_.ascii_start - 10, 0, 1, viewport()->height()), grid_line);
    painter.fillRect(QRect(metrics_.ascii_start + static_cast<int>(bytes_per_row_) * metrics_.ascii_cell_width, 0, 1, viewport()->height()), grid_line);
    painter.setPen(secondary_text);
    painter.drawLine(0, metrics_.row_header, viewport()->width(), metrics_.row_header);
    if (metrics_.row_number_width > 0) {
        painter.drawText(QRect(metrics_.row_number_start, 0, metrics_.row_number_width, metrics_.row_header - 2),
            Qt::AlignCenter, QStringLiteral("Row"));
    }
    if (metrics_.address_width > 0) {
        painter.drawText(QRect(metrics_.address_start, 0, metrics_.address_width, metrics_.row_header - 2),
            Qt::AlignCenter, QStringLiteral("Offset"));
    }
    painter.drawText(QRect(metrics_.hex_start, 0, static_cast<int>(bytes_per_row_) * metrics_.hex_cell_width, metrics_.row_header / 2),
        Qt::AlignCenter, QStringLiteral("Hex Values"));
    painter.drawText(QRect(metrics_.ascii_start, 0, static_cast<int>(bytes_per_row_) * metrics_.ascii_cell_width, metrics_.row_header - 2),
        Qt::AlignCenter, QStringLiteral("Text"));
    for (qint64 column = 0; column < bytes_per_row_; ++column) {
        painter.setPen(text_color);
        painter.drawText(
            header_cell_rect(column).adjusted(0, metrics_.row_header / 2 - 2, 0, 0),
            Qt::AlignCenter,
            QStringLiteral("%1").arg(column, 2, 16, QChar(u'0')).toUpper());
    }

    for (int row = 0; row < cached_rows_.size(); ++row) {
        const CachedRow& cached_row = cached_rows_.at(row);
        const int y = metrics_.row_header + row * metrics_.line_height;
        const bool current_row = caret_offset_ >= cached_row.row_offset &&
            caret_offset_ < cached_row.row_offset + cached_row.bytes.size();
        if (current_row) {
            painter.fillRect(QRect(metrics_.hex_start - 6, y, viewport()->width() - (metrics_.hex_start - 6), metrics_.line_height), current_row_fill);
        }
        if (row % 2 == 1) {
            QColor stripe = header_background;
            stripe.setAlpha(55);
            painter.fillRect(QRect(metrics_.hex_start - 6, y, viewport()->width() - (metrics_.hex_start - 6), metrics_.line_height), stripe);
        }

        painter.setPen(secondary_text);
        const bool row_bookmarked = has_bookmark(cached_row.row_offset);
        if (metrics_.marker_width > 0) {
            const QRect marker_rect(metrics_.marker_start, y, metrics_.marker_width, metrics_.line_height);
            if (row_bookmarked) {
                const BookmarkEntry* entry = bookmark_entry_for_row(cached_row.row_offset);
                const QColor marker_color = entry != nullptr ? entry->color : QColor(220, 68, 55);
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setBrush(marker_color);
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(marker_rect.center(), 4, 4);
                painter.setRenderHint(QPainter::Antialiasing, false);
            } else if (current_row) {
                painter.drawText(marker_rect, Qt::AlignCenter, QStringLiteral(">"));
            }
        }
        if (metrics_.row_number_width > 0) {
            painter.drawText(QRect(metrics_.row_number_start, y, metrics_.row_number_width, metrics_.line_height),
                Qt::AlignCenter, cached_row.row_number_text);
        }
        if (metrics_.address_width > 0) {
            painter.drawText(QRect(metrics_.address_start, y, metrics_.address_width, metrics_.line_height),
                Qt::AlignCenter, cached_row.offset_text);
        }

        for (int column = 0; column < cached_row.bytes.size(); ++column) {
            const qint64 offset = cached_row.row_offset + column;
            const quint8 value = static_cast<quint8>(cached_row.bytes.at(column));
            const QRect hex_rect = hex_cell_rect(row, column);
            const QRect ascii_rect = ascii_cell_rect(row, column);
            const bool selected = selection_active_ && offset >= selected_start && offset < selected_end;
            const bool is_caret = offset == caret_offset_;
            const bool hovered = offset == hovered_offset_;

            if (hovered && !selected && !is_caret) {
                painter.fillRect(hex_rect.adjusted(1, 1, -1, -1), hover_fill);
                painter.fillRect(ascii_rect.adjusted(1, 1, -1, -1), hover_fill);
            }

            if (selected || is_caret) {
                QColor fill = selection_color;
                if (is_caret && !selected) {
                    fill = caret_fill;
                }
                painter.fillRect(hex_rect.adjusted(1, 1, -1, -1), fill);
                painter.fillRect(ascii_rect.adjusted(1, 1, -1, -1), fill);
            }

            painter.setPen((selected || is_caret) ? selection_text_color : text_color);
            painter.drawText(hex_rect, Qt::AlignCenter, hex_byte(value));
            painter.drawText(ascii_rect, Qt::AlignCenter, QString(cached_row.ascii_text.at(column)));
        }

        painter.fillRect(QRect(metrics_.hex_start - 6, y + metrics_.line_height - 1, viewport()->width() - (metrics_.hex_start - 6), 1), QColor(236, 240, 245));
    }
}

void HexView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    refresh_metrics();
    invalidate_row_cache();
    refresh_scrollbar();
}

void HexView::keyPressEvent(QKeyEvent* event) {
    if (!has_document()) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    if (!is_read_only() &&
        !event->modifiers().testFlag(Qt::ControlModifier) &&
        !event->modifiers().testFlag(Qt::AltModifier) &&
        !event->modifiers().testFlag(Qt::MetaModifier)) {
        if (active_pane_ == ActivePane::Text) {
            if (apply_text_input(event->text())) {
                return;
            }
        } else {
            const int nibble_value = hex_value_for_key(event->key());
            if (nibble_value >= 0 && apply_hex_input(nibble_value)) {
                return;
            }
        }
    }

    const bool extend_selection = event->modifiers().testFlag(Qt::ShiftModifier);

    switch (event->key()) {
    case Qt::Key_Left:
        move_caret(caret_offset_ - 1, extend_selection);
        return;
    case Qt::Key_Right:
        move_caret(caret_offset_ + 1, extend_selection);
        return;
    case Qt::Key_Up:
        move_caret(caret_offset_ - bytes_per_row_, extend_selection);
        return;
    case Qt::Key_Down:
        move_caret(caret_offset_ + bytes_per_row_, extend_selection);
        return;
    case Qt::Key_PageUp:
        move_caret(caret_offset_ - visible_row_count() * bytes_per_row_, extend_selection);
        return;
    case Qt::Key_PageDown:
        move_caret(caret_offset_ + visible_row_count() * bytes_per_row_, extend_selection);
        return;
    case Qt::Key_Home:
        move_caret(0, extend_selection);
        return;
    case Qt::Key_End:
        move_caret(document_size() - 1, extend_selection);
        return;
    case Qt::Key_Insert:
        toggle_edit_mode();
        return;
    case Qt::Key_Delete:
        if (has_selection()) {
            delete_selection();
            return;
        }
        if (edit_mode_ == EditMode::Insert) {
            delete_at_caret();
            return;
        }
        break;
    case Qt::Key_Backspace:
        if (has_selection()) {
            delete_selection();
            return;
        }
        if (edit_mode_ == EditMode::Insert) {
            backspace_at_caret();
            return;
        }
        break;
    default:
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    QAbstractScrollArea::keyPressEvent(event);
}

void HexView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    if (event->pos().y() <= metrics_.row_header) {
        active_divider_ = divider_at(event->pos());
        if (active_divider_ != HeaderDivider::None) {
            update_resize_cursor(event->pos());
            event->accept();
            return;
        }
    }

    if (!has_document()) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    active_pane_ = pane_at(event->pos());
    const qint64 clicked = offset_at(event->pos());
    selection_anchor_ = clicked;
    selection_active_ = false;
    high_nibble_pending_ = true;
    move_caret(clicked, false);
}

void HexView::mouseMoveEvent(QMouseEvent* event) {
    if (active_divider_ != HeaderDivider::None) {
        const int min_row_width = 44;
        const int min_address_width = 92;
        switch (active_divider_) {
        case HeaderDivider::RowOffset:
            if (show_row_numbers_) {
                custom_row_number_width_ = qMax(min_row_width, event->pos().x() - metrics_.row_number_start);
                refresh_metrics();
                viewport()->update();
            }
            break;
        case HeaderDivider::OffsetHex:
            if (show_offsets_) {
                custom_address_width_ = qMax(min_address_width, event->pos().x() - metrics_.address_start - 12);
                refresh_metrics();
                viewport()->update();
            }
            break;
        case HeaderDivider::HexText: {
            const int desired = qBound(4, static_cast<int>((event->pos().x() - metrics_.hex_start - 24 + (metrics_.hex_cell_width / 2)) / metrics_.hex_cell_width), 64);
            if (desired != bytes_per_row_) {
                set_bytes_per_row(desired);
            }
            break;
        }
        case HeaderDivider::None:
            break;
        }
        update_resize_cursor(event->pos());
        event->accept();
        return;
    }

    const qint64 hovered = has_document() ? offset_at(event->pos()) : -1;
    if (hovered != hovered_offset_) {
        hovered_offset_ = hovered;
        viewport()->update();
    }

    update_resize_cursor(event->pos());

    if (!has_document() || !(event->buttons() & Qt::LeftButton) || selection_anchor_ < 0) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }

    selection_active_ = true;
    move_caret(offset_at(event->pos()), true);
}

void HexView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && active_divider_ != HeaderDivider::None) {
        active_divider_ = HeaderDivider::None;
        update_resize_cursor(event->pos());
        event->accept();
        return;
    }

    QAbstractScrollArea::mouseReleaseEvent(event);
}

void HexView::wheelEvent(QWheelEvent* event) {
    const QPoint steps = event->angleDelta() / 120;
    if (steps.y() != 0) {
        scroll_to_row(first_visible_row_ - steps.y() * 3);
        event->accept();
        return;
    }

    QAbstractScrollArea::wheelEvent(event);
}

void HexView::leaveEvent(QEvent* event) {
    hovered_offset_ = -1;
    if (active_divider_ == HeaderDivider::None) {
        viewport()->unsetCursor();
    }
    viewport()->update();
    QAbstractScrollArea::leaveEvent(event);
}

void HexView::refresh_metrics() {
    const QFontMetrics metrics(font());
    metrics_.line_height = metrics.height() + 8;
    metrics_.char_width = qMax(1, metrics.horizontalAdvance(QLatin1Char('9')));
    metrics_.hex_cell_width = metrics.horizontalAdvance(QStringLiteral("FF")) + 16;
    metrics_.ascii_cell_width = metrics.horizontalAdvance(QLatin1Char('.')) + 10;
    metrics_.marker_width = show_bookmark_gutter_ ? 18 : 0;
    const int default_row_number_width = metrics.horizontalAdvance(QStringLiteral("999999")) + 24;
    const qint64 max_offset = qMax<qint64>(0, document_size() - 1);
    const QString offset_sample = QStringLiteral("%1").arg(static_cast<qulonglong>(max_offset), 0, 16).toUpper();
    const int default_address_width = metrics.horizontalAdvance(QStringLiteral("0x") + offset_sample) + 18;
    metrics_.row_number_width = show_row_numbers_ ? qMax(44, custom_row_number_width_ > 0 ? custom_row_number_width_ : default_row_number_width) : 0;
    metrics_.address_width = show_offsets_ ? (custom_address_width_ > 0 ? qMax(64, custom_address_width_) : default_address_width) : 0;
    const int minimum_hex_space = 4 * metrics_.hex_cell_width + 4 * metrics_.ascii_cell_width + 48;
    const int max_gutter_width = qMax(0, viewport()->width() - minimum_hex_space);
    int gutter_width = metrics_.marker_width + metrics_.row_number_width + metrics_.address_width;
    if (gutter_width > max_gutter_width) {
        int overflow = gutter_width - max_gutter_width;
        if (metrics_.address_width > 0) {
            const int shrink = qMin(overflow, qMax(0, metrics_.address_width - 48));
            metrics_.address_width -= qMax(0, shrink);
            overflow -= qMax(0, shrink);
        }
        if (overflow > 0 && metrics_.row_number_width > 0) {
            const int shrink = qMin(overflow, qMax(0, metrics_.row_number_width - 32));
            metrics_.row_number_width -= qMax(0, shrink);
        }
    }
    metrics_.marker_start = 0;
    metrics_.row_number_start = metrics_.marker_start + metrics_.marker_width;
    metrics_.address_start = metrics_.row_number_start + metrics_.row_number_width;
    metrics_.hex_start = metrics_.address_start + metrics_.address_width + 12;
    metrics_.ascii_start = metrics_.hex_start + static_cast<int>(bytes_per_row_) * metrics_.hex_cell_width + 24;
    metrics_.row_header = metrics_.line_height + 18;
}

void HexView::refresh_scrollbar() {
    const qint64 rows = total_rows();
    const qint64 visible = fully_visible_row_count();
    const qint64 max_row = max_first_visible_row();
    first_visible_row_ = qBound<qint64>(0, first_visible_row_, max_row);

    if (max_row <= kDirectScrollbarMax) {
        verticalScrollBar()->setPageStep(static_cast<int>(qMin<qint64>(visible, kDirectScrollbarMax)));
        verticalScrollBar()->setSingleStep(1);
        verticalScrollBar()->setRange(0, static_cast<int>(max_row));
    } else {
        verticalScrollBar()->setPageStep(static_cast<int>(qMin<qint64>(visible, kScrollbarResolution)));
        verticalScrollBar()->setSingleStep(1);
        verticalScrollBar()->setRange(0, rows > visible ? kScrollbarResolution : 0);
    }
    const int target_value = row_to_slider(first_visible_row_);
    if (verticalScrollBar()->value() != target_value) {
        verticalScrollBar()->setValue(target_value);
    }
}

void HexView::invalidate_row_cache() {
    cached_first_row_ = -1;
    cached_row_count_ = -1;
    cached_generation_ = -1;
    cached_rows_.clear();
}

qint64 HexView::max_first_visible_row() const {
    return qMax<qint64>(0, total_rows() - fully_visible_row_count());
}

void HexView::ensure_row_cache(qint64 first_row, qint64 row_count) {
    if (cached_first_row_ == first_row &&
        cached_row_count_ == row_count &&
        cached_generation_ == render_generation_) {
        return;
    }

    cached_rows_.clear();
    cached_first_row_ = first_row;
    cached_row_count_ = row_count;
    cached_generation_ = render_generation_;

    const qint64 start_offset = row_to_offset(first_row);
    const qint64 bytes_to_read = qMin(row_count * bytes_per_row_, document_size() - start_offset);
    const QByteArray bytes = source_.read_range(start_offset, bytes_to_read);

    for (qint64 row = 0; row < row_count; ++row) {
        const qint64 row_offset = start_offset + row * bytes_per_row_;
        if (row_offset >= document_size()) {
            break;
        }

        const int byte_start = static_cast<int>(row * bytes_per_row_);
        const int byte_count = static_cast<int>(qMin<qint64>(bytes_per_row_, bytes.size() - byte_start));
        if (byte_count <= 0) {
            break;
        }

        CachedRow cached_row;
        cached_row.row_number = first_row + row;
        cached_row.row_offset = row_offset;
        cached_row.bytes = bytes.mid(byte_start, byte_count);
        cached_row.row_number_text = QStringLiteral("%1").arg(cached_row.row_number);
        cached_row.offset_text = formatted_offset(cached_row.row_offset);
        cached_row.ascii_text.reserve(byte_count);
        for (int i = 0; i < byte_count; ++i) {
            cached_row.ascii_text.append(printable_char(static_cast<quint8>(cached_row.bytes.at(i))));
        }

        cached_rows_.push_back(std::move(cached_row));
    }
}

void HexView::emit_status() {
    emit status_changed(
        static_cast<qulonglong>(caret_offset_),
        static_cast<qulonglong>(selection_size()),
        static_cast<qulonglong>(document_size()));
    emit_inspector();
}

void HexView::emit_bookmarks() {
    emit bookmarks_changed();
}

void HexView::emit_inspector() {
    emit inspector_changed(format_inspector_text());
}

void HexView::refresh_after_edit(bool ensure_visible) {
    ++render_generation_;
    invalidate_row_cache();
    if (ensure_visible) {
        ensure_caret_visible();
    }
    emit_status();
    viewport()->update();
}

void HexView::move_caret(qint64 offset, bool extend_selection) {
    if (!has_document()) {
        return;
    }

    const qint64 clamped = clamp_offset(offset);
    if (!extend_selection || selection_anchor_ < 0) {
        selection_anchor_ = clamped;
        selection_active_ = false;
        high_nibble_pending_ = true;
        pending_insert_high_nibble_ = -1;
    } else {
        selection_active_ = selection_anchor_ != clamped;
    }

    caret_offset_ = clamped;
    ensure_caret_visible();
    emit_status();
    viewport()->update();
}

void HexView::ensure_caret_visible() {
    const qint64 row = caret_offset_ / bytes_per_row_;
    const qint64 visible = visible_row_count();

    if (row < first_visible_row_) {
        scroll_to_row(row);
    } else if (row >= first_visible_row_ + visible) {
        scroll_to_row(row - visible + 1);
    }
}

void HexView::scroll_to_row(qint64 row) {
    const qint64 max_row = max_first_visible_row();
    const qint64 next_row = qBound<qint64>(0, row, max_row);
    if (next_row == first_visible_row_) {
        return;
    }

    first_visible_row_ = next_row;
    const int target_value = row_to_slider(first_visible_row_);
    if (verticalScrollBar()->value() != target_value) {
        verticalScrollBar()->setValue(target_value);
    } else {
        viewport()->update();
    }
}

qint64 HexView::visible_row_count() const {
    const int height = qMax(0, viewport()->height() - metrics_.row_header);
    return qMax<qint64>(1, (height + metrics_.line_height - 1) / metrics_.line_height);
}

qint64 HexView::fully_visible_row_count() const {
    const int height = qMax(0, viewport()->height() - metrics_.row_header);
    return qMax<qint64>(1, height / metrics_.line_height);
}

qint64 HexView::total_rows() const {
    if (!has_document() || document_size() <= 0) {
        return 1;
    }

    return (document_size() + bytes_per_row_ - 1) / bytes_per_row_;
}

qint64 HexView::first_visible_row() const {
    return first_visible_row_;
}

qint64 HexView::last_visible_row() const {
    return first_visible_row_ + visible_row_count() - 1;
}

qint64 HexView::row_to_offset(qint64 row) const {
    return row * bytes_per_row_;
}

qint64 HexView::clamp_offset(qint64 offset) const {
    if (!has_document() || document_size() <= 0) {
        return 0;
    }

    return qBound<qint64>(0, offset, document_size() - 1);
}

qint64 HexView::slider_to_row(int slider_value) const {
    const qint64 max_row = max_first_visible_row();
    if (max_row == 0) {
        return 0;
    }

    if (max_row <= kDirectScrollbarMax) {
        return qBound<qint64>(0, static_cast<qint64>(slider_value), max_row);
    }

    const double ratio = static_cast<double>(slider_value) / static_cast<double>(kScrollbarResolution);
    return static_cast<qint64>(qRound64(ratio * static_cast<double>(max_row)));
}

int HexView::row_to_slider(qint64 row) const {
    const qint64 max_row = max_first_visible_row();
    if (max_row == 0) {
        return 0;
    }

    if (max_row <= kDirectScrollbarMax) {
        return static_cast<int>(qBound<qint64>(0, row, max_row));
    }

    const double ratio = static_cast<double>(row) / static_cast<double>(max_row);
    return static_cast<int>(qBound(0.0, ratio * static_cast<double>(kScrollbarResolution), static_cast<double>(kScrollbarResolution)));
}

qint64 HexView::selection_start() const {
    if (!selection_active_ || selection_anchor_ < 0) {
        return caret_offset_;
    }

    return qMin(selection_anchor_, caret_offset_);
}

qint64 HexView::selection_end() const {
    if (!selection_active_ || selection_anchor_ < 0) {
        return caret_offset_;
    }

    return qMax(selection_anchor_, caret_offset_) + 1;
}

qint64 HexView::selection_size() const {
    if (!has_document()) {
        return 0;
    }

    if (!selection_active_ || selection_anchor_ < 0) {
        return 0;
    }

    return selection_end() - selection_start();
}

void HexView::clear_selection() {
    selection_anchor_ = -1;
    selection_active_ = false;
}

QRect HexView::header_cell_rect(qint64 column) const {
    return QRect(
        metrics_.hex_start + static_cast<int>(column) * metrics_.hex_cell_width,
        0,
        metrics_.hex_cell_width,
        metrics_.row_header - 2);
}

HexView::HeaderDivider HexView::divider_at(const QPoint& point) const {
    if (point.y() < 0 || point.y() > metrics_.row_header) {
        return HeaderDivider::None;
    }

    constexpr int handle_slop = 4;
    if (show_row_numbers_) {
        const int row_offset_divider = metrics_.address_start;
        if (qAbs(point.x() - row_offset_divider) <= handle_slop) {
            return HeaderDivider::RowOffset;
        }
    }
    if (show_offsets_) {
        const int offset_hex_divider = metrics_.hex_start - 6;
        if (qAbs(point.x() - offset_hex_divider) <= handle_slop) {
            return HeaderDivider::OffsetHex;
        }
    }
    const int hex_text_divider = metrics_.ascii_start - 10;
    if (qAbs(point.x() - hex_text_divider) <= handle_slop) {
        return HeaderDivider::HexText;
    }

    return HeaderDivider::None;
}

void HexView::update_resize_cursor(const QPoint& point) {
    if (active_divider_ != HeaderDivider::None || divider_at(point) != HeaderDivider::None) {
        viewport()->setCursor(Qt::SplitHCursor);
    } else {
        viewport()->unsetCursor();
    }
}

QRect HexView::hex_cell_rect(int row_index, qint64 column) const {
    return QRect(
        metrics_.hex_start + static_cast<int>(column) * metrics_.hex_cell_width,
        metrics_.row_header + row_index * metrics_.line_height,
        metrics_.hex_cell_width,
        metrics_.line_height);
}

QRect HexView::ascii_cell_rect(int row_index, qint64 column) const {
    return QRect(
        metrics_.ascii_start + static_cast<int>(column) * metrics_.ascii_cell_width,
        metrics_.row_header + row_index * metrics_.line_height,
        metrics_.ascii_cell_width,
        metrics_.line_height);
}

int HexView::hex_value_for_key(int key) const {
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return key - Qt::Key_0;
    }

    if (key >= Qt::Key_A && key <= Qt::Key_F) {
        return 10 + (key - Qt::Key_A);
    }

    return -1;
}

bool HexView::apply_hex_input(int nibble_value) {
    if (is_read_only()) {
        return false;
    }

    if (edit_mode_ == EditMode::Insert) {
        if (pending_insert_high_nibble_ < 0) {
            pending_insert_high_nibble_ = static_cast<qint8>(nibble_value);
            emit_status();
            viewport()->update();
            return true;
        }

        const quint8 byte = static_cast<quint8>((pending_insert_high_nibble_ << 4) | nibble_value);
        pending_insert_high_nibble_ = -1;
        return insert_at_caret(QByteArray(1, static_cast<char>(byte)));
    }

    quint8 current = 0;
    if (!source_.read_byte(caret_offset_, current)) {
        return false;
    }

    quint8 next = current;
    if (high_nibble_pending_) {
        next = static_cast<quint8>((current & 0x0F) | (nibble_value << 4));
        high_nibble_pending_ = false;
    } else {
        next = static_cast<quint8>((current & 0xF0) | nibble_value);
        high_nibble_pending_ = true;
    }

    if (!source_.overwrite_byte(caret_offset_, next)) {
        return false;
    }

    ++render_generation_;
    invalidate_row_cache();

    if (high_nibble_pending_ && caret_offset_ + 1 < document_size()) {
        move_caret(caret_offset_ + 1, false);
    } else {
        emit_status();
        viewport()->update();
    }

    return true;
}

bool HexView::apply_text_input(const QString& text) {
    if (is_read_only() || text.isEmpty()) {
        return false;
    }

    QByteArray bytes;
    bytes.reserve(text.size());
    for (const QChar character : text) {
        if (character.isNull() || character.isLowSurrogate() || character.isHighSurrogate()) {
            continue;
        }

        const ushort codepoint = character.unicode();
        if (codepoint < 32 || codepoint == 127) {
            continue;
        }

        const char latin1 = character.toLatin1();
        bytes.append(latin1 == '\0' && codepoint != 0 ? '?' : latin1);
    }

    if (bytes.isEmpty()) {
        return false;
    }

    qint64 start = has_selection() ? selection_start() : caret_offset_;
    if (edit_mode_ == EditMode::Insert) {
        if (has_selection()) {
            if (!source_.delete_range(start, selection_size())) {
                return false;
            }
        }

        if (!source_.insert_range(start, bytes)) {
            return false;
        }
    } else {
        if (has_selection()) {
            const qint64 length = selection_size();
            if (bytes.size() > length) {
                bytes.truncate(static_cast<int>(length));
            }
            if (bytes.isEmpty()) {
                return false;
            }
            if (!source_.overwrite_range(start, bytes)) {
                return false;
            }
        } else {
            if (start < 0 || start >= document_size()) {
                return false;
            }
            if (!source_.overwrite_range(start, bytes.left(1))) {
                return false;
            }
            bytes = bytes.left(1);
        }
    }

    selection_anchor_ = -1;
    selection_active_ = false;
    pending_insert_high_nibble_ = -1;
    high_nibble_pending_ = true;
    caret_offset_ = qMin(start + static_cast<qint64>(bytes.size()), qMax<qint64>(0, document_size() - 1));
    refresh_after_edit();
    return true;
}

qint64 HexView::offset_at(const QPoint& point) const {
    if (!has_document()) {
        return 0;
    }

    const int row_in_view = qMax(0, (point.y() - metrics_.row_header) / metrics_.line_height);
    const qint64 row = first_visible_row_ + row_in_view;
    int column = 0;

    if (point.x() >= metrics_.ascii_start) {
        column = (point.x() - metrics_.ascii_start) / metrics_.ascii_cell_width;
    } else if (point.x() >= metrics_.hex_start) {
        column = (point.x() - metrics_.hex_start) / metrics_.hex_cell_width;
    }

    column = qBound(0, column, static_cast<int>(bytes_per_row_ - 1));
    return clamp_offset(row_to_offset(row) + column);
}

HexView::ActivePane HexView::pane_at(const QPoint& point) const {
    if (point.x() >= metrics_.ascii_start) {
        return ActivePane::Text;
    }

    return ActivePane::Hex;
}

QString HexView::hex_byte(quint8 value) const {
    return QStringLiteral("%1").arg(value, 2, 16, QChar(u'0')).toUpper();
}

QString HexView::formatted_offset(qint64 offset) const {
    const qint64 max_offset = qMax<qint64>(0, document_size() - 1);
    const int digits = qMax(1, QString::number(static_cast<qulonglong>(max_offset), 16).size());
    return QStringLiteral("%1").arg(static_cast<qulonglong>(offset), digits, 16, QChar(u'0')).toUpper();
}

QChar HexView::printable_char(quint8 value) const {
    return (value >= 32 && value < 127) ? QChar(value) : QChar(u'.');
}

bool HexView::has_bookmark(qint64 offset) const {
    const qint64 row_start = (offset / bytes_per_row_) * bytes_per_row_;
    for (qint64 index = 0; index < bytes_per_row_; ++index) {
        if (bookmarks_.contains(row_start + index)) {
            return true;
        }
    }

    return false;
}

const HexView::BookmarkEntry* HexView::bookmark_entry_for_row(qint64 row_start) const {
    for (qint64 index = 0; index < bytes_per_row_; ++index) {
        auto it = bookmarks_.constFind(row_start + index);
        if (it != bookmarks_.cend()) {
            return &it.value();
        }
    }
    return nullptr;
}

QVector<HexView::InspectorRow> HexView::build_inspector_rows() const {
    QVector<InspectorRow> rows;
    auto add_row = [&rows](const QString& section, const QString& field, const QString& value) {
        rows.push_back(InspectorRow{section, field, value});
    };

    if (!has_document()) {
        add_row(QStringLiteral("Overview"), QStringLiteral("Status"), QStringLiteral("Open a file to inspect values."));
        return rows;
    }

    const bool use_selection = has_selection();
    const qint64 selected_length = selection_size();
    const qint64 start_offset = use_selection ? selection_start() : caret_offset_;
    const qint64 inspect_length = qMin<qint64>(use_selection ? selected_length : 8, 8);
    const QByteArray bytes = source_.read_range(start_offset, inspect_length);
    if (bytes.isEmpty()) {
        add_row(QStringLiteral("Overview"), QStringLiteral("Status"), QStringLiteral("No bytes available at the current offset."));
        return rows;
    }

    auto read_unsigned = [&bytes, this](int count) -> quint64 {
        quint64 value = 0;
        if (inspector_endian_ == InspectorEndian::Little) {
            for (int i = 0; i < count && i < bytes.size(); ++i) {
                value |= static_cast<quint64>(static_cast<quint8>(bytes.at(i))) << (i * 8);
            }
            return value;
        }

        for (int i = 0; i < count && i < bytes.size(); ++i) {
            value = (value << 8) | static_cast<quint64>(static_cast<quint8>(bytes.at(i)));
        }
        return value;
    };
    auto read_signed = [&read_unsigned](int count) -> qint64 {
        const quint64 unsigned_value = read_unsigned(count);
        const int bits = count * 8;
        if (bits <= 0 || bits >= 64) {
            return static_cast<qint64>(unsigned_value);
        }

        const quint64 sign_mask = 1ULL << (bits - 1);
        if ((unsigned_value & sign_mask) == 0) {
            return static_cast<qint64>(unsigned_value);
        }

        const quint64 extend_mask = ~((1ULL << bits) - 1);
        return static_cast<qint64>(unsigned_value | extend_mask);
    };
    auto read_float = [&bytes, this](int count) -> double {
        std::array<unsigned char, 8> raw{};
        for (int i = 0; i < count && i < bytes.size(); ++i) {
            const int index = inspector_endian_ == InspectorEndian::Little ? i : (count - 1 - i);
            raw[static_cast<std::size_t>(index)] = static_cast<unsigned char>(bytes.at(i));
        }

        if (count == 4) {
            float value = 0.0F;
            std::memcpy(&value, raw.data(), sizeof(float));
            return value;
        }

        double value = 0.0;
        std::memcpy(&value, raw.data(), sizeof(double));
        return value;
    };

    QString ascii;
    ascii.reserve(bytes.size());
    QString hex_bytes;
    for (int i = 0; i < bytes.size(); ++i) {
        if (i > 0) {
            hex_bytes += QLatin1Char(' ');
        }
        const quint8 value = static_cast<quint8>(bytes.at(i));
        hex_bytes += hex_byte(value);
        ascii.append(printable_char(value));
    }

    add_row(QStringLiteral("Overview"), QStringLiteral("Offset"), QStringLiteral("0x%1").arg(start_offset, 8, 16, QChar(u'0')).toUpper());
    if (use_selection) {
        QString selection_text = QStringLiteral("%1 bytes").arg(selected_length);
        if (selected_length > bytes.size()) {
            selection_text += QStringLiteral(" (showing first %1)").arg(bytes.size());
        }
        add_row(QStringLiteral("Overview"), QStringLiteral("Selection"), selection_text);
    } else {
        add_row(QStringLiteral("Overview"), QStringLiteral("Selection"), QStringLiteral("No active selection"));
        add_row(QStringLiteral("Overview"), QStringLiteral("Window"), QStringLiteral("%1 bytes from caret").arg(bytes.size()));
    }
    add_row(QStringLiteral("Overview"), QStringLiteral("Endian"), inspector_endian_ == InspectorEndian::Little ? QStringLiteral("Little") : QStringLiteral("Big"));
    add_row(QStringLiteral("Overview"), QStringLiteral("Bytes"), hex_bytes);
    add_row(QStringLiteral("Overview"), QStringLiteral("ASCII"), ascii);
    if (bytes.size() >= 4) {
        add_row(
            QStringLiteral("Network"),
            QStringLiteral("IPv4"),
            QStringLiteral("%1.%2.%3.%4")
                .arg(static_cast<quint8>(bytes.at(0)))
                .arg(static_cast<quint8>(bytes.at(1)))
                .arg(static_cast<quint8>(bytes.at(2)))
                .arg(static_cast<quint8>(bytes.at(3))));
    }

    if (use_selection) {
        const int width = bytes.size();
        const int bits = width * 8;
        add_row(QStringLiteral("Integers"), QStringLiteral("u%1").arg(bits), QString::number(read_unsigned(width)));
        add_row(QStringLiteral("Integers"), QStringLiteral("i%1").arg(bits), QString::number(read_signed(width)));
        if (width == 4) {
            add_row(QStringLiteral("Floating Point"), QStringLiteral("f32"), QString::number(read_float(4), 'g', 9));
            add_row(
                QStringLiteral("Time"),
                QStringLiteral("Unix32"),
                QDateTime::fromSecsSinceEpoch(static_cast<qint64>(read_signed(4)), Qt::UTC).toString(Qt::ISODate));
        } else if (width == 8) {
            add_row(QStringLiteral("Floating Point"), QStringLiteral("f64"), QString::number(read_float(8), 'g', 17));
            add_row(
                QStringLiteral("Time"),
                QStringLiteral("Unix64"),
                QDateTime::fromSecsSinceEpoch(static_cast<qint64>(read_signed(8)), Qt::UTC).toString(Qt::ISODate));
        }
    } else {
        add_row(QStringLiteral("Integers"), QStringLiteral("u8"), QString::number(read_unsigned(1)));
        add_row(QStringLiteral("Integers"), QStringLiteral("i8"), QString::number(read_signed(1)));
        if (bytes.size() >= 2) {
            add_row(QStringLiteral("Integers"), QStringLiteral("u16"), QString::number(read_unsigned(2)));
            add_row(QStringLiteral("Integers"), QStringLiteral("i16"), QString::number(read_signed(2)));
        }
        if (bytes.size() >= 4) {
            add_row(QStringLiteral("Integers"), QStringLiteral("u32"), QString::number(read_unsigned(4)));
            add_row(QStringLiteral("Integers"), QStringLiteral("i32"), QString::number(read_signed(4)));
            add_row(QStringLiteral("Floating Point"), QStringLiteral("f32"), QString::number(read_float(4), 'g', 9));
            add_row(
                QStringLiteral("Time"),
                QStringLiteral("Unix32"),
                QDateTime::fromSecsSinceEpoch(static_cast<qint64>(read_signed(4)), Qt::UTC).toString(Qt::ISODate));
        }
        if (bytes.size() >= 8) {
            add_row(QStringLiteral("Integers"), QStringLiteral("u64"), QString::number(read_unsigned(8)));
            add_row(QStringLiteral("Integers"), QStringLiteral("i64"), QString::number(read_signed(8)));
            add_row(QStringLiteral("Floating Point"), QStringLiteral("f64"), QString::number(read_float(8), 'g', 17));
            add_row(
                QStringLiteral("Time"),
                QStringLiteral("Unix64"),
                QDateTime::fromSecsSinceEpoch(static_cast<qint64>(read_signed(8)), Qt::UTC).toString(Qt::ISODate));
        }
    }

    return rows;
}

QString HexView::format_inspector_text() const {
    QString text = QStringLiteral("Inspector\n\n");
    QString current_section;
    const QVector<InspectorRow> rows = build_inspector_rows();
    for (const InspectorRow& row : rows) {
        if (row.section != current_section) {
            if (!current_section.isEmpty()) {
                text += QLatin1Char('\n');
            }
            current_section = row.section;
        }
        text += QStringLiteral("%1: %2\n").arg(row.field, row.value);
    }
    return text;
}

bool HexView::try_parse_hex_string(const QString& text, QByteArray& bytes) {
    QString normalized = text.trimmed();
    if (normalized.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        normalized = normalized.mid(2);
    }
    normalized.remove(QLatin1Char(' '));
    if (normalized.isEmpty() || normalized.size() % 2 != 0) {
        return false;
    }

    QByteArray parsed;
    parsed.reserve(normalized.size() / 2);
    for (int index = 0; index < normalized.size(); index += 2) {
        bool ok = false;
        const auto byte = static_cast<char>(normalized.mid(index, 2).toUInt(&ok, 16));
        if (!ok) {
            return false;
        }
        parsed.append(byte);
    }

    bytes = parsed;
    return true;
}

qint64 HexView::search_from(
    const QByteArray& pattern,
    qint64 start_offset,
    bool forward,
    qint64 range_start,
    qint64 range_end,
    const SearchProgressCallback& progress_callback) const {
    if (!has_document() || pattern.isEmpty()) {
        return -1;
    }

    constexpr qint64 kChunkSize = 256 * 1024;
    const qint64 overlap = qMax<qint64>(0, pattern.size() - 1);
    const qint64 bounded_start = qMax<qint64>(0, range_start < 0 ? 0 : range_start);
    const qint64 bounded_end = qMin(document_size(), range_end < 0 ? document_size() : range_end);
    if (bounded_end <= bounded_start || pattern.size() > bounded_end - bounded_start) {
        return -1;
    }

    const qint64 clamped_start = forward
        ? qBound(bounded_start, start_offset, bounded_end)
        : qBound(bounded_start, start_offset, bounded_end - 1);
    const qint64 total = forward
        ? qMax<qint64>(0, bounded_end - clamped_start)
        : qMax<qint64>(0, clamped_start - bounded_start + 1);

    if (progress_callback && !progress_callback(0, total)) {
        return -1;
    }

    if (forward) {
        for (qint64 offset = clamped_start; offset < bounded_end;) {
            const qint64 to_read = qMin(bounded_end - offset, kChunkSize + overlap);
            const QByteArray chunk = source_.read_range(offset, to_read);
            if (chunk.isEmpty()) {
                break;
            }

            const qint64 index = chunk.indexOf(pattern);
            if (index >= 0) {
                return offset + index;
            }

            if (progress_callback && !progress_callback(qMin(total, (offset - clamped_start) + chunk.size()), total)) {
                return -1;
            }

            if (chunk.size() <= overlap) {
                break;
            }

            offset += chunk.size() - overlap;
        }
        return -1;
    }

    for (qint64 chunk_end = qMin(bounded_end, clamped_start + 1); chunk_end > bounded_start;) {
        const qint64 chunk_start = qMax(bounded_start, chunk_end - (kChunkSize + overlap));
        const QByteArray chunk = source_.read_range(chunk_start, chunk_end - chunk_start);
        if (chunk.isEmpty()) {
            break;
        }

        const qint64 max_index = qMin<qint64>(chunk.size() - pattern.size(), clamped_start - chunk_start);
        for (qint64 index = max_index; index >= 0; --index) {
            bool matched = true;
            for (int pattern_index = 0; pattern_index < pattern.size(); ++pattern_index) {
                if (chunk.at(index + pattern_index) != pattern.at(pattern_index)) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                return chunk_start + index;
            }
        }

        if (progress_callback && !progress_callback(qMin(total, clamped_start - chunk_start + 1), total)) {
            return -1;
        }

        if (chunk_start == bounded_start) {
            break;
        }
        chunk_end = chunk_start + overlap;
    }

    return -1;
}
