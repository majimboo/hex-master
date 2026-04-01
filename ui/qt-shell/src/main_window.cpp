#include "main_window.hpp"

#include "hex_view.hpp"

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QComboBox>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QStatusBar>
#include <QTextEdit>
#include <QTreeWidget>
#include <QHeaderView>
#include <QToolBar>

#include <limits>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Hex Master");
    setup_central_widget();
    setup_docks();
    setup_menu();
    setup_toolbar();
    setup_status_bar();
    restore_session();
}

void MainWindow::setup_menu() {
    auto* fileMenu = menuBar()->addMenu("&File");
    new_action_ = fileMenu->addAction("&New");
    new_action_->setShortcut(QKeySequence::New);
    connect(new_action_, &QAction::triggered, this, &MainWindow::new_file);
    open_action_ = fileMenu->addAction("&Open...");
    open_action_->setShortcut(QKeySequence::Open);
    connect(open_action_, &QAction::triggered, this, &MainWindow::open_file);
    setup_recent_files_menu(fileMenu);
    fileMenu->addSeparator();
    save_action_ = fileMenu->addAction("&Save");
    save_action_->setShortcut(QKeySequence::Save);
    connect(save_action_, &QAction::triggered, this, &MainWindow::save_file);
    save_as_action_ = fileMenu->addAction("Save &As...");
    save_as_action_->setShortcut(QKeySequence::SaveAs);
    connect(save_as_action_, &QAction::triggered, this, &MainWindow::save_file_as);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu("&Edit");
    undo_action_ = editMenu->addAction("&Undo");
    undo_action_->setShortcut(QKeySequence::Undo);
    connect(undo_action_, &QAction::triggered, this, &MainWindow::undo);
    redo_action_ = editMenu->addAction("&Redo");
    redo_action_->setShortcut(QKeySequence::Redo);
    connect(redo_action_, &QAction::triggered, this, &MainWindow::redo);
    editMenu->addSeparator();
    cut_action_ = editMenu->addAction("Cu&t");
    cut_action_->setShortcut(QKeySequence::Cut);
    connect(cut_action_, &QAction::triggered, this, &MainWindow::cut);
    copy_action_ = editMenu->addAction("&Copy");
    copy_action_->setShortcut(QKeySequence::Copy);
    connect(copy_action_, &QAction::triggered, this, &MainWindow::copy);
    copy_as_hex_action_ = editMenu->addAction("Copy as &Hex");
    copy_as_hex_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    connect(copy_as_hex_action_, &QAction::triggered, this, &MainWindow::copy_as_hex);
    paste_action_ = editMenu->addAction("&Paste");
    paste_action_->setShortcut(QKeySequence::Paste);
    connect(paste_action_, &QAction::triggered, this, &MainWindow::paste);
    paste_hex_action_ = editMenu->addAction("Paste H&ex");
    paste_hex_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    connect(paste_hex_action_, &QAction::triggered, this, &MainWindow::paste_hex);
    fill_selection_action_ = editMenu->addAction("&Fill Selection...");
    connect(fill_selection_action_, &QAction::triggered, this, &MainWindow::fill_selection);
    editMenu->addSeparator();
    edit_mode_group_ = new QActionGroup(this);
    edit_mode_group_->setExclusive(true);
    insert_mode_action_ = editMenu->addAction("&Insert Mode");
    insert_mode_action_->setCheckable(true);
    insert_mode_action_->setShortcut(QKeySequence(Qt::Key_Insert));
    overwrite_mode_action_ = editMenu->addAction("&Overwrite Mode");
    overwrite_mode_action_->setCheckable(true);
    overwrite_mode_action_->setChecked(true);
    edit_mode_group_->addAction(insert_mode_action_);
    edit_mode_group_->addAction(overwrite_mode_action_);
    connect(insert_mode_action_, &QAction::triggered, this, &MainWindow::set_insert_mode);
    connect(overwrite_mode_action_, &QAction::triggered, this, &MainWindow::set_overwrite_mode);
    editMenu->addSeparator();
    goto_action_ = editMenu->addAction("&Go To Offset...");
    goto_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    connect(goto_action_, &QAction::triggered, this, &MainWindow::go_to_offset);

    auto* searchMenu = menuBar()->addMenu("&Search");
    find_action_ = searchMenu->addAction("&Find...");
    find_action_->setShortcut(QKeySequence::Find);
    connect(find_action_, &QAction::triggered, this, &MainWindow::find);
    find_all_action_ = searchMenu->addAction("Find &All...");
    find_all_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+F")));
    connect(find_all_action_, &QAction::triggered, this, &MainWindow::find_all);
    find_in_selection_action_ = searchMenu->addAction("Find In &Selection");
    find_in_selection_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    connect(find_in_selection_action_, &QAction::triggered, this, &MainWindow::find_in_selection);
    find_next_action_ = searchMenu->addAction("Find &Next");
    find_next_action_->setShortcut(QKeySequence::FindNext);
    connect(find_next_action_, &QAction::triggered, this, &MainWindow::find_next);
    find_previous_action_ = searchMenu->addAction("Find &Previous");
    find_previous_action_->setShortcut(QKeySequence::FindPrevious);
    connect(find_previous_action_, &QAction::triggered, this, &MainWindow::find_previous);
    replace_action_ = searchMenu->addAction("&Replace...");
    replace_action_->setShortcut(QKeySequence::Replace);
    connect(replace_action_, &QAction::triggered, this, &MainWindow::replace);
    replace_all_action_ = searchMenu->addAction("Replace &All...");
    connect(replace_all_action_, &QAction::triggered, this, &MainWindow::replace_all);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(inspector_dock_->toggleViewAction());
    viewMenu->addAction(bookmarks_dock_->toggleViewAction());
    viewMenu->addAction(search_results_dock_->toggleViewAction());
    viewMenu->addAction(analysis_dock_->toggleViewAction());
    auto* inspectorEndianMenu = viewMenu->addMenu("Inspector &Endian");
    inspector_endian_group_ = new QActionGroup(this);
    inspector_endian_group_->setExclusive(true);
    auto* little_endian_action = inspectorEndianMenu->addAction("&Little Endian");
    little_endian_action->setCheckable(true);
    little_endian_action->setChecked(true);
    auto* big_endian_action = inspectorEndianMenu->addAction("&Big Endian");
    big_endian_action->setCheckable(true);
    inspector_endian_group_->addAction(little_endian_action);
    inspector_endian_group_->addAction(big_endian_action);
    connect(little_endian_action, &QAction::triggered, this, &MainWindow::set_inspector_little_endian);
    connect(big_endian_action, &QAction::triggered, this, &MainWindow::set_inspector_big_endian);
    setup_view_menu(viewMenu);

    auto* bookmarks_menu = menuBar()->addMenu("&Bookmarks");
    toggle_bookmark_action_ = bookmarks_menu->addAction("&Toggle Bookmark");
    toggle_bookmark_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    connect(toggle_bookmark_action_, &QAction::triggered, this, &MainWindow::toggle_bookmark);
    next_bookmark_action_ = bookmarks_menu->addAction("&Next Bookmark");
    next_bookmark_action_->setShortcut(QKeySequence(Qt::Key_F2));
    connect(next_bookmark_action_, &QAction::triggered, this, &MainWindow::next_bookmark);
    previous_bookmark_action_ = bookmarks_menu->addAction("&Previous Bookmark");
    previous_bookmark_action_->setShortcut(QKeySequence(QStringLiteral("Shift+F2")));
    connect(previous_bookmark_action_, &QAction::triggered, this, &MainWindow::previous_bookmark);
    auto* tools_menu = menuBar()->addMenu("&Tools");
    compute_hashes_action_ = tools_menu->addAction("&Compute Hashes");
    connect(compute_hashes_action_, &QAction::triggered, this, &MainWindow::compute_hashes);
    settings_action_ = tools_menu->addAction("&Settings...");
    connect(settings_action_, &QAction::triggered, this, &MainWindow::open_settings);
    menuBar()->addMenu("&Help");
}

