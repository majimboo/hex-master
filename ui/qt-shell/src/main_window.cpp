#include "main_window.hpp"

#include "hex_view.hpp"

#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Hex Master");
    setup_central_widget();
    setup_menu();
    setup_toolbar();
    setup_docks();
    setup_status_bar();
}

void MainWindow::setup_menu() {
    auto* fileMenu = menuBar()->addMenu("&File");
    open_action_ = fileMenu->addAction("&Open...");
    connect(open_action_, &QAction::triggered, this, &MainWindow::open_file);
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
    editMenu->addAction("Cu&t");
    editMenu->addAction("&Copy");
    editMenu->addAction("&Paste");
    editMenu->addSeparator();
    goto_action_ = editMenu->addAction("&Go To Offset...");
    goto_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    connect(goto_action_, &QAction::triggered, this, &MainWindow::go_to_offset);

    auto* searchMenu = menuBar()->addMenu("&Search");
    find_action_ = searchMenu->addAction("&Find...");
    find_action_->setShortcut(QKeySequence::Find);
    connect(find_action_, &QAction::triggered, this, &MainWindow::find);
    find_next_action_ = searchMenu->addAction("Find &Next");
    find_next_action_->setShortcut(QKeySequence::FindNext);
    connect(find_next_action_, &QAction::triggered, this, &MainWindow::find_next);
    find_previous_action_ = searchMenu->addAction("Find &Previous");
    find_previous_action_->setShortcut(QKeySequence::FindPrevious);
    connect(find_previous_action_, &QAction::triggered, this, &MainWindow::find_previous);
    replace_action_ = searchMenu->addAction("&Replace...");
    replace_action_->setShortcut(QKeySequence::Replace);
    connect(replace_action_, &QAction::triggered, this, &MainWindow::replace);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("&Inspector");
    viewMenu->addAction("&Bookmarks");
    viewMenu->addAction("&Search Results");
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
    menuBar()->addMenu("&Tools");
    menuBar()->addMenu("&Help");
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
        if (inspector_text_ != nullptr) {
            inspector_text_->setPlainText(text);
        }
    });
}

void MainWindow::setup_docks() {
    inspector_dock_ = new QDockWidget("Inspector", this);
    inspector_text_ = new QTextEdit("Inspector\n\nOpen a file to inspect values.", inspector_dock_);
    inspector_text_->setReadOnly(true);
    inspector_text_->setLineWrapMode(QTextEdit::NoWrap);
    inspector_dock_->setWidget(inspector_text_);
    inspector_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);

    bookmarks_dock_ = new QDockWidget("Bookmarks", this);
    bookmarks_text_ = new QTextEdit("Bookmarks\n\nOpen a file to add bookmarks.", bookmarks_dock_);
    bookmarks_text_->setReadOnly(true);
    bookmarks_text_->setLineWrapMode(QTextEdit::NoWrap);
    bookmarks_dock_->setWidget(bookmarks_text_);
    bookmarks_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, bookmarks_dock_);

    search_results_dock_ = new QDockWidget("Search Results", this);
    search_results_text_ = new QTextEdit("Search Results\n\nNo active search.", search_results_dock_);
    search_results_text_->setReadOnly(true);
    search_results_text_->setLineWrapMode(QTextEdit::NoWrap);
    search_results_dock_->setWidget(search_results_text_);
    search_results_dock_->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, search_results_dock_);
}

void MainWindow::setup_status_bar() {
    status_label_ = new QLabel("Offset: 0x0 | Selection: 0 bytes | Size: 0 bytes | Mode: Overwrite", this);
    statusBar()->addPermanentWidget(status_label_, 1);
}

void MainWindow::open_file() {
    const QString path = QFileDialog::getOpenFileName(this, "Open File");
    if (path.isEmpty()) {
        return;
    }

    if (!hex_view_->open_file(path)) {
        statusBar()->showMessage(QStringLiteral("Failed to open %1").arg(QFileInfo(path).fileName()), 4000);
        return;
    }

    statusBar()->showMessage(QStringLiteral("Opened %1").arg(QFileInfo(path).fileName()), 3000);
}

void MainWindow::save_file() {
    if (!hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before saving."), 3000);
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

    bool accepted = false;
    const QString text = QInputDialog::getText(
        this,
        QStringLiteral("Find"),
        QStringLiteral("Enter text or hex bytes (for hex use spaces or a 0x prefix):"),
        QLineEdit::Normal,
        QString(),
        &accepted);
    if (!accepted || text.trimmed().isEmpty()) {
        return;
    }

    QByteArray hex_bytes;
    last_search_hex_mode_ = try_parse_hex_bytes(text, hex_bytes);
    last_search_pattern_ = last_search_hex_mode_ ? hex_bytes : text.toUtf8();
    run_search(true, false);
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

    bool accepted = false;
    const QString find_text = QInputDialog::getText(
        this,
        QStringLiteral("Replace"),
        QStringLiteral("Find text or hex bytes:"),
        QLineEdit::Normal,
        last_search_hex_mode_ ? QString::fromLatin1(last_search_pattern_.toHex(' ').toUpper()) : QString::fromUtf8(last_search_pattern_),
        &accepted);
    if (!accepted || find_text.trimmed().isEmpty()) {
        return;
    }

    QByteArray before;
    const bool hex_mode = try_parse_hex_bytes(find_text, before);
    if (before.isEmpty()) {
        before = find_text.toUtf8();
    }

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
        after = replace_text.toUtf8();
    }

    if (before.size() != after.size()) {
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

    if (search_results_text_ != nullptr) {
        search_results_text_->setPlainText(
            QStringLiteral("Search Results\n\nReplaced %1 bytes at 0x%2")
                .arg(after.size())
                .arg(found_offset, 8, 16, QChar(u'0'))
                .toUpper());
    }
    statusBar()->showMessage(QStringLiteral("Replaced match at 0x%1").arg(found_offset, 0, 16), 2500);
}

void MainWindow::update_status(qulonglong caret_offset, qulonglong selection_size, qulonglong document_size) {
    const bool dirty = hex_view_ && hex_view_->is_dirty();
    status_label_->setText(
        QStringLiteral("Offset: 0x%1 | Selection: %2 bytes | Size: %3 bytes | Row: %4 | Mode: %5")
            .arg(caret_offset, 0, 16)
            .arg(selection_size)
            .arg(document_size)
            .arg(hex_view_ ? hex_view_->bytes_per_row() : 16)
            .arg(dirty ? QStringLiteral("Overwrite*") : QStringLiteral("Overwrite")));

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

void MainWindow::run_search(bool forward, bool from_caret) {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before searching."), 2500);
        return;
    }

    if (last_search_pattern_.isEmpty()) {
        find();
        return;
    }

    qint64 found_offset = -1;
    if (!hex_view_->find_pattern(last_search_pattern_, forward, from_caret, &found_offset)) {
        if (search_results_text_ != nullptr) {
            search_results_text_->setPlainText(QStringLiteral("Search Results\n\nNo matches found."));
        }
        statusBar()->showMessage(QStringLiteral("No matches found."), 2500);
        return;
    }

    if (search_results_text_ != nullptr) {
        search_results_text_->setPlainText(
            hex_view_->format_search_result(found_offset, last_search_pattern_, last_search_hex_mode_));
    }
    statusBar()->showMessage(QStringLiteral("Match at 0x%1").arg(found_offset, 0, 16), 2500);
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
