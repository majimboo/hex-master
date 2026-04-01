#pragma once

#include <QAbstractScrollArea>
#include <QSet>
#include <QVector>

#include "file_byte_source.hpp"

class HexView final : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit HexView(QWidget* parent = nullptr);

    bool open_file(const QString& path);
    bool has_document() const;
    QString document_title() const;
    QString document_path() const;
    qint64 document_size() const;
    qint64 bytes_per_row() const;
    void set_bytes_per_row(qint64 bytes_per_row);
    void go_to_offset(qint64 offset);
    bool is_dirty() const;
    bool save();
    bool save_as(const QString& path);
    bool undo();
    bool redo();
    void toggle_bookmark_at_caret();
    void next_bookmark();
    void previous_bookmark();
    bool find_pattern(const QByteArray& pattern, bool forward, bool from_caret, qint64* found_offset = nullptr);
    QString format_search_result(qint64 found_offset, const QByteArray& pattern, bool hex_mode) const;
    bool replace_range(qint64 offset, const QByteArray& before, const QByteArray& after);

signals:
    void status_changed(qulonglong caret_offset, qulonglong selection_size, qulonglong document_size);
    void document_loaded(const QString& title, qulonglong document_size);
    void bookmarks_changed(const QString& text);
    void inspector_changed(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
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

    void refresh_metrics();
    void refresh_scrollbar();
    void invalidate_row_cache();
    void ensure_row_cache(qint64 first_row, qint64 row_count);
    void emit_status();
    void emit_bookmarks();
    void emit_inspector();
    void move_caret(qint64 offset, bool extend_selection);
    void ensure_caret_visible();
    void scroll_to_row(qint64 row);
    qint64 visible_row_count() const;
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
    QRect header_cell_rect(qint64 column) const;
    QRect hex_cell_rect(int row_index, qint64 column) const;
    QRect ascii_cell_rect(int row_index, qint64 column) const;
    int hex_value_for_key(int key) const;
    bool apply_hex_input(int nibble_value);
    qint64 offset_at(const QPoint& point) const;
    QString hex_byte(quint8 value) const;
    QChar printable_char(quint8 value) const;
    bool has_bookmark(qint64 offset) const;
    QString format_bookmarks_text() const;
    QString format_inspector_text() const;
    qint64 search_from(const QByteArray& pattern, qint64 start_offset, bool forward) const;
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
    QSet<qint64> bookmarks_;
};