void MainWindow::setup_recent_files_menu(QMenu* file_menu) {
    recent_files_menu_ = file_menu->addMenu("Open &Recent");
    recent_files_separator_ = file_menu->addSeparator();
    update_recent_files_menu();
}

void MainWindow::update_recent_files_menu() {
    if (recent_files_menu_ == nullptr) {
        return;
    }

    recent_files_menu_->clear();
    const int max_recent = 10;
    int visible_count = 0;
    for (const QString& path : recent_files_) {
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            continue;
        }

        auto* action = recent_files_menu_->addAction(QFileInfo(path).fileName());
        action->setData(path);
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, &MainWindow::open_recent_file);
        ++visible_count;
        if (visible_count >= max_recent) {
            break;
        }
    }

    if (visible_count == 0) {
        auto* action = recent_files_menu_->addAction("No Recent Files");
        action->setEnabled(false);
    }

    if (recent_files_separator_ != nullptr) {
        recent_files_separator_->setVisible(visible_count > 0);
    }
}

void MainWindow::restore_session() {
    QSettings settings;
    recent_files_ = settings.value(QStringLiteral("recentFiles")).toStringList();
    update_recent_files_menu();

    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());

    const int bytes_per_row = settings.value(QStringLiteral("editor/bytesPerRow"), 16).toInt();
    hex_view_->set_bytes_per_row(bytes_per_row);

    const auto endian = settings.value(QStringLiteral("inspector/endian"), QStringLiteral("little")).toString();
    if (endian == QStringLiteral("big")) {
        set_inspector_big_endian();
    } else {
        set_inspector_little_endian();
    }

    const bool restore_last_session = settings.value(QStringLiteral("settings/restoreLastSession"), true).toBool();
    const QString last_file = settings.value(QStringLiteral("session/lastFile")).toString();
    if (restore_last_session && !last_file.isEmpty() && QFileInfo::exists(last_file)) {
        open_file_path(last_file);
    }
}

void MainWindow::save_session() const {
    QSettings settings;
    settings.setValue(QStringLiteral("recentFiles"), recent_files_);
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.setValue(QStringLiteral("editor/bytesPerRow"), hex_view_ ? hex_view_->bytes_per_row() : 16);
    settings.setValue(
        QStringLiteral("inspector/endian"),
        hex_view_ && hex_view_->inspector_endian() == HexView::InspectorEndian::Big ? QStringLiteral("big") : QStringLiteral("little"));
    settings.setValue(QStringLiteral("session/lastFile"), hex_view_ && hex_view_->has_document() ? hex_view_->document_path() : QString());
}

void MainWindow::add_recent_file(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    recent_files_.removeAll(path);
    recent_files_.prepend(path);
    while (recent_files_.size() > 10) {
        recent_files_.removeLast();
    }
    update_recent_files_menu();
}

bool MainWindow::confirm_discard_changes() {
    if (hex_view_ == nullptr || !hex_view_->has_document() || !hex_view_->is_dirty()) {
        return true;
    }

    const auto response = QMessageBox::warning(
        this,
        QStringLiteral("Unsaved Changes"),
        QStringLiteral("Save changes to %1 before continuing?").arg(hex_view_->document_title()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (response == QMessageBox::Cancel) {
        return false;
    }

    if (response == QMessageBox::Save) {
        if (hex_view_->is_read_only()) {
            save_file_as();
        } else {
            save_file();
        }
        return !hex_view_->is_dirty();
    }

    return true;
}

bool MainWindow::open_file_path(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }

    if (!confirm_discard_changes()) {
        return false;
    }

    if (!hex_view_->open_file(path)) {
        statusBar()->showMessage(QStringLiteral("Failed to open %1").arg(QFileInfo(path).fileName()), 4000);
        recent_files_.removeAll(path);
        update_recent_files_menu();
        return false;
    }

    add_recent_file(path);
    statusBar()->showMessage(QStringLiteral("Opened %1").arg(QFileInfo(path).fileName()), 3000);
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirm_discard_changes()) {
        event->ignore();
        return;
    }

    save_session();
    QMainWindow::closeEvent(event);
}

void MainWindow::setup_view_menu(QMenu* view_menu) {
    view_menu->addSeparator();
    auto* rowMenu = view_menu->addMenu("Bytes Per &Row");
    row_width_group_ = new QActionGroup(this);
    row_width_group_->setExclusive(true);

    const QList<int> widths = {8, 16, 24, 32, 48};
    for (const int width : widths) {
        auto* action = rowMenu->addAction(QStringLiteral("%1").arg(width));
        action->setCheckable(true);
        action->setData(width);
        if (width == hex_view_->bytes_per_row()) {
            action->setChecked(true);
        }

        row_width_group_->addAction(action);
        connect(action, &QAction::triggered, this, [this, width]() {
            set_bytes_per_row(width);
        });
    }
}

