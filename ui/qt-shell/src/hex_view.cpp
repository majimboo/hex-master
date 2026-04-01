#include "hex_view.hpp"

#include <QFontDatabase>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>

namespace {
constexpr int kScrollbarResolution = 1000000;
}

HexView::HexView(QWidget* parent) : QAbstractScrollArea(parent) {
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setFamily(QStringLiteral("Consolas"));
    mono.setPointSize(10);
    setFont(mono);
    viewport()->setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        first_visible_row_ = slider_to_row(value);
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
    bookmarks_.clear();
    ++render_generation_;
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

bool HexView::find_pattern(const QByteArray& pattern, bool forward, bool from_caret, qint64* found_offset) {
    if (!has_document() || pattern.isEmpty()) {
        return false;
    }

    qint64 start_offset = 0;
    if (from_caret) {
        start_offset = forward ? qMin(document_size(), caret_offset_ + 1) : qMax<qint64>(0, caret_offset_ - 1);
    } else {
        start_offset = forward ? 0 : document_size() - 1;
    }

    qint64 match = search_from(pattern, start_offset, forward);
    if (match < 0 && from_caret) {
        match = search_from(pattern, forward ? 0 : document_size() - 1, forward);
    }
    if (match < 0) {
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

bool HexView::replace_range(qint64 offset, const QByteArray& before, const QByteArray& after) {
    if (!has_document() || before.isEmpty() || before.size() != after.size()) {
        return false;
    }

    const QByteArray current = source_.read_range(offset, before.size());
    if (current != before) {
        return false;
    }
    if (!source_.overwrite_range(offset, after)) {
        return false;
    }

    selection_anchor_ = offset;
    selection_active_ = after.size() > 1;
    caret_offset_ = offset + qMax(0, after.size() - 1);
    high_nibble_pending_ = true;
    ++render_generation_;
    invalidate_row_cache();
    ensure_caret_visible();
    emit_status();
    viewport()->update();
    return true;
}

void HexView::toggle_bookmark_at_caret() {
    if (!has_document()) {
        return;
    }

    if (bookmarks_.contains(caret_offset_)) {
        bookmarks_.remove(caret_offset_);
    } else {
        bookmarks_.insert(caret_offset_);
    }

    emit_bookmarks();
    viewport()->update();
}

void HexView::next_bookmark() {
    if (!has_document() || bookmarks_.isEmpty()) {
        return;
    }

    QList<qint64> sorted = bookmarks_.values();
    std::sort(sorted.begin(), sorted.end());
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

    QList<qint64> sorted = bookmarks_.values();
    std::sort(sorted.begin(), sorted.end());
    for (auto it = sorted.crbegin(); it != sorted.crend(); ++it) {
        if (*it < caret_offset_) {
            move_caret(*it, false);
            return;
        }
    }

    move_caret(sorted.last(), false);
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

    const qint64 first_row = first_visible_row();
    const qint64 rows = visible_row_count();
    ensure_row_cache(first_row, rows);

    const qint64 selected_start = selection_start();
    const qint64 selected_end = selection_end();

    painter.fillRect(QRect(0, 0, viewport()->width(), metrics_.row_header), header_background);
    painter.fillRect(QRect(0, metrics_.row_header, metrics_.hex_start - 12, viewport()->height() - metrics_.row_header), gutter_background);
    painter.fillRect(QRect(metrics_.marker_start, 0, metrics_.marker_width, viewport()->height()), QColor(224, 230, 239));
    painter.fillRect(QRect(metrics_.hex_start - 6, 0, 1, viewport()->height()), grid_line);
    painter.fillRect(QRect(metrics_.ascii_start - 10, 0, 1, viewport()->height()), grid_line);
    painter.setPen(secondary_text);
    painter.drawLine(0, metrics_.row_header, viewport()->width(), metrics_.row_header);
    painter.drawText(QRect(metrics_.marker_start, 0, metrics_.marker_width, metrics_.row_header - 2),
        Qt::AlignCenter, QStringLiteral("Bk"));
    painter.drawText(QRect(metrics_.row_number_start, 0, metrics_.row_number_width, metrics_.row_header - 2),
        Qt::AlignCenter, QStringLiteral("Row"));
    painter.drawText(QRect(metrics_.address_start, 0, metrics_.address_width, metrics_.row_header - 2),
        Qt::AlignCenter, QStringLiteral("Offset"));
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
        painter.drawText(QRect(metrics_.marker_start, y, metrics_.marker_width, metrics_.line_height),
            Qt::AlignCenter, row_bookmarked ? QStringLiteral("*") : (current_row ? QStringLiteral(">") : QString()));
        painter.drawText(QRect(metrics_.row_number_start, y, metrics_.row_number_width, metrics_.line_height),
            Qt::AlignCenter, cached_row.row_number_text);
        painter.drawText(QRect(metrics_.address_start, y, metrics_.address_width, metrics_.line_height),
            Qt::AlignCenter, cached_row.offset_text);

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

    if (!event->modifiers().testFlag(Qt::ControlModifier) &&
        !event->modifiers().testFlag(Qt::AltModifier) &&
        !event->modifiers().testFlag(Qt::MetaModifier)) {
        const int nibble_value = hex_value_for_key(event->key());
        if (nibble_value >= 0 && apply_hex_input(nibble_value)) {
            return;
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
    default:
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
}

void HexView::mousePressEvent(QMouseEvent* event) {
    if (!has_document() || event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    const qint64 clicked = offset_at(event->pos());
    selection_anchor_ = clicked;
    selection_active_ = false;
    high_nibble_pending_ = true;
    move_caret(clicked, false);
}

void HexView::mouseMoveEvent(QMouseEvent* event) {
    const qint64 hovered = has_document() ? offset_at(event->pos()) : -1;
    if (hovered != hovered_offset_) {
        hovered_offset_ = hovered;
        viewport()->update();
    }

    if (!has_document() || !(event->buttons() & Qt::LeftButton) || selection_anchor_ < 0) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }

    selection_active_ = true;
    move_caret(offset_at(event->pos()), true);
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
    viewport()->update();
    QAbstractScrollArea::leaveEvent(event);
}

void HexView::refresh_metrics() {
    const QFontMetrics metrics(font());
    metrics_.line_height = metrics.height() + 8;
    metrics_.char_width = qMax(1, metrics.horizontalAdvance(QLatin1Char('9')));
    metrics_.hex_cell_width = metrics.horizontalAdvance(QStringLiteral("FF")) + 16;
    metrics_.ascii_cell_width = metrics.horizontalAdvance(QLatin1Char('.')) + 10;
    metrics_.marker_width = 18;
    metrics_.row_number_width = metrics.horizontalAdvance(QStringLiteral("999999")) + 24;
    metrics_.address_width = metrics.horizontalAdvance(QStringLiteral("0000000000000000")) + 30;
    metrics_.marker_start = 0;
    metrics_.row_number_start = metrics_.marker_start + metrics_.marker_width;
    metrics_.address_start = metrics_.row_number_start + metrics_.row_number_width;
    metrics_.hex_start = metrics_.address_start + metrics_.address_width + 12;
    metrics_.ascii_start = metrics_.hex_start + static_cast<int>(bytes_per_row_) * metrics_.hex_cell_width + 24;
    metrics_.row_header = metrics_.line_height + 18;
}

void HexView::refresh_scrollbar() {
    const qint64 rows = total_rows();
    const qint64 visible = visible_row_count();

    verticalScrollBar()->setPageStep(static_cast<int>(qMin<qint64>(visible, kScrollbarResolution)));
    verticalScrollBar()->setRange(0, rows > visible ? kScrollbarResolution : 0);
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
        cached_row.offset_text = QStringLiteral("%1").arg(cached_row.row_offset, 8, 16, QChar(u'0')).toUpper();
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
    emit bookmarks_changed(format_bookmarks_text());
}

void HexView::emit_inspector() {
    emit inspector_changed(format_inspector_text());
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
    const qint64 max_row = qMax<qint64>(0, total_rows() - visible_row_count());
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
    return qMax<qint64>(1, height / metrics_.line_height + 1);
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
    const qint64 max_row = qMax<qint64>(0, total_rows() - visible_row_count());
    if (max_row == 0) {
        return 0;
    }

    const double ratio = static_cast<double>(slider_value) / static_cast<double>(kScrollbarResolution);
    return static_cast<qint64>(qRound64(ratio * static_cast<double>(max_row)));
}

int HexView::row_to_slider(qint64 row) const {
    const qint64 max_row = qMax<qint64>(0, total_rows() - visible_row_count());
    if (max_row == 0) {
        return 0;
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

QString HexView::hex_byte(quint8 value) const {
    return QStringLiteral("%1").arg(value, 2, 16, QChar(u'0')).toUpper();
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

QString HexView::format_bookmarks_text() const {
    if (!has_document()) {
        return QStringLiteral("Bookmarks\n\nOpen a file to add bookmarks.");
    }

    if (bookmarks_.isEmpty()) {
        return QStringLiteral("Bookmarks\n\nNo bookmarks yet.\nUse Ctrl+B to toggle a bookmark at the caret.");
    }

    QList<qint64> sorted = bookmarks_.values();
    std::sort(sorted.begin(), sorted.end());

    QString text = QStringLiteral("Bookmarks\n\n");
    for (const qint64 bookmark : sorted) {
        const qint64 row = bookmark / bytes_per_row_;
        text += QStringLiteral("0x%1   Row %2\n")
                    .arg(bookmark, 8, 16, QChar(u'0'))
                    .arg(row);
    }
    return text.toUpper();
}

QString HexView::format_inspector_text() const {
    if (!has_document()) {
        return QStringLiteral("Inspector\n\nOpen a file to inspect values.");
    }

    const QByteArray bytes = source_.read_range(caret_offset_, 8);
    if (bytes.isEmpty()) {
        return QStringLiteral("Inspector\n\nNo bytes available at the current offset.");
    }

    auto le_value = [&bytes](int count) -> quint64 {
        quint64 value = 0;
        for (int i = 0; i < count && i < bytes.size(); ++i) {
            value |= static_cast<quint64>(static_cast<quint8>(bytes.at(i))) << (i * 8);
        }
        return value;
    };
    auto be_value = [&bytes](int count) -> quint64 {
        quint64 value = 0;
        for (int i = 0; i < count && i < bytes.size(); ++i) {
            value = (value << 8) | static_cast<quint64>(static_cast<quint8>(bytes.at(i)));
        }
        return value;
    };

    QString ascii;
    ascii.reserve(bytes.size());
    for (const char byte : bytes) {
        ascii.append(printable_char(static_cast<quint8>(byte)));
    }

    QString text = QStringLiteral("Inspector\n\n");
    text += QStringLiteral("Offset: 0x%1\n").arg(caret_offset_, 8, 16, QChar(u'0')).toUpper();
    text += QStringLiteral("Selection: %1 bytes\n").arg(selection_size());
    text += QStringLiteral("Bytes: ");
    for (int i = 0; i < bytes.size(); ++i) {
        if (i > 0) {
            text += QLatin1Char(' ');
        }
        text += hex_byte(static_cast<quint8>(bytes.at(i)));
    }
    text += QStringLiteral("\nASCII: %1\n\n").arg(ascii);

    text += QStringLiteral("u8:  %1\n").arg(static_cast<quint8>(bytes.at(0)));
    if (bytes.size() >= 2) {
        text += QStringLiteral("u16 LE: %1\n").arg(le_value(2));
        text += QStringLiteral("u16 BE: %1\n").arg(be_value(2));
    }
    if (bytes.size() >= 4) {
        text += QStringLiteral("u32 LE: %1\n").arg(le_value(4));
        text += QStringLiteral("u32 BE: %1\n").arg(be_value(4));
    }
    if (bytes.size() >= 8) {
        text += QStringLiteral("u64 LE: %1\n").arg(le_value(8));
        text += QStringLiteral("u64 BE: %1\n").arg(be_value(8));
    }

    return text;
}

qint64 HexView::search_from(const QByteArray& pattern, qint64 start_offset, bool forward) const {
    if (!has_document() || pattern.isEmpty()) {
        return -1;
    }

    constexpr qint64 kChunkSize = 256 * 1024;
    const qint64 overlap = qMax<qint64>(0, pattern.size() - 1);

    if (forward) {
        for (qint64 offset = qMax<qint64>(0, start_offset); offset < document_size();) {
            const qint64 to_read = qMin(document_size() - offset, kChunkSize + overlap);
            const QByteArray chunk = source_.read_range(offset, to_read);
            if (chunk.isEmpty()) {
                break;
            }

            const qint64 index = chunk.indexOf(pattern);
            if (index >= 0) {
                return offset + index;
            }

            if (chunk.size() <= overlap) {
                break;
            }

            offset += chunk.size() - overlap;
        }
        return -1;
    }

    for (qint64 chunk_end = qMin(document_size(), start_offset + 1); chunk_end > 0;) {
        const qint64 chunk_start = qMax<qint64>(0, chunk_end - (kChunkSize + overlap));
        const QByteArray chunk = source_.read_range(chunk_start, chunk_end - chunk_start);
        if (chunk.isEmpty()) {
            break;
        }

        const qint64 max_index = qMin<qint64>(chunk.size() - pattern.size(), start_offset - chunk_start);
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

        if (chunk_start == 0) {
            break;
        }
        chunk_end = chunk_start + overlap;
        start_offset = qMin(start_offset, chunk_end - 1);
    }

    return -1;
}
