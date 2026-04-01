#pragma once

#include <QAbstractScrollArea>
#include <QColor>
#include <QMap>
#include <QVector>

#include "file_byte_source.hpp"

class HexView final : public QAbstractScrollArea {
    Q_OBJECT

public:
    enum class EditMode {
        Insert,
        Overwrite,
    };

    enum class ActivePane {
        Hex,
        Text,
    };

    enum class InspectorEndian {
        Little,
        Big,
    };

    struct InspectorRow {
        QString section;
        QString field;
        QString value;
    };

    struct AnalysisRow {
        QString section;
        QString field;
        QString value;
    };

    struct BookmarkRow {
        qint64 offset = 0;
        qint64 row = 0;
        QColor color;
        QString label;
    };

    explicit HexView(QWidget* parent = nullptr);

    bool open_file(const QString& path);
    bool has_document() const;
    QString document_title() const;
    QString document_path() const;
    qint64 document_size() const;
    qint64 bytes_per_row() const;
    void set_bytes_per_row(qint64 bytes_per_row);
    void go_to_offset(qint64 offset);
    void select_range(qint64 offset, qint64 length);
    EditMode edit_mode() const;
    void set_edit_mode(EditMode mode);
    void toggle_edit_mode();
    ActivePane active_pane() const;
    bool bookmark_gutter_visible() const;
    void set_bookmark_gutter_visible(bool visible);
    bool row_numbers_visible() const;
    void set_row_numbers_visible(bool visible);
    bool offsets_visible() const;
    void set_offsets_visible(bool visible);
    int row_number_column_width() const;
    void set_row_number_column_width(int width);
    int offset_column_width() const;
    void set_offset_column_width(int width);
    void reset_view_layout();
    bool is_read_only() const;
    bool is_dirty() const;
    bool save();
    bool save_as(const QString& path);
    bool undo();
    bool redo();
    bool has_selection() const;
    QByteArray selected_bytes() const;
    QByteArray read_bytes(qint64 offset, qint64 length) const;
    QString selected_hex_text() const;
    QString selected_text_text() const;
    bool insert_at_caret(const QByteArray& bytes);
    bool delete_selection();
    bool delete_at_caret();
    bool backspace_at_caret();
    bool overwrite_at_caret(const QByteArray& bytes);
    bool overwrite_selection(const QByteArray& bytes);
    bool fill_selection(quint8 value);
    void toggle_bookmark_at_caret();
    void next_bookmark();
    void previous_bookmark();
    QVector<BookmarkRow> bookmark_rows() const;
    bool set_bookmark_label(qint64 offset, const QString& label);
    bool set_bookmark_color(qint64 offset, const QColor& color);
    bool remove_bookmark(qint64 offset);
    bool find_pattern(const QByteArray& pattern, bool forward, bool from_caret, qint64* found_offset = nullptr);
    bool find_pattern_in_selection(const QByteArray& pattern, bool forward, bool from_caret, qint64* found_offset = nullptr);
    QVector<qint64> find_all_patterns(const QByteArray& pattern, bool selection_only = false) const;
    QString format_search_result(qint64 found_offset, const QByteArray& pattern, bool hex_mode) const;
    QString build_hash_report(bool selection_only) const;
    QVector<AnalysisRow> analysis_rows(bool selection_only) const;
    bool replace_range(qint64 offset, const QByteArray& before, const QByteArray& after);
    qint64 replace_all(const QByteArray& before, const QByteArray& after, bool selection_only);
    InspectorEndian inspector_endian() const;
    void set_inspector_endian(InspectorEndian endian);
    QVector<InspectorRow> inspector_rows() const;
    bool apply_inspector_edit(const QString& section, const QString& field, const QString& value, QString* error_message = nullptr);

signals:
    void status_changed(qulonglong caret_offset, qulonglong selection_size, qulonglong document_size);
    void document_loaded(const QString& title, qulonglong document_size);
    void bookmarks_changed();
    void inspector_changed(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Metrics {
        int line_height = 1;
        int char_width = 1;
        int hex_cell_width = 1;
        int ascii_cell_width = 1;
        int marker_width = 14;
        int row_number_width = 48;
        int address_width = 120;
        int marker_start = 0;
        int row_number_start = 0;
        int address_start = 0;
        int hex_start = 0;
        int ascii_start = 0;
        int row_header = 0;
    };

    struct CachedRow {
        qint64 row_number = 0;
        qint64 row_offset = 0;
        QByteArray bytes;
        QString row_number_text;
        QString offset_text;
        QString ascii_text;
    };

    struct BookmarkEntry {
        QColor color;
        QString label;
    };

    enum class HeaderDivider {
        None,
        RowOffset,
        OffsetHex,
        HexText,
    };

    void refresh_metrics();
    void refresh_scrollbar();
    void invalidate_row_cache();
    void ensure_row_cache(qint64 first_row, qint64 row_count);
    qint64 max_first_visible_row() const;
    void emit_status();
    void emit_bookmarks();
    void emit_inspector();
    void refresh_after_edit(bool ensure_visible = true);
    void move_caret(qint64 offset, bool extend_selection);
    bool apply_text_input(const QString& text);
    void ensure_caret_visible();
    void scroll_to_row(qint64 row);
    qint64 visible_row_count() const;
    qint64 fully_visible_row_count() const;
    qint64 total_rows() const;
    qint64 first_visible_row() const;
    qint64 last_visible_row() const;
    qint64 row_to_offset(qint64 row) const;
    qint64 clamp_offset(qint64 offset) const;
    qint64 slider_to_row(int slider_value) const;
    int row_to_slider(qint64 row) const;
    qint64 selection_start() const;
    qint64 selection_end() const;
    qint64 selection_size() const;
    void clear_selection();
    HeaderDivider divider_at(const QPoint& point) const;
    void update_resize_cursor(const QPoint& point);
    QRect header_cell_rect(qint64 column) const;
    QRect hex_cell_rect(int row_index, qint64 column) const;
    QRect ascii_cell_rect(int row_index, qint64 column) const;
    int hex_value_for_key(int key) const;
    bool apply_hex_input(int nibble_value);
    qint64 offset_at(const QPoint& point) const;
    ActivePane pane_at(const QPoint& point) const;
    QString hex_byte(quint8 value) const;
    QString formatted_offset(qint64 offset) const;
    QChar printable_char(quint8 value) const;
    bool has_bookmark(qint64 offset) const;
    const BookmarkEntry* bookmark_entry_for_row(qint64 row_start) const;
    QVector<InspectorRow> build_inspector_rows() const;
    QString format_inspector_text() const;
    QVector<AnalysisRow> build_analysis_rows(bool selection_only) const;
    static bool try_parse_hex_string(const QString& text, QByteArray& bytes);
    qint64 search_from(const QByteArray& pattern, qint64 start_offset, bool forward, qint64 range_start = -1, qint64 range_end = -1) const;
    void leaveEvent(QEvent* event) override;

    FileByteSource source_;
    Metrics metrics_;
    qint64 bytes_per_row_ = 16;
    qint64 first_visible_row_ = 0;
    qint64 caret_offset_ = 0;
    qint64 selection_anchor_ = -1;
    bool selection_active_ = false;
    bool high_nibble_pending_ = true;
    qint64 cached_first_row_ = -1;
    qint64 cached_row_count_ = -1;
    qint64 cached_generation_ = -1;
    qint64 render_generation_ = 0;
    QVector<CachedRow> cached_rows_;
    qint64 hovered_offset_ = -1;
    QMap<qint64, BookmarkEntry> bookmarks_;
    EditMode edit_mode_ = EditMode::Overwrite;
    ActivePane active_pane_ = ActivePane::Hex;
    qint8 pending_insert_high_nibble_ = -1;
    InspectorEndian inspector_endian_ = InspectorEndian::Little;
    bool show_bookmark_gutter_ = true;
    bool show_row_numbers_ = false;
    bool show_offsets_ = true;
    int custom_row_number_width_ = -1;
    int custom_address_width_ = -1;
    HeaderDivider active_divider_ = HeaderDivider::None;
};