void MainWindow::setup_toolbar() {
    toolbar_ = new QToolBar("Main", this);
    toolbar_->setMovable(false);
    toolbar_->setFloatable(false);
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar_->setIconSize(QSize(16, 16));
    addToolBar(Qt::TopToolBarArea, toolbar_);

    toolbar_->addAction(open_action_);
    toolbar_->addAction(save_action_);
    toolbar_->addSeparator();
    toolbar_->addAction(undo_action_);
    toolbar_->addAction(redo_action_);
    toolbar_->addSeparator();
    toolbar_->addAction(insert_mode_action_);
    toolbar_->addAction(overwrite_mode_action_);
    toolbar_->addSeparator();
    toolbar_->addAction(toggle_bookmark_action_);
    toolbar_->addSeparator();
    toolbar_->addAction(find_action_);
    toolbar_->addSeparator();
    toolbar_->addWidget(new QLabel("Goto:", toolbar_));
    goto_offset_edit_ = new QLineEdit(toolbar_);
    goto_offset_edit_->setClearButtonEnabled(true);
    goto_offset_edit_->setPlaceholderText("0x0");
    goto_offset_edit_->setMaximumWidth(120);
    toolbar_->addWidget(goto_offset_edit_);
    auto run_goto = [this]() {
        if (!goto_offset_edit_) {
            return;
        }

        quint64 offset = 0;
        if (!try_parse_offset(goto_offset_edit_->text(), offset)) {
            statusBar()->showMessage(QStringLiteral("Invalid offset."), 2500);
            return;
        }

        if (hex_view_ && hex_view_->has_document()) {
            hex_view_->go_to_offset(static_cast<qint64>(offset));
            statusBar()->showMessage(QStringLiteral("Moved to offset 0x%1").arg(offset, 0, 16), 2500);
        }
    };
    connect(goto_offset_edit_, &QLineEdit::returnPressed, this, run_goto);
    auto* goto_now = toolbar_->addAction("Go");
    connect(goto_now, &QAction::triggered, this, run_goto);
}

void MainWindow::setup_central_widget() {
    hex_view_ = new HexView(this);
    setCentralWidget(hex_view_);

    connect(hex_view_, &HexView::status_changed, this, &MainWindow::update_status);
    connect(hex_view_, &HexView::document_loaded, this, &MainWindow::update_window_title);
    connect(hex_view_, &HexView::bookmarks_changed, this, [this](const QString& text) {
        if (bookmarks_text_ != nullptr) {
            bookmarks_text_->setPlainText(text);
        }
    });
    connect(hex_view_, &HexView::inspector_changed, this, [this](const QString& text) {
        update_inspector_view(text);
    });
}

void MainWindow::setup_docks() {
    inspector_dock_ = new QDockWidget("Inspector", this);
    inspector_tree_ = new QTreeWidget(inspector_dock_);
    inspector_tree_->setRootIsDecorated(false);
    inspector_tree_->setAlternatingRowColors(true);
    inspector_tree_->setUniformRowHeights(true);
    inspector_tree_->setColumnCount(2);
    inspector_tree_->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    inspector_tree_->header()->setStretchLastSection(true);
    inspector_tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    inspector_tree_->setSelectionMode(QAbstractItemView::NoSelection);
    inspector_tree_->setFocusPolicy(Qt::NoFocus);
    inspector_dock_->setWidget(inspector_tree_);
    inspector_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    update_inspector_view(QStringLiteral("Inspector\n\nOpen a file to inspect values."));

    bookmarks_dock_ = new QDockWidget("Bookmarks", this);
    bookmarks_text_ = new QTextEdit("Bookmarks\n\nOpen a file to add bookmarks.", bookmarks_dock_);
    bookmarks_text_->setReadOnly(true);
    bookmarks_text_->setLineWrapMode(QTextEdit::NoWrap);
    bookmarks_dock_->setWidget(bookmarks_text_);
    bookmarks_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, bookmarks_dock_);

    search_results_dock_ = new QDockWidget("Search Results", this);
    search_results_tree_ = new QTreeWidget(search_results_dock_);
    search_results_tree_->setRootIsDecorated(false);
    search_results_tree_->setAlternatingRowColors(true);
    search_results_tree_->setUniformRowHeights(true);
    search_results_tree_->setColumnCount(2);
    search_results_tree_->setHeaderLabels({QStringLiteral("Offset"), QStringLiteral("Details")});
    search_results_tree_->header()->setStretchLastSection(true);
    search_results_tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    connect(search_results_tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem*, int) { activate_search_result_item(); });
    connect(search_results_tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem*, int) { activate_search_result_item(); });
    search_results_dock_->setWidget(search_results_tree_);
    search_results_dock_->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, search_results_dock_);
    show_search_summary(QStringLiteral("No active search."));

    analysis_dock_ = new QDockWidget("Analysis", this);
    analysis_text_ = new QTextEdit("Analysis\n\nOpen a file to compute hashes.", analysis_dock_);
    analysis_text_->setReadOnly(true);
    analysis_text_->setLineWrapMode(QTextEdit::NoWrap);
    analysis_dock_->setWidget(analysis_text_);
    analysis_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, analysis_dock_);
    tabifyDockWidget(inspector_dock_, analysis_dock_);
}

void MainWindow::setup_status_bar() {
    status_label_ = new QLabel("Offset: 0x0 | Selection: 0 bytes | Size: 0 bytes | Mode: Overwrite", this);
    statusBar()->addPermanentWidget(status_label_, 1);
}

void MainWindow::new_file() {
    if (!confirm_discard_changes()) {
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, "New Buffer", QStringLiteral("untitled.bin"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        statusBar()->showMessage(QStringLiteral("Failed to create %1").arg(QFileInfo(path).fileName()), 4000);
        return;
    }
    file.close();

    if (hex_view_->open_file(path)) {
        update_window_title(hex_view_->document_title(), static_cast<qulonglong>(hex_view_->document_size()));
        add_recent_file(path);
        statusBar()->showMessage(QStringLiteral("Created %1").arg(QFileInfo(path).fileName()), 3000);
    }
}

void MainWindow::open_file() {
    const QString path = QFileDialog::getOpenFileName(this, "Open File");
    if (path.isEmpty()) {
        return;
    }

    open_file_path(path);
}

void MainWindow::open_recent_file() {
    auto* action = qobject_cast<QAction*>(sender());
    if (action == nullptr) {
        return;
    }

    open_file_path(action->data().toString());
}

void MainWindow::save_file() {
    if (!hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before saving."), 3000);
        return;
    }

    if (hex_view_->is_read_only()) {
        save_file_as();
        return;
    }

    if (!hex_view_->save()) {
        statusBar()->showMessage(QStringLiteral("Save failed."), 4000);
        return;
    }

    statusBar()->showMessage(QStringLiteral("Saved %1").arg(hex_view_->document_title()), 3000);
}

void MainWindow::save_file_as() {
    if (!hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before saving."), 3000);
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, "Save File As", hex_view_->document_path());
    if (path.isEmpty()) {
        return;
    }

    if (!hex_view_->save_as(path)) {
        statusBar()->showMessage(QStringLiteral("Save As failed."), 4000);
        return;
    }

    add_recent_file(path);
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(QFileInfo(path).fileName()), 3000);
}

void MainWindow::undo() {
    if (!hex_view_->has_document()) {
        return;
    }

    if (hex_view_->undo()) {
        statusBar()->showMessage(QStringLiteral("Undo"), 1500);
    }
}

void MainWindow::redo() {
    if (!hex_view_->has_document()) {
        return;
    }

    if (hex_view_->redo()) {
        statusBar()->showMessage(QStringLiteral("Redo"), 1500);
    }
}

