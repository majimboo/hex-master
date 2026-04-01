#pragma once

#include <QMainWindow>
#include <QString>

class QDockWidget;
class QLabel;
class QAction;
class QLineEdit;
class QTextEdit;
class HexView;
class QActionGroup;
class QToolBar;
class QMenu;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void setup_menu();
    void setup_central_widget();
    void setup_docks();
    void setup_status_bar();
    void setup_toolbar();
    void setup_view_menu(QMenu* view_menu);
    void open_file();
    void save_file();
    void save_file_as();
    void undo();
    void redo();
    void go_to_offset();
    void set_bytes_per_row(int bytes_per_row);
    void toggle_bookmark();
    void next_bookmark();
    void previous_bookmark();
    void find();
    void find_next();
    void find_previous();
    void replace();
    void update_status(qulonglong caret_offset, qulonglong selection_size, qulonglong document_size);
    void update_window_title(const QString& title, qulonglong document_size);
    void run_search(bool forward, bool from_caret);
    static bool try_parse_hex_bytes(const QString& text, QByteArray& bytes);
    static bool try_parse_offset(const QString& text, quint64& value);

    HexView* hex_view_ = nullptr;
    QDockWidget* inspector_dock_ = nullptr;
    QDockWidget* bookmarks_dock_ = nullptr;
    QDockWidget* search_results_dock_ = nullptr;
    QTextEdit* inspector_text_ = nullptr;
    QTextEdit* bookmarks_text_ = nullptr;
    QTextEdit* search_results_text_ = nullptr;
    QLabel* status_label_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    QLineEdit* goto_offset_edit_ = nullptr;
    QAction* open_action_ = nullptr;
    QAction* save_action_ = nullptr;
    QAction* save_as_action_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
    QAction* goto_action_ = nullptr;
    QAction* toggle_bookmark_action_ = nullptr;
    QAction* next_bookmark_action_ = nullptr;
    QAction* previous_bookmark_action_ = nullptr;
    QAction* find_action_ = nullptr;
    QAction* find_next_action_ = nullptr;
    QAction* find_previous_action_ = nullptr;
    QAction* replace_action_ = nullptr;
    QActionGroup* row_width_group_ = nullptr;
    bool last_dirty_state_ = false;
    QByteArray last_search_pattern_;
    bool last_search_hex_mode_ = false;
};
