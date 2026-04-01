#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

class QDockWidget;
class QLabel;
class QAction;
class QLineEdit;
class QTextEdit;
class QTreeWidget;
class QCloseEvent;
class HexView;
class QActionGroup;
class QToolBar;
class QMenu;

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
    void update_status(qulonglong caret_offset, qulonglong selection_size, qulonglong document_size);
    void update_window_title(const QString& title, qulonglong document_size);
    void run_search(bool forward, bool from_caret, bool selection_only = false);
    void run_find_all(bool selection_only);
    void update_inspector_view(const QString& text);
    void show_search_summary(const QString& summary);
    void show_search_matches(const QString& summary, const QVector<qint64>& matches);
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
    QTreeWidget* inspector_tree_ = nullptr;
    QTreeWidget* search_results_tree_ = nullptr;
    QTextEdit* bookmarks_text_ = nullptr;
    QTextEdit* analysis_text_ = nullptr;
    QLabel* status_label_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    QLineEdit* goto_offset_edit_ = nullptr;
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
    bool last_dirty_state_ = false;
    QByteArray last_search_pattern_;
    bool last_search_hex_mode_ = false;
    SearchInputMode last_search_input_mode_ = SearchInputMode::Text;
    SearchTextEncoding last_search_text_encoding_ = SearchTextEncoding::Utf8;
    NumericSearchType last_search_numeric_type_ = NumericSearchType::Unsigned32;
    SearchByteOrder last_search_numeric_byte_order_ = SearchByteOrder::Little;
    QString last_search_display_text_;
    QStringList recent_files_;
};