void MainWindow::cut() {
    if (!hex_view_ || !hex_view_->has_document() || !hex_view_->has_selection()) {
        statusBar()->showMessage(QStringLiteral("Select a range before cutting."), 2500);
        return;
    }

    if (hex_view_->is_read_only()) {
        statusBar()->showMessage(QStringLiteral("Document is read-only."), 2500);
        return;
    }

    copy();
    if (hex_view_->delete_selection()) {
        statusBar()->showMessage(QStringLiteral("Cut selection."), 3000);
    }
}

void MainWindow::copy() {
    if (!hex_view_ || !hex_view_->has_document()) {
        return;
    }

    const QByteArray bytes = hex_view_->selected_bytes();
    if (bytes.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Select a range before copying."), 2500);
        return;
    }

    auto* clipboard = QApplication::clipboard();
    clipboard->setText(QString::fromLatin1(bytes.toHex(' ').toUpper()));
    statusBar()->showMessage(QStringLiteral("Copied %1 bytes as hex text.").arg(bytes.size()), 2500);
}

void MainWindow::copy_as_hex() {
    copy();
}

void MainWindow::paste() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before pasting."), 2500);
        return;
    }

    if (hex_view_->is_read_only()) {
        statusBar()->showMessage(QStringLiteral("Document is read-only."), 2500);
        return;
    }

    const QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Clipboard is empty."), 2500);
        return;
    }

    QByteArray bytes;
    if (!try_parse_hex_bytes(text, bytes)) {
        bytes = text.toUtf8();
    }

    const bool ok = hex_view_->edit_mode() == HexView::EditMode::Insert
        ? hex_view_->insert_at_caret(bytes)
        : (hex_view_->has_selection() ? hex_view_->overwrite_selection(bytes) : hex_view_->overwrite_at_caret(bytes));
    if (!ok) {
        statusBar()->showMessage(
            hex_view_->edit_mode() == HexView::EditMode::Insert
                ? QStringLiteral("Paste failed.")
                : QStringLiteral("Paste failed. Overwrite mode requires the byte count to fit the target range."),
            4000);
        return;
    }

    statusBar()->showMessage(QStringLiteral("Pasted %1 bytes.").arg(bytes.size()), 2500);
}

void MainWindow::paste_hex() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before pasting."), 2500);
        return;
    }

    if (hex_view_->is_read_only()) {
        statusBar()->showMessage(QStringLiteral("Document is read-only."), 2500);
        return;
    }

    const QString text = QApplication::clipboard()->text();
    QByteArray bytes;
    if (!try_parse_hex_bytes(text, bytes)) {
        statusBar()->showMessage(QStringLiteral("Clipboard does not contain valid hex bytes."), 3000);
        return;
    }

    const bool ok = hex_view_->edit_mode() == HexView::EditMode::Insert
        ? hex_view_->insert_at_caret(bytes)
        : (hex_view_->has_selection() ? hex_view_->overwrite_selection(bytes) : hex_view_->overwrite_at_caret(bytes));
    if (!ok) {
        statusBar()->showMessage(
            hex_view_->edit_mode() == HexView::EditMode::Insert
                ? QStringLiteral("Paste Hex failed.")
                : QStringLiteral("Paste Hex failed. Overwrite mode requires the byte count to fit the target range."),
            4000);
        return;
    }

    statusBar()->showMessage(QStringLiteral("Pasted %1 hex bytes.").arg(bytes.size()), 2500);
}

void MainWindow::fill_selection() {
    if (!hex_view_ || !hex_view_->has_document() || !hex_view_->has_selection()) {
        statusBar()->showMessage(QStringLiteral("Select a range before filling."), 2500);
        return;
    }

    if (hex_view_->is_read_only()) {
        statusBar()->showMessage(QStringLiteral("Document is read-only."), 2500);
        return;
    }

    bool accepted = false;
    const QString text = QInputDialog::getText(
        this,
        QStringLiteral("Fill Selection"),
        QStringLiteral("Byte value (hex, e.g. FF or 0xFF):"),
        QLineEdit::Normal,
        QStringLiteral("00"),
        &accepted);
    if (!accepted || text.trimmed().isEmpty()) {
        return;
    }

    QByteArray bytes;
    if (!try_parse_hex_bytes(text, bytes) || bytes.size() != 1) {
        statusBar()->showMessage(QStringLiteral("Enter exactly one byte in hex."), 3000);
        return;
    }

    const qint64 fill_size = hex_view_->selected_bytes().size();
    if (!hex_view_->fill_selection(static_cast<quint8>(bytes.at(0)))) {
        statusBar()->showMessage(QStringLiteral("Fill failed."), 3000);
        return;
    }

    statusBar()->showMessage(QStringLiteral("Filled %1 bytes.").arg(fill_size), 2500);
}

void MainWindow::compute_hashes() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before computing hashes."), 2500);
        return;
    }

    const bool selection_only =
        hex_view_->has_selection() &&
        QMessageBox::question(
            this,
            QStringLiteral("Compute Hashes"),
            QStringLiteral("Compute hashes for the current selection only?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes) == QMessageBox::Yes;

    if (analysis_text_ != nullptr) {
        analysis_text_->setPlainText(hex_view_->build_hash_report(selection_only));
    }
    if (analysis_dock_ != nullptr) {
        analysis_dock_->raise();
        analysis_dock_->show();
    }
    statusBar()->showMessage(QStringLiteral("Computed hashes%1.")
                                 .arg(selection_only ? QStringLiteral(" for the selection") : QStringLiteral(" for the document")),
        3000);
}

void MainWindow::open_settings() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Settings"));
    auto* layout = new QFormLayout(&dialog);

    auto* row_width = new QComboBox(&dialog);
    const QList<int> widths = {8, 16, 24, 32, 48};
    for (const int width : widths) {
        row_width->addItem(QString::number(width), width);
    }
    row_width->setCurrentText(QString::number(hex_view_ ? hex_view_->bytes_per_row() : 16));

    QSettings settings;
    auto* restore_session = new QCheckBox(QStringLiteral("Restore last open file on startup"), &dialog);
    restore_session->setChecked(settings.value(QStringLiteral("settings/restoreLastSession"), true).toBool());

    layout->addRow(QStringLiteral("Bytes per row"), row_width);
    layout->addRow(QString(), restore_session);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    set_bytes_per_row(row_width->currentData().toInt());
    settings.setValue(QStringLiteral("settings/restoreLastSession"), restore_session->isChecked());
    statusBar()->showMessage(QStringLiteral("Settings updated."), 2500);
}

void MainWindow::set_insert_mode() {
    if (hex_view_ != nullptr) {
        hex_view_->set_edit_mode(HexView::EditMode::Insert);
    }
}

void MainWindow::set_overwrite_mode() {
    if (hex_view_ != nullptr) {
        hex_view_->set_edit_mode(HexView::EditMode::Overwrite);
    }
}

