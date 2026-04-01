#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <functional>

class QDockWidget;
class QLabel;
class QAction;
class QComboBox;
class QTreeWidget;
class QTreeWidgetItem;
class QCloseEvent;
class HexView;
class QActionGroup;
class QToolBar;
class QMenu;
class QWidget;
class QTabWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    enum class SearchInputMode {
        Text,
        Hex,
        Value,
    };

    enum class SearchTextEncoding {
        Ascii,
        Utf8,
        Utf16Le,
        Utf16Be,
    };

    enum class NumericSearchType {
        Unsigned8,
        Signed8,
        Unsigned16,
        Signed16,
        Unsigned32,
        Signed32,
        Unsigned64,
        Signed64,
        Float32,
        Float64,
    };

    enum class SearchByteOrder {
        Little,
        Big,
    };

    enum class SearchExecution {
        FindNext,
        FindAll,
    };

    enum class SaveBackupPolicy {
        Ask,
        Always,
        Never,
    };

    void setup_menu();
    void setup_central_widget();
    void setup_docks();
    void setup_status_bar();
    void setup_toolbar();
    void setup_view_menu(QMenu* view_menu);
    void setup_recent_files_menu(QMenu* file_menu);
    void update_recent_files_menu();
    void restore_session();
    void save_session() const;
    void add_recent_file(const QString& path);
    QString preferred_dialog_directory(const QString& fallback_file_name = QString()) const;
    void remember_dialog_path(const QString& path);
    void add_goto_offset_history(const QString& text);
    void add_search_history(const QString& text);
    void add_replace_history(const QString& text);
    void refresh_goto_offset_widgets();
    SaveBackupPolicy save_backup_policy() const;
    bool confirm_explicit_save(const QString& title) const;
    bool prepare_backup_for_save(const QString& path, const std::function<bool(qint64, qint64, const QString&)>& progress_callback);
    static bool copy_file_with_progress(const QString& source_path, const QString& target_path, const std::function<bool(qint64, qint64, const QString&)>& progress_callback);
    bool save_current_document(bool confirm_save, const QString* save_as_path = nullptr);
    static QString backup_path_for(const QString& path);
    bool confirm_discard_changes();
    bool open_file_path(const QString& path);
    void closeEvent(QCloseEvent* event) override;
    void new_file();
    void open_file();
    void open_recent_file();
    void save_file();
    void save_file_as();
    void undo();
    void redo();
    void cut();
    void copy();
    void copy_as_hex();
    void paste();
    void paste_hex();
    void fill_selection();
    void compute_hashes();
    void open_settings();
    void show_about();
    void set_insert_mode();
    void set_overwrite_mode();
    void go_to_offset();
    void set_bytes_per_row(int bytes_per_row);
    void toggle_bookmark();
    void next_bookmark();
    void previous_bookmark();
    void find();
    void find_in_selection();
    void find_next();
    void find_previous();
    void replace();
    void replace_all();
    void set_inspector_little_endian();
    void set_inspector_big_endian();
    void set_bookmark_gutter_visible(bool visible);
    void set_row_numbers_visible(bool visible);
    void set_offsets_visible(bool visible);
    void increase_bytes_per_row();
    void decrease_bytes_per_row();
    void reset_view();
    void update_status(qulonglong caret_offset, qulonglong selection_size, qulonglong document_size);
    void update_window_title(const QString& title, qulonglong document_size);
    void run_search(bool forward, bool from_caret, bool selection_only = false);
    void run_find_all(bool selection_only);
    void update_inspector_view(const QString& text);
    void update_bookmarks_view();
    void update_analysis_view(bool selection_only);
    void handle_inspector_item_changed(QTreeWidgetItem* item, int column);
    void activate_bookmark_item();
    void rename_current_bookmark();
    void recolor_current_bookmark();
    void remove_current_bookmark();
    void copy_current_tree_value(QTreeWidget* tree);
    void close_search_results_tab(int index);
    void show_search_summary(const QString& summary, const QString& tab_title = QString());
    void show_search_matches(const QString& summary, const QVector<qint64>& matches, const QString& tab_title = QString());
    void activate_search_result_item();
    bool prompt_for_replace_operation(
        QByteArray& before,
        QByteArray& after,
        bool& find_hex_mode,
        SearchExecution& execution,
        bool& selection_only);
    bool prompt_for_search_pattern(
        const QString& title,
        const QString& label,
        QByteArray& pattern,
        bool& hex_mode,
        SearchExecution* execution = nullptr,
        bool* selection_only = nullptr,
        bool default_selection_only = false,
        QString* display_text = nullptr);
    static QByteArray encode_search_text(const QString& text, SearchTextEncoding encoding);
    static QString encoding_label(SearchTextEncoding encoding);
    static QString numeric_type_label(NumericSearchType type);
    static bool encode_numeric_search_value(const QString& text, NumericSearchType type, SearchByteOrder byte_order, QByteArray& bytes);
    static bool try_parse_hex_bytes(const QString& text, QByteArray& bytes);
    static bool try_parse_offset(const QString& text, quint64& value);

    HexView* hex_view_ = nullptr;
    QDockWidget* inspector_dock_ = nullptr;
    QDockWidget* bookmarks_dock_ = nullptr;
    QDockWidget* search_results_dock_ = nullptr;
    QDockWidget* analysis_dock_ = nullptr;
    QTreeWidget* bookmarks_tree_ = nullptr;
    QTreeWidget* inspector_tree_ = nullptr;
    QTabWidget* search_results_tabs_ = nullptr;
    QTreeWidget* analysis_tree_ = nullptr;
    QLabel* status_label_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    QWidget* edit_mode_toggle_ = nullptr;
    QComboBox* goto_offset_edit_ = nullptr;
    QAction* open_action_ = nullptr;
    QAction* new_action_ = nullptr;
    QAction* save_action_ = nullptr;
    QAction* save_as_action_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
    QAction* cut_action_ = nullptr;
    QAction* copy_action_ = nullptr;
    QAction* copy_as_hex_action_ = nullptr;
    QAction* paste_action_ = nullptr;
    QAction* paste_hex_action_ = nullptr;
    QAction* fill_selection_action_ = nullptr;
    QAction* compute_hashes_action_ = nullptr;
    QAction* settings_action_ = nullptr;
    QAction* insert_mode_action_ = nullptr;
    QAction* overwrite_mode_action_ = nullptr;
    QAction* goto_action_ = nullptr;
    QAction* toggle_bookmark_action_ = nullptr;
    QAction* next_bookmark_action_ = nullptr;
    QAction* previous_bookmark_action_ = nullptr;
    QAction* find_action_ = nullptr;
    QAction* find_next_action_ = nullptr;
    QAction* find_previous_action_ = nullptr;
    QAction* replace_action_ = nullptr;
    QAction* recent_files_separator_ = nullptr;
    QActionGroup* row_width_group_ = nullptr;
    QActionGroup* inspector_endian_group_ = nullptr;
    QActionGroup* edit_mode_group_ = nullptr;
    QMenu* recent_files_menu_ = nullptr;
    QAction* show_bookmark_gutter_action_ = nullptr;
    QAction* show_row_numbers_action_ = nullptr;
    QAction* show_offsets_action_ = nullptr;
    QAction* increase_width_action_ = nullptr;
    QAction* decrease_width_action_ = nullptr;
    QAction* reset_view_action_ = nullptr;
    bool updating_inspector_tree_ = false;
    bool last_dirty_state_ = false;
    QByteArray last_search_pattern_;
    bool last_search_hex_mode_ = false;
    SearchInputMode last_search_input_mode_ = SearchInputMode::Text;
    SearchTextEncoding last_search_text_encoding_ = SearchTextEncoding::Utf8;
    NumericSearchType last_search_numeric_type_ = NumericSearchType::Unsigned32;
    SearchByteOrder last_search_numeric_byte_order_ = SearchByteOrder::Little;
    QString last_search_display_text_;
    SearchExecution last_search_execution_ = SearchExecution::FindNext;
    bool last_search_selection_only_ = false;
    QString last_replace_display_text_;
    SearchInputMode last_replace_input_mode_ = SearchInputMode::Text;
    SearchTextEncoding last_replace_text_encoding_ = SearchTextEncoding::Utf8;
    NumericSearchType last_replace_numeric_type_ = NumericSearchType::Unsigned32;
    SearchByteOrder last_replace_numeric_byte_order_ = SearchByteOrder::Little;
    SearchExecution last_replace_execution_ = SearchExecution::FindNext;
    bool last_replace_selection_only_ = false;
    QStringList recent_files_;
    QStringList goto_offset_history_;
    QStringList search_history_;
    QStringList replace_history_;
    int search_results_counter_ = 0;
};