bool MainWindow::prompt_for_search_pattern(
    const QString& title,
    const QString& label,
    QByteArray& pattern,
    bool& hex_mode,
    QString* display_text) {
    QDialog dialog(this);
    dialog.setWindowTitle(title);

    auto* root = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    root->addLayout(form);

    auto* text_input = new QLineEdit(&dialog);
    text_input->setClearButtonEnabled(true);
    text_input->setText(last_search_display_text_);

    auto* mode_row = new QWidget(&dialog);
    auto* mode_layout = new QHBoxLayout(mode_row);
    mode_layout->setContentsMargins(0, 0, 0, 0);
    auto* text_mode = new QRadioButton(QStringLiteral("Text"), mode_row);
    auto* hex_mode_button = new QRadioButton(QStringLiteral("Hex"), mode_row);
    auto* value_mode = new QRadioButton(QStringLiteral("Value"), mode_row);
    text_mode->setChecked(last_search_input_mode_ == SearchInputMode::Text);
    hex_mode_button->setChecked(last_search_input_mode_ == SearchInputMode::Hex);
    value_mode->setChecked(last_search_input_mode_ == SearchInputMode::Value);
    mode_layout->addWidget(text_mode);
    mode_layout->addWidget(hex_mode_button);
    mode_layout->addWidget(value_mode);
    mode_layout->addStretch(1);

    auto* encoding_combo = new QComboBox(&dialog);
    encoding_combo->addItem(encoding_label(SearchTextEncoding::Ascii), static_cast<int>(SearchTextEncoding::Ascii));
    encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf8), static_cast<int>(SearchTextEncoding::Utf8));
    encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf16Le), static_cast<int>(SearchTextEncoding::Utf16Le));
    encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf16Be), static_cast<int>(SearchTextEncoding::Utf16Be));
    encoding_combo->setCurrentIndex(encoding_combo->findData(static_cast<int>(last_search_text_encoding_)));

    auto* numeric_type_combo = new QComboBox(&dialog);
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Unsigned8), static_cast<int>(NumericSearchType::Unsigned8));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Signed8), static_cast<int>(NumericSearchType::Signed8));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Unsigned16), static_cast<int>(NumericSearchType::Unsigned16));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Signed16), static_cast<int>(NumericSearchType::Signed16));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Unsigned32), static_cast<int>(NumericSearchType::Unsigned32));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Signed32), static_cast<int>(NumericSearchType::Signed32));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Unsigned64), static_cast<int>(NumericSearchType::Unsigned64));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Signed64), static_cast<int>(NumericSearchType::Signed64));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Float32), static_cast<int>(NumericSearchType::Float32));
    numeric_type_combo->addItem(numeric_type_label(NumericSearchType::Float64), static_cast<int>(NumericSearchType::Float64));
    numeric_type_combo->setCurrentIndex(numeric_type_combo->findData(static_cast<int>(last_search_numeric_type_)));

    auto* endian_combo = new QComboBox(&dialog);
    endian_combo->addItem(QStringLiteral("Little Endian"), static_cast<int>(SearchByteOrder::Little));
    endian_combo->addItem(QStringLiteral("Big Endian"), static_cast<int>(SearchByteOrder::Big));
    endian_combo->setCurrentIndex(endian_combo->findData(static_cast<int>(last_search_numeric_byte_order_)));

    auto update_mode_widgets = [=]() {
        const bool text_selected = text_mode->isChecked();
        const bool value_selected = value_mode->isChecked();
        encoding_combo->setEnabled(text_selected);
        numeric_type_combo->setEnabled(value_selected);
        endian_combo->setEnabled(value_selected);
    };
    connect(text_mode, &QRadioButton::toggled, &dialog, update_mode_widgets);
    connect(value_mode, &QRadioButton::toggled, &dialog, update_mode_widgets);
    update_mode_widgets();

    form->addRow(label, text_input);
    form->addRow(QStringLiteral("Interpret as"), mode_row);
    form->addRow(QStringLiteral("Text encoding"), encoding_combo);
    form->addRow(QStringLiteral("Value type"), numeric_type_combo);
    form->addRow(QStringLiteral("Byte order"), endian_combo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const QString input = text_input->text().trimmed();
    if (input.isEmpty()) {
        return false;
    }

    hex_mode = !text_mode->isChecked();
    QByteArray parsed;
    if (hex_mode_button->isChecked()) {
        last_search_input_mode_ = SearchInputMode::Hex;
        if (!try_parse_hex_bytes(input, parsed)) {
            statusBar()->showMessage(QStringLiteral("Invalid hex byte sequence."), 3000);
            return false;
        }
    } else if (text_mode->isChecked()) {
        last_search_input_mode_ = SearchInputMode::Text;
        last_search_text_encoding_ = static_cast<SearchTextEncoding>(encoding_combo->currentData().toInt());
        parsed = encode_search_text(input, last_search_text_encoding_);
    } else {
        last_search_input_mode_ = SearchInputMode::Value;
        last_search_numeric_type_ = static_cast<NumericSearchType>(numeric_type_combo->currentData().toInt());
        last_search_numeric_byte_order_ = static_cast<SearchByteOrder>(endian_combo->currentData().toInt());
        if (!encode_numeric_search_value(input, last_search_numeric_type_, last_search_numeric_byte_order_, parsed)) {
            statusBar()->showMessage(QStringLiteral("Invalid typed search value."), 3000);
            return false;
        }
    }

    pattern = parsed;
    last_search_display_text_ = input;
    if (display_text != nullptr) {
        *display_text = input;
    }
    return true;
}

QByteArray MainWindow::encode_search_text(const QString& text, SearchTextEncoding encoding) {
    switch (encoding) {
    case SearchTextEncoding::Ascii:
        return text.toLatin1();
    case SearchTextEncoding::Utf8:
        return text.toUtf8();
    case SearchTextEncoding::Utf16Le: {
        QByteArray bytes;
        const auto utf16 = text.utf16();
        for (qsizetype index = 0; index < text.size(); ++index) {
            const char16_t value = utf16[index];
            bytes.append(static_cast<char>(value & 0xFF));
            bytes.append(static_cast<char>((value >> 8) & 0xFF));
        }
        return bytes;
    }
    case SearchTextEncoding::Utf16Be: {
        QByteArray bytes;
        const auto utf16 = text.utf16();
        for (qsizetype index = 0; index < text.size(); ++index) {
            const char16_t value = utf16[index];
            bytes.append(static_cast<char>((value >> 8) & 0xFF));
            bytes.append(static_cast<char>(value & 0xFF));
        }
        return bytes;
    }
    }

    return text.toUtf8();
}

QString MainWindow::numeric_type_label(NumericSearchType type) {
    switch (type) {
    case NumericSearchType::Unsigned8:
        return QStringLiteral("u8");
    case NumericSearchType::Signed8:
        return QStringLiteral("i8");
    case NumericSearchType::Unsigned16:
        return QStringLiteral("u16");
    case NumericSearchType::Signed16:
        return QStringLiteral("i16");
    case NumericSearchType::Unsigned32:
        return QStringLiteral("u32");
    case NumericSearchType::Signed32:
        return QStringLiteral("i32");
    case NumericSearchType::Unsigned64:
        return QStringLiteral("u64");
    case NumericSearchType::Signed64:
        return QStringLiteral("i64");
    case NumericSearchType::Float32:
        return QStringLiteral("f32");
    case NumericSearchType::Float64:
        return QStringLiteral("f64");
    }

    return QStringLiteral("u32");
}

bool MainWindow::encode_numeric_search_value(
    const QString& text,
    NumericSearchType type,
    SearchByteOrder byte_order,
    QByteArray& bytes) {
    const auto append_unsigned = [&bytes, byte_order](quint64 value, int width) {
        bytes.clear();
        bytes.reserve(width);
        for (int index = 0; index < width; ++index) {
            const int shift = byte_order == SearchByteOrder::Little ? index * 8 : (width - 1 - index) * 8;
            bytes.append(static_cast<char>((value >> shift) & 0xFFU));
        }
    };

    const auto normalized = text.trimmed();
    bool ok = false;
    switch (type) {
    case NumericSearchType::Unsigned8: {
        const quint64 value = normalized.toULongLong(&ok, 0);
        if (!ok || value > 0xFFU) {
            return false;
        }
        append_unsigned(value, 1);
        return true;
    }
    case NumericSearchType::Signed8: {
        const qint64 value = normalized.toLongLong(&ok, 0);
        if (!ok || value < -128 || value > 127) {
            return false;
        }
        append_unsigned(static_cast<quint8>(static_cast<qint8>(value)), 1);
        return true;
    }
    case NumericSearchType::Unsigned16: {
        const quint64 value = normalized.toULongLong(&ok, 0);
        if (!ok || value > 0xFFFFU) {
            return false;
        }
        append_unsigned(value, 2);
        return true;
    }
    case NumericSearchType::Signed16: {
        const qint64 value = normalized.toLongLong(&ok, 0);
        if (!ok || value < -32768 || value > 32767) {
            return false;
        }
        append_unsigned(static_cast<quint16>(static_cast<qint16>(value)), 2);
        return true;
    }
    case NumericSearchType::Unsigned32: {
        const quint64 value = normalized.toULongLong(&ok, 0);
        if (!ok || value > 0xFFFFFFFFULL) {
            return false;
        }
        append_unsigned(value, 4);
        return true;
    }
    case NumericSearchType::Signed32: {
        const qint64 value = normalized.toLongLong(&ok, 0);
        if (!ok || value < std::numeric_limits<qint32>::min() || value > std::numeric_limits<qint32>::max()) {
            return false;
        }
        append_unsigned(static_cast<quint32>(static_cast<qint32>(value)), 4);
        return true;
    }
    case NumericSearchType::Unsigned64: {
        const quint64 value = normalized.toULongLong(&ok, 0);
        if (!ok) {
            return false;
        }
        append_unsigned(value, 8);
        return true;
    }
    case NumericSearchType::Signed64: {
        const qint64 value = normalized.toLongLong(&ok, 0);
        if (!ok) {
            return false;
        }
        append_unsigned(static_cast<quint64>(value), 8);
        return true;
    }
    case NumericSearchType::Float32: {
        const float value = normalized.toFloat(&ok);
        if (!ok) {
            return false;
        }
        bytes.resize(4);
        const auto raw = reinterpret_cast<const unsigned char*>(&value);
        for (int index = 0; index < 4; ++index) {
            const int source_index = byte_order == SearchByteOrder::Little ? index : (3 - index);
            bytes[index] = static_cast<char>(raw[source_index]);
        }
        return true;
    }
    case NumericSearchType::Float64: {
        const double value = normalized.toDouble(&ok);
        if (!ok) {
            return false;
        }
        bytes.resize(8);
        const auto raw = reinterpret_cast<const unsigned char*>(&value);
        for (int index = 0; index < 8; ++index) {
            const int source_index = byte_order == SearchByteOrder::Little ? index : (7 - index);
            bytes[index] = static_cast<char>(raw[source_index]);
        }
        return true;
    }
    }

    return false;
}

QString MainWindow::encoding_label(SearchTextEncoding encoding) {
    switch (encoding) {
    case SearchTextEncoding::Ascii:
        return QStringLiteral("ASCII");
    case SearchTextEncoding::Utf8:
        return QStringLiteral("UTF-8");
    case SearchTextEncoding::Utf16Le:
        return QStringLiteral("UTF-16 LE");
    case SearchTextEncoding::Utf16Be:
        return QStringLiteral("UTF-16 BE");
    }

    return QStringLiteral("UTF-8");
}

void MainWindow::go_to_offset() {
    if (!hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before navigating."), 3000);
        return;
    }

    bool accepted = false;
    const QString text = QInputDialog::getText(
        this,
        QStringLiteral("Go To Offset"),
        QStringLiteral("Offset (decimal or 0x-prefixed hex):"),
        QLineEdit::Normal,
        QStringLiteral("0x%1").arg(hex_view_->document_size() > 0 ? 0 : 0),
        &accepted);

    if (!accepted || text.trimmed().isEmpty()) {
        return;
    }

    quint64 offset = 0;
    if (!try_parse_offset(text, offset)) {
        statusBar()->showMessage(QStringLiteral("Invalid offset: %1").arg(text.trimmed()), 4000);
        return;
    }

    hex_view_->go_to_offset(static_cast<qint64>(offset));
    statusBar()->showMessage(QStringLiteral("Moved to offset 0x%1").arg(offset, 0, 16), 3000);
}

void MainWindow::set_bytes_per_row(int bytes_per_row) {
    hex_view_->set_bytes_per_row(bytes_per_row);
    statusBar()->showMessage(QStringLiteral("Bytes per row set to %1").arg(bytes_per_row), 3000);
}

void MainWindow::toggle_bookmark() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before bookmarking."), 2500);
        return;
    }

    hex_view_->toggle_bookmark_at_caret();
    statusBar()->showMessage(QStringLiteral("Toggled bookmark at current offset."), 2000);
}

void MainWindow::next_bookmark() {
    if (!hex_view_ || !hex_view_->has_document()) {
        return;
    }

    hex_view_->next_bookmark();
    statusBar()->showMessage(QStringLiteral("Moved to next bookmark."), 1500);
}

void MainWindow::previous_bookmark() {
    if (!hex_view_ || !hex_view_->has_document()) {
        return;
    }

    hex_view_->previous_bookmark();
    statusBar()->showMessage(QStringLiteral("Moved to previous bookmark."), 1500);
}

void MainWindow::find() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before searching."), 2500);
        return;
    }

    QByteArray pattern;
    bool hex_mode = false;
    if (!prompt_for_search_pattern(QStringLiteral("Find"), QStringLiteral("Pattern"), pattern, hex_mode)) {
        return;
    }

    last_search_hex_mode_ = hex_mode;
    last_search_pattern_ = pattern;
    run_search(true, false);
}

void MainWindow::find_all() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before searching."), 2500);
        return;
    }

    QByteArray pattern;
    bool hex_mode = false;
    if (!prompt_for_search_pattern(QStringLiteral("Find All"), QStringLiteral("Pattern"), pattern, hex_mode)) {
        return;
    }

    last_search_hex_mode_ = hex_mode;
    last_search_pattern_ = pattern;

    const bool selection_only =
        hex_view_->has_selection() &&
        QMessageBox::question(
            this,
            QStringLiteral("Find All"),
            QStringLiteral("Limit the find-all operation to the current selection?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes) == QMessageBox::Yes;
    run_find_all(selection_only);
}

void MainWindow::find_in_selection() {
    if (!hex_view_ || !hex_view_->has_document() || !hex_view_->has_selection()) {
        statusBar()->showMessage(QStringLiteral("Select a range before searching in selection."), 2500);
        return;
    }

    QByteArray pattern;
    bool hex_mode = false;
    if (!prompt_for_search_pattern(QStringLiteral("Find In Selection"), QStringLiteral("Pattern"), pattern, hex_mode)) {
        return;
    }

    last_search_hex_mode_ = hex_mode;
    last_search_pattern_ = pattern;
    run_search(true, false, true);
}

void MainWindow::find_next() {
    run_search(true, true);
}

void MainWindow::find_previous() {
    run_search(false, true);
}

void MainWindow::replace() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before replacing."), 2500);
        return;
    }

    QByteArray before;
    bool hex_mode = false;
    if (!prompt_for_search_pattern(QStringLiteral("Replace"), QStringLiteral("Find pattern"), before, hex_mode)) {
        return;
    }

    bool accepted = false;
    const QString replace_text = QInputDialog::getText(
        this,
        QStringLiteral("Replace"),
        QStringLiteral("Replace with (same length only in current overwrite mode):"),
        QLineEdit::Normal,
        QString(),
        &accepted);
    if (!accepted) {
        return;
    }

    QByteArray after;
    if (hex_mode) {
        if (!try_parse_hex_bytes(replace_text, after)) {
            statusBar()->showMessage(QStringLiteral("Replacement must be valid hex bytes."), 3000);
            return;
        }
    } else {
        after = encode_search_text(replace_text, last_search_text_encoding_);
    }

    if (hex_view_->edit_mode() == HexView::EditMode::Overwrite && before.size() != after.size()) {
        statusBar()->showMessage(QStringLiteral("Replacement must match the original length in overwrite mode."), 4000);
        return;
    }

    last_search_pattern_ = before;
    last_search_hex_mode_ = hex_mode;

    qint64 found_offset = -1;
    if (!hex_view_->find_pattern(before, true, true, &found_offset)) {
        if (!hex_view_->find_pattern(before, true, false, &found_offset)) {
            statusBar()->showMessage(QStringLiteral("No match available to replace."), 3000);
            return;
        }
    }

    if (!hex_view_->replace_range(found_offset, before, after)) {
        statusBar()->showMessage(QStringLiteral("Replace failed."), 3000);
        return;
    }

    show_search_summary(
        QStringLiteral("Replaced %1 bytes at 0x%2")
            .arg(after.size())
            .arg(found_offset, 8, 16, QChar(u'0'))
            .toUpper());
    statusBar()->showMessage(QStringLiteral("Replaced match at 0x%1").arg(found_offset, 0, 16), 2500);
}

void MainWindow::replace_all() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before replacing."), 2500);
        return;
    }

    if (hex_view_->is_read_only()) {
        statusBar()->showMessage(QStringLiteral("Document is read-only."), 2500);
        return;
    }

    QByteArray before;
    bool hex_mode = false;
    if (!prompt_for_search_pattern(QStringLiteral("Replace All"), QStringLiteral("Find pattern"), before, hex_mode)) {
        return;
    }

    bool accepted = false;
    const QString replace_text = QInputDialog::getText(
        this,
        QStringLiteral("Replace All"),
        QStringLiteral("Replace with (same length only in overwrite mode):"),
        QLineEdit::Normal,
        QString(),
        &accepted);
    if (!accepted) {
        return;
    }

    QByteArray after;
    if (hex_mode) {
        if (!try_parse_hex_bytes(replace_text, after)) {
            statusBar()->showMessage(QStringLiteral("Replacement must be valid hex bytes."), 3000);
            return;
        }
    } else {
        after = encode_search_text(replace_text, last_search_text_encoding_);
    }

    if (hex_view_->edit_mode() == HexView::EditMode::Overwrite && before.size() != after.size()) {
        statusBar()->showMessage(QStringLiteral("Replacement must match the original length in overwrite mode."), 4000);
        return;
    }

    const bool selection_only =
        hex_view_->has_selection() &&
        QMessageBox::question(
            this,
            QStringLiteral("Replace All"),
            QStringLiteral("Limit the replace-all operation to the current selection?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes) == QMessageBox::Yes;

    const qint64 replaced = hex_view_->replace_all(before, after, selection_only);
    last_search_pattern_ = before;
    last_search_hex_mode_ = hex_mode;

    if (replaced <= 0) {
        statusBar()->showMessage(QStringLiteral("No matches replaced."), 2500);
        return;
    }

    show_search_summary(
        QStringLiteral("Replaced %1 occurrence(s)%2.")
            .arg(replaced)
            .arg(selection_only ? QStringLiteral(" in the current selection") : QString()));
    statusBar()->showMessage(QStringLiteral("Replaced %1 occurrence(s).").arg(replaced), 3000);
}

void MainWindow::set_inspector_little_endian() {
    if (hex_view_ != nullptr) {
        hex_view_->set_inspector_endian(HexView::InspectorEndian::Little);
    }
}

void MainWindow::set_inspector_big_endian() {
    if (hex_view_ != nullptr) {
        hex_view_->set_inspector_endian(HexView::InspectorEndian::Big);
    }
}

void MainWindow::update_status(qulonglong caret_offset, qulonglong selection_size, qulonglong document_size) {
    const bool dirty = hex_view_ && hex_view_->is_dirty();
    const bool read_only = hex_view_ && hex_view_->is_read_only();
    const bool insert_mode = hex_view_ && hex_view_->edit_mode() == HexView::EditMode::Insert;
    status_label_->setText(
        QStringLiteral("Offset: 0x%1 | Selection: %2 bytes | Size: %3 bytes | Row: %4 | Mode: %5")
            .arg(caret_offset, 0, 16)
            .arg(selection_size)
            .arg(document_size)
            .arg(hex_view_ ? hex_view_->bytes_per_row() : 16)
            .arg(read_only
                    ? QStringLiteral("Read Only")
                    : (insert_mode
                            ? (dirty ? QStringLiteral("Insert*") : QStringLiteral("Insert"))
                            : (dirty ? QStringLiteral("Overwrite*") : QStringLiteral("Overwrite")))));

    if (insert_mode_action_ != nullptr && overwrite_mode_action_ != nullptr) {
        insert_mode_action_->setChecked(insert_mode);
        overwrite_mode_action_->setChecked(!insert_mode);
    }

    if (hex_view_ && hex_view_->has_document() && dirty != last_dirty_state_) {
        last_dirty_state_ = dirty;
        update_window_title(hex_view_->document_title(), document_size);
    }
}

void MainWindow::update_window_title(const QString& title, qulonglong document_size) {
    const bool dirty = hex_view_ && hex_view_->is_dirty();
    last_dirty_state_ = dirty;
    const QString dirty_marker = dirty ? QStringLiteral("*") : QString();
    setWindowTitle(QStringLiteral("%1%2 (%3 bytes) - Hex Master").arg(title, dirty_marker).arg(document_size));
}

void MainWindow::update_inspector_view(const QString& text) {
    if (inspector_tree_ == nullptr) {
        return;
    }

    inspector_tree_->clear();
    if (hex_view_ != nullptr) {
        QString current_section;
        QTreeWidgetItem* current_group = nullptr;
        const QVector<HexView::InspectorRow> rows = hex_view_->inspector_rows();
        for (const HexView::InspectorRow& row : rows) {
            if (row.section != current_section) {
                current_section = row.section;
                current_group = new QTreeWidgetItem(inspector_tree_, QStringList{current_section});
                current_group->setFirstColumnSpanned(true);
                current_group->setExpanded(true);
            }

            if (current_group != nullptr) {
                new QTreeWidgetItem(current_group, QStringList{row.field, row.value});
            }
        }
    }

    if (inspector_tree_->topLevelItemCount() == 0) {
        auto* item = new QTreeWidgetItem(inspector_tree_, QStringList{QStringLiteral("Inspector"), text.trimmed()});
        item->setFirstColumnSpanned(text.trimmed().isEmpty() || text.trimmed() == QStringLiteral("Inspector"));
    }

    inspector_tree_->expandAll();
}

void MainWindow::show_search_summary(const QString& summary) {
    if (search_results_tree_ == nullptr) {
        return;
    }

    search_results_tree_->clear();
    auto* item = new QTreeWidgetItem(search_results_tree_, QStringList{QStringLiteral("Summary"), summary});
    item->setFirstColumnSpanned(false);
}

void MainWindow::show_search_matches(const QString& summary, const QVector<qint64>& matches) {
    if (search_results_tree_ == nullptr) {
        return;
    }

    search_results_tree_->clear();
    auto* summary_item = new QTreeWidgetItem(search_results_tree_, QStringList{QStringLiteral("Summary"), summary});
    summary_item->setFirstColumnSpanned(false);

    auto* results_root = new QTreeWidgetItem(search_results_tree_, QStringList{QStringLiteral("Results"), QStringLiteral("%1 match(es)").arg(matches.size())});
    results_root->setExpanded(true);

    const int max_results = 512;
    const int shown = qMin(matches.size(), max_results);
    for (int index = 0; index < shown; ++index) {
        const qint64 offset = matches.at(index);
        auto* item = new QTreeWidgetItem(
            results_root,
            QStringList{
                QStringLiteral("0x%1").arg(offset, 8, 16, QChar(u'0')).toUpper(),
                QStringLiteral("Match %1").arg(index + 1)});
        item->setData(0, Qt::UserRole, offset);
    }

    if (matches.size() > max_results) {
        new QTreeWidgetItem(
            results_root,
            QStringList{
                QStringLiteral("More"),
                QStringLiteral("Showing first %1 matches").arg(max_results)});
    }

    search_results_tree_->expandAll();
}

void MainWindow::activate_search_result_item() {
    if (search_results_tree_ == nullptr || hex_view_ == nullptr || !hex_view_->has_document()) {
        return;
    }

    QTreeWidgetItem* item = search_results_tree_->currentItem();
    if (item == nullptr) {
        return;
    }

    const QVariant offset_value = item->data(0, Qt::UserRole);
    if (!offset_value.isValid()) {
        return;
    }

    const qint64 offset = offset_value.toLongLong();
    hex_view_->go_to_offset(offset);
    statusBar()->showMessage(QStringLiteral("Moved to search result at 0x%1").arg(offset, 0, 16), 2500);
}

void MainWindow::run_search(bool forward, bool from_caret, bool selection_only) {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before searching."), 2500);
        return;
    }

    if (last_search_pattern_.isEmpty()) {
        find();
        return;
    }

    qint64 found_offset = -1;
    const bool found = selection_only
        ? hex_view_->find_pattern_in_selection(last_search_pattern_, forward, from_caret, &found_offset)
        : hex_view_->find_pattern(last_search_pattern_, forward, from_caret, &found_offset);
    if (!found) {
        show_search_summary(
            QStringLiteral("No matches found%1.")
                .arg(selection_only ? QStringLiteral(" in the current selection") : QString()));
        statusBar()->showMessage(QStringLiteral("No matches found."), 2500);
        return;
    }

    show_search_matches(
        QStringLiteral("1 match at 0x%1").arg(found_offset, 8, 16, QChar(u'0')).toUpper(),
        QVector<qint64>{found_offset});
    statusBar()->showMessage(QStringLiteral("Match at 0x%1").arg(found_offset, 0, 16), 2500);
}

void MainWindow::run_find_all(bool selection_only) {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before searching."), 2500);
        return;
    }

    if (last_search_pattern_.isEmpty()) {
        find_all();
        return;
    }

    const QVector<qint64> matches = hex_view_->find_all_patterns(last_search_pattern_, selection_only);
    if (matches.isEmpty()) {
        show_search_summary(
            QStringLiteral("No matches found%1.")
                .arg(selection_only ? QStringLiteral(" in the current selection") : QString()));
    } else {
        show_search_matches(
            QStringLiteral("%1 match(es)%2.")
                .arg(matches.size())
                .arg(selection_only ? QStringLiteral(" in the current selection") : QString()),
            matches);
    }
    if (search_results_dock_ != nullptr) {
        search_results_dock_->raise();
        search_results_dock_->show();
    }
    statusBar()->showMessage(
        matches.isEmpty()
            ? QStringLiteral("No matches found.")
            : QStringLiteral("Found %1 match(es).").arg(matches.size()),
        3000);
}

bool MainWindow::try_parse_hex_bytes(const QString& text, QByteArray& bytes) {
    QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        return false;
    }

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

bool MainWindow::try_parse_offset(const QString& text, quint64& value) {
    QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        return false;
    }

    int base = 10;
    if (normalized.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        normalized = normalized.mid(2);
        base = 16;
    }

    bool ok = false;
    value = normalized.toULongLong(&ok, base);
    return ok;
}
