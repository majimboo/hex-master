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
#include <QColorDialog>
#include <QGroupBox>
#include <QFormLayout>
#include <QComboBox>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPalette>
#include <QPainter>
#include <QSettings>
#include <QStatusBar>
#include <QStyleOptionButton>
#include <QTabWidget>
#include <QTextEdit>
#include <QTreeWidget>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QToolBar>
#include <QWidget>

#include <limits>

namespace {
class TreeValueDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(option);
        Q_UNUSED(index);
        QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto* line_edit = qobject_cast<QLineEdit*>(editor)) {
            QPalette palette = line_edit->palette();
            palette.setColor(QPalette::Base, QColor(43, 108, 176));
            palette.setColor(QPalette::Text, QColor(255, 255, 255));
            palette.setColor(QPalette::Highlight, QColor(173, 216, 255));
            palette.setColor(QPalette::HighlightedText, QColor(24, 32, 40));
            line_edit->setPalette(palette);
            line_edit->setAutoFillBackground(true);
            line_edit->setFrame(false);
            line_edit->setContentsMargins(0, 0, 0, 0);
            line_edit->setTextMargins(0, 0, 0, 0);
            line_edit->setStyleSheet(QStringLiteral(
                "QLineEdit {"
                " margin: 0px;"
                " padding: 0px;"
                " border: 0px;"
                " background: #2b6cb0;"
                " color: white;"
                " selection-background-color: #add8ff;"
                " selection-color: #182028;"
                "}"
            ));
        }
        return editor;
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(index);
        editor->setGeometry(option.rect);
    }
};

class EditModeSegmentedControl final : public QWidget {
public:
    explicit EditModeSegmentedControl(QWidget* parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedHeight(28);
    }

    void set_actions(QAction* insert_action, QAction* overwrite_action) {
        insert_action_ = insert_action;
        overwrite_action_ = overwrite_action;
        if (insert_action_ != nullptr) {
            QObject::connect(insert_action_, &QAction::changed, this, [this]() { update(); });
            QObject::connect(insert_action_, &QAction::toggled, this, [this]() { update(); });
        }
        if (overwrite_action_ != nullptr) {
            QObject::connect(overwrite_action_, &QAction::changed, this, [this]() { update(); });
            QObject::connect(overwrite_action_, &QAction::toggled, this, [this]() { update(); });
        }
        updateGeometry();
        update();
    }

    QSize sizeHint() const override {
        const QFontMetrics fm(font());
        const int segment_width = qMax(76, qMax(fm.horizontalAdvance(QStringLiteral("Insert")), fm.horizontalAdvance(QStringLiteral("Overwrite"))) + 24);
        return QSize(segment_width * 2, 28);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        const QRect left_rect(0, 0, width() / 2 + 1, height());
        const QRect right_rect(width() / 2, 0, width() - (width() / 2), height());

        QStyleOptionButton left_button;
        init_button_option(&left_button, left_rect, QStringLiteral("Insert"), checked_segment() == 0, hovered_segment_ == 0, pressed_segment_ == 0);
        QStyleOptionButton right_button;
        init_button_option(&right_button, right_rect, QStringLiteral("Overwrite"), checked_segment() == 1, hovered_segment_ == 1, pressed_segment_ == 1);

        painter.save();
        painter.setClipRect(left_rect.adjusted(0, 0, -1, 0));
        style()->drawControl(QStyle::CE_PushButtonBevel, &left_button, &painter, this);
        style()->drawControl(QStyle::CE_PushButtonLabel, &left_button, &painter, this);
        painter.restore();

        painter.save();
        painter.setClipRect(right_rect.adjusted(1, 0, 0, 0));
        style()->drawControl(QStyle::CE_PushButtonBevel, &right_button, &painter, this);
        style()->drawControl(QStyle::CE_PushButtonLabel, &right_button, &painter, this);
        painter.restore();

        const QColor button_face = palette().color(QPalette::Button);
        painter.setPen(QPen(button_face, 2));
        painter.drawLine(width() / 2, 1, width() / 2, height() - 2);

        painter.setPen(palette().color(QPalette::Midlight));
        painter.drawLine(width() / 2, 2, width() / 2, height() - 3);

        if (hasFocus()) {
            QStyleOptionFocusRect focus_option;
            focus_option.initFrom(this);
            focus_option.rect = rect().adjusted(3, 3, -3, -3);
            focus_option.backgroundColor = palette().color(QPalette::Button);
            style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus_option, &painter, this);
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        pressed_segment_ = segment_at(event->pos());
        if (pressed_segment_ >= 0) {
            update();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        hovered_segment_ = segment_at(event->pos());
        if (pressed_segment_ >= 0 && hovered_segment_ != pressed_segment_) {
            update();
        } else {
            update();
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        const int released_segment = segment_at(event->pos());
        if (event->button() == Qt::LeftButton && pressed_segment_ >= 0 && released_segment == pressed_segment_) {
            trigger_segment(released_segment);
            event->accept();
        }
        pressed_segment_ = -1;
        update();
        QWidget::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_segment_ = -1;
        update();
        QWidget::leaveEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Left) {
            trigger_segment(0);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right) {
            trigger_segment(1);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            trigger_segment(checked_segment() == 0 ? 1 : 0);
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    void init_button_option(QStyleOptionButton* option, const QRect& rect, const QString& label, bool checked, bool hovered, bool pressed) const {
        option->initFrom(this);
        option->rect = rect;
        option->text = label;
        option->features = QStyleOptionButton::None;
        option->state |= QStyle::State_Raised;
        if (hovered) {
            option->state |= QStyle::State_MouseOver;
        }
        if (checked) {
            option->state |= QStyle::State_On | QStyle::State_Sunken;
        }
        if (pressed) {
            option->state |= QStyle::State_Sunken;
        }
    }

    int checked_segment() const {
        if (insert_action_ != nullptr && insert_action_->isChecked()) {
            return 0;
        }
        if (overwrite_action_ != nullptr && overwrite_action_->isChecked()) {
            return 1;
        }
        return -1;
    }

    int segment_at(const QPoint& pos) const {
        if (!rect().contains(pos)) {
            return -1;
        }
        return pos.x() < width() / 2 ? 0 : 1;
    }

    void trigger_segment(int segment) {
        QAction* action = segment == 0 ? insert_action_ : overwrite_action_;
        if (action != nullptr && action->isEnabled()) {
            action->trigger();
        }
    }

    QAction* insert_action_ = nullptr;
    QAction* overwrite_action_ = nullptr;
    int hovered_segment_ = -1;
    int pressed_segment_ = -1;
};
}

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
    auto* help_menu = menuBar()->addMenu("&Help");
    auto* about_action = help_menu->addAction("&About Hex Master");
    connect(about_action, &QAction::triggered, this, &MainWindow::show_about);
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
    goto_offset_history_ = settings.value(QStringLiteral("gotoOffsetHistory")).toStringList();
    search_history_ = settings.value(QStringLiteral("search/history")).toStringList();
    replace_history_ = settings.value(QStringLiteral("replace/history")).toStringList();
    while (search_history_.size() > 15) {
        search_history_.removeLast();
    }
    while (replace_history_.size() > 15) {
        replace_history_.removeLast();
    }
    last_search_display_text_ = settings.value(QStringLiteral("search/lastText")).toString();
    last_search_hex_mode_ = settings.value(QStringLiteral("search/lastHexMode"), false).toBool();
    last_search_input_mode_ = static_cast<SearchInputMode>(
        settings.value(QStringLiteral("search/lastInputMode"), static_cast<int>(SearchInputMode::Text)).toInt());
    last_search_text_encoding_ = static_cast<SearchTextEncoding>(
        settings.value(QStringLiteral("search/lastTextEncoding"), static_cast<int>(SearchTextEncoding::Utf8)).toInt());
    last_search_numeric_type_ = static_cast<NumericSearchType>(
        settings.value(QStringLiteral("search/lastNumericType"), static_cast<int>(NumericSearchType::Unsigned32)).toInt());
    last_search_numeric_byte_order_ = static_cast<SearchByteOrder>(
        settings.value(QStringLiteral("search/lastByteOrder"), static_cast<int>(SearchByteOrder::Little)).toInt());
    last_search_execution_ = static_cast<SearchExecution>(
        settings.value(QStringLiteral("search/lastExecution"), static_cast<int>(SearchExecution::FindNext)).toInt());
    last_search_selection_only_ = settings.value(QStringLiteral("search/lastSelectionOnly"), false).toBool();
    last_replace_display_text_ = settings.value(QStringLiteral("replace/lastText")).toString();
    last_replace_input_mode_ = static_cast<SearchInputMode>(
        settings.value(QStringLiteral("replace/lastInputMode"), static_cast<int>(SearchInputMode::Text)).toInt());
    last_replace_text_encoding_ = static_cast<SearchTextEncoding>(
        settings.value(QStringLiteral("replace/lastTextEncoding"), static_cast<int>(SearchTextEncoding::Utf8)).toInt());
    last_replace_numeric_type_ = static_cast<NumericSearchType>(
        settings.value(QStringLiteral("replace/lastNumericType"), static_cast<int>(NumericSearchType::Unsigned32)).toInt());
    last_replace_numeric_byte_order_ = static_cast<SearchByteOrder>(
        settings.value(QStringLiteral("replace/lastByteOrder"), static_cast<int>(SearchByteOrder::Little)).toInt());
    last_replace_execution_ = static_cast<SearchExecution>(
        settings.value(QStringLiteral("replace/lastExecution"), static_cast<int>(SearchExecution::FindNext)).toInt());
    last_replace_selection_only_ = settings.value(QStringLiteral("replace/lastSelectionOnly"), false).toBool();
    update_recent_files_menu();
    refresh_goto_offset_widgets();

    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());

    const int bytes_per_row = settings.value(QStringLiteral("editor/bytesPerRow"), 16).toInt();
    hex_view_->set_bytes_per_row(bytes_per_row);
    hex_view_->set_bookmark_gutter_visible(settings.value(QStringLiteral("view/showBookmarkGutter"), true).toBool());
    hex_view_->set_row_numbers_visible(settings.value(QStringLiteral("view/showRowNumbers"), false).toBool());
    hex_view_->set_offsets_visible(settings.value(QStringLiteral("view/showOffsets"), true).toBool());
    hex_view_->set_row_number_column_width(settings.value(QStringLiteral("view/rowNumberWidth"), -1).toInt());
    hex_view_->set_offset_column_width(settings.value(QStringLiteral("view/offsetWidth"), -1).toInt());
    if (show_bookmark_gutter_action_ != nullptr) {
        show_bookmark_gutter_action_->setChecked(hex_view_->bookmark_gutter_visible());
    }
    if (show_row_numbers_action_ != nullptr) {
        show_row_numbers_action_->setChecked(hex_view_->row_numbers_visible());
    }
    if (show_offsets_action_ != nullptr) {
        show_offsets_action_->setChecked(hex_view_->offsets_visible());
    }

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
    settings.setValue(QStringLiteral("gotoOffsetHistory"), goto_offset_history_);
    settings.setValue(QStringLiteral("search/history"), search_history_);
    settings.setValue(QStringLiteral("replace/history"), replace_history_);
    settings.setValue(QStringLiteral("search/lastText"), last_search_display_text_);
    settings.setValue(QStringLiteral("search/lastHexMode"), last_search_hex_mode_);
    settings.setValue(QStringLiteral("search/lastInputMode"), static_cast<int>(last_search_input_mode_));
    settings.setValue(QStringLiteral("search/lastTextEncoding"), static_cast<int>(last_search_text_encoding_));
    settings.setValue(QStringLiteral("search/lastNumericType"), static_cast<int>(last_search_numeric_type_));
    settings.setValue(QStringLiteral("search/lastByteOrder"), static_cast<int>(last_search_numeric_byte_order_));
    settings.setValue(QStringLiteral("search/lastExecution"), static_cast<int>(last_search_execution_));
    settings.setValue(QStringLiteral("search/lastSelectionOnly"), last_search_selection_only_);
    settings.setValue(QStringLiteral("replace/lastText"), last_replace_display_text_);
    settings.setValue(QStringLiteral("replace/lastInputMode"), static_cast<int>(last_replace_input_mode_));
    settings.setValue(QStringLiteral("replace/lastTextEncoding"), static_cast<int>(last_replace_text_encoding_));
    settings.setValue(QStringLiteral("replace/lastNumericType"), static_cast<int>(last_replace_numeric_type_));
    settings.setValue(QStringLiteral("replace/lastByteOrder"), static_cast<int>(last_replace_numeric_byte_order_));
    settings.setValue(QStringLiteral("replace/lastExecution"), static_cast<int>(last_replace_execution_));
    settings.setValue(QStringLiteral("replace/lastSelectionOnly"), last_replace_selection_only_);
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.setValue(QStringLiteral("editor/bytesPerRow"), hex_view_ ? hex_view_->bytes_per_row() : 16);
    settings.setValue(QStringLiteral("view/showBookmarkGutter"), hex_view_ ? hex_view_->bookmark_gutter_visible() : true);
    settings.setValue(QStringLiteral("view/showRowNumbers"), hex_view_ ? hex_view_->row_numbers_visible() : false);
    settings.setValue(QStringLiteral("view/showOffsets"), hex_view_ ? hex_view_->offsets_visible() : true);
    settings.setValue(QStringLiteral("view/rowNumberWidth"), hex_view_ ? hex_view_->row_number_column_width() : -1);
    settings.setValue(QStringLiteral("view/offsetWidth"), hex_view_ ? hex_view_->offset_column_width() : -1);
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

QString MainWindow::preferred_dialog_directory(const QString& fallback_file_name) const {
    QSettings settings;
    const QString last_directory = settings.value(QStringLiteral("session/lastDirectory")).toString();
    if (!last_directory.isEmpty()) {
        if (fallback_file_name.isEmpty()) {
            return last_directory;
        }
        return QDir(last_directory).filePath(fallback_file_name);
    }

    if (hex_view_ != nullptr && hex_view_->has_document()) {
        const QFileInfo current_info(hex_view_->document_path());
        if (current_info.exists()) {
            if (fallback_file_name.isEmpty()) {
                return current_info.absolutePath();
            }
            return current_info.absoluteDir().filePath(fallback_file_name);
        }
    }

    return fallback_file_name;
}

void MainWindow::remember_dialog_path(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    const QFileInfo info(path);
    const QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (directory.isEmpty()) {
        return;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("session/lastDirectory"), directory);
}

void MainWindow::add_goto_offset_history(const QString& text) {
    const QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        return;
    }

    goto_offset_history_.removeAll(normalized);
    goto_offset_history_.prepend(normalized);
    while (goto_offset_history_.size() > 10) {
        goto_offset_history_.removeLast();
    }
    refresh_goto_offset_widgets();
}

void MainWindow::add_search_history(const QString& text) {
    const QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        return;
    }

    search_history_.removeAll(normalized);
    search_history_.prepend(normalized);
    while (search_history_.size() > 15) {
        search_history_.removeLast();
    }
}

void MainWindow::add_replace_history(const QString& text) {
    const QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        return;
    }

    replace_history_.removeAll(normalized);
    replace_history_.prepend(normalized);
    while (replace_history_.size() > 15) {
        replace_history_.removeLast();
    }
}

void MainWindow::refresh_goto_offset_widgets() {
    if (goto_offset_edit_ == nullptr) {
        return;
    }

    const QString current_text = goto_offset_edit_->currentText();
    goto_offset_edit_->blockSignals(true);
    goto_offset_edit_->clear();
    goto_offset_edit_->addItems(goto_offset_history_);
    goto_offset_edit_->setCurrentText(current_text);
    goto_offset_edit_->blockSignals(false);
}

MainWindow::SaveBackupPolicy MainWindow::save_backup_policy() const {
    QSettings settings;
    return static_cast<SaveBackupPolicy>(settings.value(QStringLiteral("settings/saveBackupPolicy"), static_cast<int>(SaveBackupPolicy::Ask)).toInt());
}

bool MainWindow::confirm_explicit_save(const QString& title) const {
    const auto response = QMessageBox::question(
        const_cast<MainWindow*>(this),
        QStringLiteral("Confirm Save"),
        QStringLiteral("Save changes to %1?").arg(title),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    return response == QMessageBox::Yes;
}

bool MainWindow::copy_file_with_progress(
    const QString& source_path,
    const QString& target_path,
    const std::function<bool(qint64, qint64, const QString&)>& progress_callback) {
    QFile source(source_path);
    if (!source.open(QIODevice::ReadOnly)) {
        return false;
    }

    QFile target(target_path);
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    const qint64 total = source.size();
    qint64 completed = 0;
    constexpr qint64 kChunkSize = 8 * 1024 * 1024;

    if (progress_callback && !progress_callback(0, total, QStringLiteral("Creating backup"))) {
        return false;
    }

    while (!source.atEnd()) {
        const QByteArray chunk = source.read(kChunkSize);
        if (chunk.isEmpty() && source.error() != QFile::NoError) {
            return false;
        }
        if (!chunk.isEmpty() && target.write(chunk) != chunk.size()) {
            return false;
        }

        completed += chunk.size();
        if (progress_callback && !progress_callback(completed, total, QStringLiteral("Creating backup"))) {
            return false;
        }
    }

    return target.flush();
}

QString MainWindow::backup_path_for(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        return QString();
    }

    if (info.suffix().isEmpty()) {
        return path + QStringLiteral(".bak");
    }

    return info.path() + QDir::separator() + info.completeBaseName() + QStringLiteral(".") + info.suffix() + QStringLiteral(".bak");
}

bool MainWindow::prepare_backup_for_save(const QString& path, const std::function<bool(qint64, qint64, const QString&)>& progress_callback) {
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists()) {
        return true;
    }

    SaveBackupPolicy policy = save_backup_policy();
    if (policy == SaveBackupPolicy::Ask) {
        const auto response = QMessageBox::question(
            this,
            QStringLiteral("Create Backup"),
            QStringLiteral("Create a backup copy before saving?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
            QMessageBox::Yes);
        if (response == QMessageBox::Cancel) {
            return false;
        }
        policy = response == QMessageBox::Yes ? SaveBackupPolicy::Always : SaveBackupPolicy::Never;
    }

    if (policy == SaveBackupPolicy::Never) {
        return true;
    }

    const QString backup_path = backup_path_for(path);
    if (backup_path.isEmpty()) {
        return true;
    }

    if (QFile::exists(backup_path) && !QFile::remove(backup_path)) {
        QMessageBox::warning(this, QStringLiteral("Backup Failed"), QStringLiteral("Could not replace the existing backup file."));
        return false;
    }

    if (!copy_file_with_progress(path, backup_path, progress_callback)) {
        QMessageBox::warning(this, QStringLiteral("Backup Failed"), QStringLiteral("Could not create a backup copy before saving."));
        return false;
    }

    return true;
}

bool MainWindow::save_current_document(bool confirm_save, const QString* save_as_path) {
    if (hex_view_ == nullptr || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before saving."), 3000);
        return false;
    }

    const bool save_as = save_as_path != nullptr || hex_view_->is_read_only();
    QString target_path = save_as_path != nullptr ? *save_as_path : QString();
    if (save_as && target_path.isEmpty()) {
        target_path = QFileDialog::getSaveFileName(this, QStringLiteral("Save File As"), preferred_dialog_directory(QFileInfo(hex_view_->document_path()).fileName()));
        if (target_path.isEmpty()) {
            return false;
        }
    }

    const QString title = save_as ? QFileInfo(target_path).fileName() : hex_view_->document_title();
    if (confirm_save && !confirm_explicit_save(title)) {
        return false;
    }

    QProgressDialog progress_dialog(this);
    progress_dialog.setWindowTitle(QStringLiteral("Saving"));
    progress_dialog.setLabelText(QStringLiteral("Preparing save..."));
    progress_dialog.setCancelButton(nullptr);
    progress_dialog.setRange(0, 1000);
    progress_dialog.setValue(0);
    progress_dialog.setMinimumDuration(0);
    progress_dialog.setWindowModality(Qt::ApplicationModal);
    progress_dialog.setMinimumWidth(460);
    progress_dialog.setMaximumWidth(460);

    auto format_bytes = [](qint64 bytes) -> QString {
        static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 4) {
            value /= 1024.0;
            ++unit;
        }
        return QStringLiteral("%1 %2").arg(unit == 0 ? QString::number(static_cast<qint64>(value)) : QString::number(value, 'f', 1)).arg(QString::fromLatin1(units[unit]));
    };
    auto format_duration = [](qint64 seconds) -> QString {
        if (seconds < 60) {
            return QStringLiteral("%1s").arg(seconds);
        }
        if (seconds < 3600) {
            return QStringLiteral("%1m %2s").arg(seconds / 60).arg(seconds % 60);
        }
        return QStringLiteral("%1h %2m").arg(seconds / 3600).arg((seconds % 3600) / 60);
    };

    QElapsedTimer timer;
    timer.start();
    QElapsedTimer ui_timer;
    ui_timer.start();
    qint64 last_ui_completed = -1;
    const auto progress_callback = [&](qint64 completed, qint64 total, const QString& phase) -> bool {
        constexpr qint64 kUiUpdateBytes = 16LL * 1024 * 1024;
        constexpr qint64 kUiUpdateMs = 120;
        const bool force_update = completed <= 0 || completed >= total;
        if (!force_update && last_ui_completed >= 0 &&
            (completed - last_ui_completed) < kUiUpdateBytes &&
            ui_timer.elapsed() < kUiUpdateMs) {
            return true;
        }

        last_ui_completed = completed;
        ui_timer.restart();
        const int value = total > 0 ? static_cast<int>((completed * 1000) / total) : 0;
        progress_dialog.setValue(qBound(0, value, 1000));

        QString label = QStringLiteral("%1 %2 of %3").arg(phase).arg(format_bytes(completed)).arg(format_bytes(total));
        if (completed > 0 && total > completed) {
            const qint64 elapsed_ms = timer.elapsed();
            const double bytes_per_ms = static_cast<double>(completed) / qMax<qint64>(1, elapsed_ms);
            if (bytes_per_ms > 0.0) {
                const qint64 remaining_seconds = static_cast<qint64>((total - completed) / bytes_per_ms / 1000.0);
                label += QStringLiteral("\nEstimated time left: %1").arg(format_duration(qMax<qint64>(0, remaining_seconds)));
            }
        }
        progress_dialog.setLabelText(label);
        statusBar()->showMessage(label.replace(QLatin1Char('\n'), QLatin1Char(' ')));
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        return true;
    };

    QApplication::setOverrideCursor(Qt::WaitCursor);
    progress_dialog.show();
    QApplication::processEvents();
    const QString backup_source_path = save_as ? target_path : hex_view_->document_path();
    if (!prepare_backup_for_save(backup_source_path, progress_callback)) {
        QApplication::restoreOverrideCursor();
        return false;
    }
    const bool saved = save_as
        ? hex_view_->save_as_with_progress(target_path, [&](qint64 completed, qint64 total) {
            return progress_callback(completed, total, QStringLiteral("Saving"));
        })
        : hex_view_->save_with_progress([&](qint64 completed, qint64 total) {
            return progress_callback(completed, total, QStringLiteral("Saving"));
        });
    progress_dialog.setValue(1000);
    QApplication::restoreOverrideCursor();
    if (!saved) {
        statusBar()->showMessage(save_as ? QStringLiteral("Save As failed.") : QStringLiteral("Save failed."), 4000);
        return false;
    }

    if (save_as) {
        add_recent_file(target_path);
        remember_dialog_path(target_path);
        statusBar()->showMessage(QStringLiteral("Saved %1").arg(QFileInfo(target_path).fileName()), 3000);
    } else {
        remember_dialog_path(hex_view_->document_path());
        statusBar()->showMessage(QStringLiteral("Saved %1").arg(hex_view_->document_title()), 3000);
    }

    return true;
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
        return save_current_document(false);
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
    remember_dialog_path(path);
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
    show_bookmark_gutter_action_ = view_menu->addAction(QStringLiteral("Show Bookmark Gutter"));
    show_bookmark_gutter_action_->setCheckable(true);
    show_bookmark_gutter_action_->setChecked(true);
    connect(show_bookmark_gutter_action_, &QAction::toggled, this, &MainWindow::set_bookmark_gutter_visible);
    show_row_numbers_action_ = view_menu->addAction(QStringLiteral("Show Row Numbers"));
    show_row_numbers_action_->setCheckable(true);
    show_row_numbers_action_->setChecked(false);
    connect(show_row_numbers_action_, &QAction::toggled, this, &MainWindow::set_row_numbers_visible);
    show_offsets_action_ = view_menu->addAction(QStringLiteral("Show Offsets"));
    show_offsets_action_->setCheckable(true);
    show_offsets_action_->setChecked(true);
    connect(show_offsets_action_, &QAction::toggled, this, &MainWindow::set_offsets_visible);
    view_menu->addSeparator();
    decrease_width_action_ = view_menu->addAction(QStringLiteral("Narrower"));
    decrease_width_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+-")));
    connect(decrease_width_action_, &QAction::triggered, this, &MainWindow::decrease_bytes_per_row);
    increase_width_action_ = view_menu->addAction(QStringLiteral("Wider"));
    increase_width_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+=")));
    connect(increase_width_action_, &QAction::triggered, this, &MainWindow::increase_bytes_per_row);
    reset_view_action_ = view_menu->addAction(QStringLiteral("Reset View"));
    connect(reset_view_action_, &QAction::triggered, this, &MainWindow::reset_view);
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
    auto* edit_mode_control = new EditModeSegmentedControl(toolbar_);
    edit_mode_control->setFont(toolbar_->font());
    edit_mode_control->set_actions(insert_mode_action_, overwrite_mode_action_);
    edit_mode_toggle_ = edit_mode_control;
    toolbar_->addWidget(edit_mode_toggle_);
    toolbar_->addSeparator();
    toolbar_->addAction(toggle_bookmark_action_);
    toolbar_->addSeparator();
    toolbar_->addAction(find_action_);
    toolbar_->addSeparator();
    auto* goto_label = new QLabel("Goto:", toolbar_);
    goto_label->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    toolbar_->addWidget(goto_label);
    goto_offset_edit_ = new QComboBox(toolbar_);
    goto_offset_edit_->setEditable(true);
    goto_offset_edit_->setInsertPolicy(QComboBox::NoInsert);
    goto_offset_edit_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    goto_offset_edit_->setMinimumContentsLength(14);
    goto_offset_edit_->setMinimumHeight(28);
    goto_offset_edit_->setMaximumWidth(190);
    goto_offset_edit_->setStyleSheet(QStringLiteral(
        "QComboBox {"
        " min-height: 28px;"
        " padding: 2px 8px;"
        " border: 1px solid #b8b2a6;"
        " border-radius: 7px;"
        " background: #fffdf8;"
        "}"
        "QComboBox::drop-down {"
        " border: 0px;"
        " width: 22px;"
        "}"
        "QComboBox QAbstractItemView {"
        " selection-background-color: #e0d6c2;"
        "}"
    ));
    if (goto_offset_edit_->lineEdit() != nullptr) {
        goto_offset_edit_->lineEdit()->setClearButtonEnabled(true);
        goto_offset_edit_->lineEdit()->setPlaceholderText("0x0");
        goto_offset_edit_->lineEdit()->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    }
    toolbar_->addWidget(goto_offset_edit_);
    auto run_goto = [this]() {
        if (!goto_offset_edit_) {
            return;
        }

        const QString text = goto_offset_edit_->currentText().trimmed();
        quint64 offset = 0;
        if (!try_parse_offset(text, offset)) {
            statusBar()->showMessage(QStringLiteral("Invalid offset."), 2500);
            return;
        }

        if (hex_view_ && hex_view_->has_document()) {
            hex_view_->go_to_offset(static_cast<qint64>(offset));
            add_goto_offset_history(text);
            statusBar()->showMessage(QStringLiteral("Moved to offset 0x%1").arg(offset, 0, 16), 2500);
        }
    };
    if (goto_offset_edit_->lineEdit() != nullptr) {
        connect(goto_offset_edit_->lineEdit(), &QLineEdit::returnPressed, this, run_goto);
    }
    auto* goto_now = toolbar_->addAction("Go");
    connect(goto_now, &QAction::triggered, this, run_goto);
}

void MainWindow::setup_central_widget() {
    hex_view_ = new HexView(this);
    setCentralWidget(hex_view_);

    connect(hex_view_, &HexView::status_changed, this, &MainWindow::update_status);
    connect(hex_view_, &HexView::document_loaded, this, &MainWindow::update_window_title);
    connect(hex_view_, &HexView::bookmarks_changed, this, &MainWindow::update_bookmarks_view);
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
    inspector_tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    inspector_tree_->setSelectionBehavior(QAbstractItemView::SelectItems);
    inspector_tree_->setFocusPolicy(Qt::StrongFocus);
    inspector_tree_->setItemDelegate(new TreeValueDelegate(inspector_tree_));
    inspector_tree_->setStyleSheet(QStringLiteral(
        "QTreeWidget::item:selected {"
        " background: #2b6cb0;"
        " color: white;"
        "}"
    ));
    connect(inspector_tree_, &QTreeWidget::itemChanged, this, &MainWindow::handle_inspector_item_changed);
    auto* inspector_copy = new QAction(inspector_tree_);
    inspector_copy->setText(QStringLiteral("Copy Value"));
    inspector_tree_->setContextMenuPolicy(Qt::ActionsContextMenu);
    inspector_tree_->addAction(inspector_copy);
    connect(inspector_copy, &QAction::triggered, this, [this]() { copy_current_tree_value(inspector_tree_); });
    inspector_dock_->setWidget(inspector_tree_);
    inspector_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    update_inspector_view(QStringLiteral("Inspector\n\nOpen a file to inspect values."));

    bookmarks_dock_ = new QDockWidget("Bookmarks", this);
    bookmarks_tree_ = new QTreeWidget(bookmarks_dock_);
    bookmarks_tree_->setRootIsDecorated(false);
    bookmarks_tree_->setAlternatingRowColors(true);
    bookmarks_tree_->setUniformRowHeights(true);
    bookmarks_tree_->setColumnCount(4);
    bookmarks_tree_->setHeaderLabels({QStringLiteral("Color"), QStringLiteral("Offset"), QStringLiteral("Row"), QStringLiteral("Label")});
    bookmarks_tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    bookmarks_tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    bookmarks_tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    bookmarks_tree_->header()->setStretchLastSection(true);
    connect(bookmarks_tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem*, int) { activate_bookmark_item(); });
    connect(bookmarks_tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem*, int) { activate_bookmark_item(); });
    auto* rename_bookmark = new QAction(QStringLiteral("Rename Bookmark"), bookmarks_tree_);
    auto* recolor_bookmark = new QAction(QStringLiteral("Change Color"), bookmarks_tree_);
    auto* remove_bookmark = new QAction(QStringLiteral("Remove Bookmark"), bookmarks_tree_);
    bookmarks_tree_->setContextMenuPolicy(Qt::ActionsContextMenu);
    bookmarks_tree_->addAction(rename_bookmark);
    bookmarks_tree_->addAction(recolor_bookmark);
    bookmarks_tree_->addAction(remove_bookmark);
    connect(rename_bookmark, &QAction::triggered, this, &MainWindow::rename_current_bookmark);
    connect(recolor_bookmark, &QAction::triggered, this, &MainWindow::recolor_current_bookmark);
    connect(remove_bookmark, &QAction::triggered, this, &MainWindow::remove_current_bookmark);
    bookmarks_dock_->setWidget(bookmarks_tree_);
    bookmarks_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, bookmarks_dock_);
    bookmarks_dock_->hide();
    update_bookmarks_view();

    search_results_dock_ = new QDockWidget("Search Results", this);
    search_results_tabs_ = new QTabWidget(search_results_dock_);
    search_results_tabs_->setTabsClosable(true);
    search_results_tabs_->setDocumentMode(true);
    connect(search_results_tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::close_search_results_tab);
    search_results_dock_->setWidget(search_results_tabs_);
    search_results_dock_->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, search_results_dock_);
    show_search_summary(QStringLiteral("No active search."));

    analysis_dock_ = new QDockWidget("Analysis", this);
    analysis_tree_ = new QTreeWidget(analysis_dock_);
    analysis_tree_->setRootIsDecorated(false);
    analysis_tree_->setAlternatingRowColors(true);
    analysis_tree_->setUniformRowHeights(true);
    analysis_tree_->setColumnCount(2);
    analysis_tree_->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    analysis_tree_->header()->setStretchLastSection(true);
    analysis_tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    analysis_tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    analysis_tree_->setSelectionBehavior(QAbstractItemView::SelectItems);
    analysis_tree_->setFocusPolicy(Qt::StrongFocus);
    analysis_tree_->setItemDelegate(new TreeValueDelegate(analysis_tree_));
    analysis_tree_->setStyleSheet(QStringLiteral(
        "QTreeWidget::item:selected {"
        " background: #2b6cb0;"
        " color: white;"
        "}"
    ));
    auto* analysis_copy = new QAction(analysis_tree_);
    analysis_copy->setText(QStringLiteral("Copy Value"));
    analysis_tree_->setContextMenuPolicy(Qt::ActionsContextMenu);
    analysis_tree_->addAction(analysis_copy);
    connect(analysis_copy, &QAction::triggered, this, [this]() { copy_current_tree_value(analysis_tree_); });
    analysis_dock_->setWidget(analysis_tree_);
    analysis_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, analysis_dock_);
    tabifyDockWidget(inspector_dock_, analysis_dock_);
    update_analysis_view(false);
}

void MainWindow::setup_status_bar() {
    status_label_ = new QLabel("Offset: 0x0 | Selection: 0 bytes | Size: 0 bytes | Mode: Overwrite", this);
    statusBar()->addPermanentWidget(status_label_, 1);
}

void MainWindow::new_file() {
    if (!confirm_discard_changes()) {
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, "New Buffer", preferred_dialog_directory(QStringLiteral("untitled.bin")));
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
        remember_dialog_path(path);
        statusBar()->showMessage(QStringLiteral("Created %1").arg(QFileInfo(path).fileName()), 3000);
    }
}

void MainWindow::open_file() {
    const QString path = QFileDialog::getOpenFileName(this, "Open File", preferred_dialog_directory());
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
    save_current_document(true);
}

void MainWindow::save_file_as() {
    const QString requested_path;
    save_current_document(true, &requested_path);
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
    QWidget* focus = QApplication::focusWidget();
    if (analysis_tree_ != nullptr && (focus == analysis_tree_ || focus == analysis_tree_->viewport() || analysis_tree_->isAncestorOf(focus))) {
        copy_current_tree_value(analysis_tree_);
        return;
    }

    if (inspector_tree_ != nullptr && (focus == inspector_tree_ || focus == inspector_tree_->viewport() || inspector_tree_->isAncestorOf(focus))) {
        copy_current_tree_value(inspector_tree_);
        return;
    }

    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Nothing to copy."), 2000);
        return;
    }

    const QString text = hex_view_->active_pane() == HexView::ActivePane::Text
        ? hex_view_->selected_text_text()
        : hex_view_->selected_hex_text();
    if (text.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Select a range before copying."), 2500);
        return;
    }

    auto* clipboard = QApplication::clipboard();
    clipboard->setText(text);
    statusBar()->showMessage(
        hex_view_->active_pane() == HexView::ActivePane::Text
            ? QStringLiteral("Copied selected text.")
            : QStringLiteral("Copied selected bytes as hex."),
        2500);
}

void MainWindow::copy_as_hex() {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Nothing to copy."), 2000);
        return;
    }

    const QString text = hex_view_->selected_hex_text();
    if (text.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Select a range before copying."), 2500);
        return;
    }

    QApplication::clipboard()->setText(text);
    statusBar()->showMessage(QStringLiteral("Copied selected bytes as hex."), 2500);
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

    statusBar()->showMessage(QStringLiteral("Computing hashes..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    update_analysis_view(selection_only);
    QApplication::restoreOverrideCursor();
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
    auto* backup_policy = new QComboBox(&dialog);
    backup_policy->addItem(QStringLiteral("Ask"), static_cast<int>(SaveBackupPolicy::Ask));
    backup_policy->addItem(QStringLiteral("Always"), static_cast<int>(SaveBackupPolicy::Always));
    backup_policy->addItem(QStringLiteral("Never"), static_cast<int>(SaveBackupPolicy::Never));
    backup_policy->setCurrentIndex(backup_policy->findData(static_cast<int>(save_backup_policy())));

    layout->addRow(QStringLiteral("Bytes per row"), row_width);
    layout->addRow(QString(), restore_session);
    layout->addRow(QStringLiteral("Backup file"), backup_policy);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    set_bytes_per_row(row_width->currentData().toInt());
    settings.setValue(QStringLiteral("settings/restoreLastSession"), restore_session->isChecked());
    settings.setValue(QStringLiteral("settings/saveBackupPolicy"), backup_policy->currentData().toInt());
    statusBar()->showMessage(QStringLiteral("Settings updated."), 2500);
}

void MainWindow::show_about() {
    QMessageBox::about(
        this,
        QStringLiteral("About Hex Master"),
        QStringLiteral(
            "<b>Hex Master</b><br>"
            "Version %1<br><br>"
            "Created by Majid Siddiqui<br>"
            "me@majidarif.com<br><br>"
            "2026<br><br>"
            "A desktop hex editor built with Qt and Rust.")
            .arg(QApplication::applicationVersion()));
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
    SearchExecution* execution,
    bool* selection_only,
    bool default_selection_only,
    QString* display_text) {
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(520, 0);

    auto* root = new QVBoxLayout(&dialog);
    auto* pattern_group = new QGroupBox(QStringLiteral("Pattern"), &dialog);
    auto* pattern_form = new QFormLayout(pattern_group);
    root->addWidget(pattern_group);

    auto* text_input = new QComboBox(pattern_group);
    text_input->setEditable(true);
    text_input->setInsertPolicy(QComboBox::NoInsert);
    text_input->addItems(search_history_);
    text_input->setCurrentText(last_search_display_text_);
    if (text_input->lineEdit() != nullptr) {
        text_input->lineEdit()->setClearButtonEnabled(true);
    }

    auto* mode_combo = new QComboBox(pattern_group);
    mode_combo->addItem(QStringLiteral("Text"), static_cast<int>(SearchInputMode::Text));
    mode_combo->addItem(QStringLiteral("Hex Bytes"), static_cast<int>(SearchInputMode::Hex));
    mode_combo->addItem(QStringLiteral("Typed Value"), static_cast<int>(SearchInputMode::Value));
    mode_combo->setCurrentIndex(mode_combo->findData(static_cast<int>(last_search_input_mode_)));

    auto* options_stack = new QStackedWidget(pattern_group);

    auto* text_page = new QWidget(options_stack);
    auto* text_form = new QFormLayout(text_page);
    auto* encoding_combo = new QComboBox(text_page);
    encoding_combo->addItem(encoding_label(SearchTextEncoding::Ascii), static_cast<int>(SearchTextEncoding::Ascii));
    encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf8), static_cast<int>(SearchTextEncoding::Utf8));
    encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf16Le), static_cast<int>(SearchTextEncoding::Utf16Le));
    encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf16Be), static_cast<int>(SearchTextEncoding::Utf16Be));
    encoding_combo->setCurrentIndex(encoding_combo->findData(static_cast<int>(last_search_text_encoding_)));
    text_form->addRow(QStringLiteral("Text encoding"), encoding_combo);
    options_stack->addWidget(text_page);

    auto* hex_page = new QWidget(options_stack);
    auto* hex_form = new QFormLayout(hex_page);
    auto* hex_hint = new QLabel(QStringLiteral("Search for raw byte sequences like `DE AD BE EF` or `0xDEADBEEF`."), hex_page);
    hex_hint->setWordWrap(true);
    hex_form->addRow(QStringLiteral("Hex mode"), hex_hint);
    options_stack->addWidget(hex_page);

    auto* value_page = new QWidget(options_stack);
    auto* value_form = new QFormLayout(value_page);
    auto* numeric_type_combo = new QComboBox(value_page);
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

    auto* endian_combo = new QComboBox(value_page);
    endian_combo->addItem(QStringLiteral("Little Endian"), static_cast<int>(SearchByteOrder::Little));
    endian_combo->addItem(QStringLiteral("Big Endian"), static_cast<int>(SearchByteOrder::Big));
    endian_combo->setCurrentIndex(endian_combo->findData(static_cast<int>(last_search_numeric_byte_order_)));
    value_form->addRow(QStringLiteral("Value type"), numeric_type_combo);
    value_form->addRow(QStringLiteral("Byte order"), endian_combo);
    options_stack->addWidget(value_page);

    pattern_form->addRow(label, text_input);
    pattern_form->addRow(QStringLiteral("Interpret as"), mode_combo);
    pattern_form->addRow(QStringLiteral("Mode options"), options_stack);

    QComboBox* action_combo = nullptr;
    auto* options_group = new QGroupBox(QStringLiteral("Search Options"), &dialog);
    auto* options_form = new QFormLayout(options_group);
    root->addWidget(options_group);

    if (execution != nullptr) {
        action_combo = new QComboBox(options_group);
        action_combo->addItem(QStringLiteral("Find Next"), static_cast<int>(SearchExecution::FindNext));
        action_combo->addItem(QStringLiteral("Find All"), static_cast<int>(SearchExecution::FindAll));
        action_combo->setCurrentIndex(action_combo->findData(static_cast<int>(last_search_execution_)));
        options_form->addRow(QStringLiteral("Action"), action_combo);
    }

    QCheckBox* selection_only_check = nullptr;
    if (selection_only != nullptr && hex_view_ != nullptr && hex_view_->has_selection()) {
        selection_only_check = new QCheckBox(QStringLiteral("Search in selection only"), options_group);
        selection_only_check->setChecked(default_selection_only || last_search_selection_only_);
        options_form->addRow(QString(), selection_only_check);
    }
    if (action_combo == nullptr && selection_only_check == nullptr) {
        options_group->hide();
    }

    auto update_mode_page = [=]() {
        const SearchInputMode mode = static_cast<SearchInputMode>(mode_combo->currentData().toInt());
        switch (mode) {
        case SearchInputMode::Text:
            options_stack->setCurrentWidget(text_page);
            break;
        case SearchInputMode::Hex:
            options_stack->setCurrentWidget(hex_page);
            break;
        case SearchInputMode::Value:
            options_stack->setCurrentWidget(value_page);
            break;
        }
    };
    connect(mode_combo, &QComboBox::currentIndexChanged, &dialog, update_mode_page);
    update_mode_page();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const QString input = text_input->currentText().trimmed();
    if (input.isEmpty()) {
        return false;
    }

    const SearchInputMode selected_mode = static_cast<SearchInputMode>(mode_combo->currentData().toInt());
    hex_mode = selected_mode != SearchInputMode::Text;
    QByteArray parsed;
    if (selected_mode == SearchInputMode::Hex) {
        last_search_input_mode_ = SearchInputMode::Hex;
        if (!try_parse_hex_bytes(input, parsed)) {
            statusBar()->showMessage(QStringLiteral("Invalid hex byte sequence."), 3000);
            return false;
        }
    } else if (selected_mode == SearchInputMode::Text) {
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
    add_search_history(input);
    if (execution != nullptr && action_combo != nullptr) {
        *execution = static_cast<SearchExecution>(action_combo->currentData().toInt());
        last_search_execution_ = *execution;
    }
    if (selection_only != nullptr) {
        *selection_only = selection_only_check != nullptr && selection_only_check->isChecked();
        last_search_selection_only_ = *selection_only;
    }
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

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Go To Offset"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* prompt = new QLabel(QStringLiteral("Offset"), &dialog);
    prompt->setStyleSheet(QStringLiteral("font-weight: 600;"));
    layout->addWidget(prompt);
    auto* combo = new QComboBox(&dialog);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->addItems(goto_offset_history_);
    combo->setCurrentText(goto_offset_edit_ ? goto_offset_edit_->currentText() : QStringLiteral("0x0"));
    combo->setMinimumWidth(320);
    combo->setMinimumHeight(32);
    if (combo->lineEdit() != nullptr) {
        combo->lineEdit()->setClearButtonEnabled(true);
        combo->lineEdit()->setPlaceholderText(QStringLiteral("0x0"));
    }
    layout->addWidget(combo);

    auto* hint = new QLabel(QStringLiteral("Enter a decimal offset or a hex value prefixed with 0x."), &dialog);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #5a6675;"));
    layout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString text = combo->currentText().trimmed();
    if (text.isEmpty()) {
        return;
    }

    quint64 offset = 0;
    if (!try_parse_offset(text, offset)) {
        statusBar()->showMessage(QStringLiteral("Invalid offset: %1").arg(text.trimmed()), 4000);
        return;
    }

    if (goto_offset_edit_ != nullptr) {
        goto_offset_edit_->setCurrentText(text);
    }
    add_goto_offset_history(text);
    hex_view_->go_to_offset(static_cast<qint64>(offset));
    statusBar()->showMessage(QStringLiteral("Moved to offset 0x%1").arg(offset, 0, 16), 3000);
}

void MainWindow::set_bytes_per_row(int bytes_per_row) {
    hex_view_->set_bytes_per_row(bytes_per_row);
    if (row_width_group_ != nullptr) {
        for (QAction* action : row_width_group_->actions()) {
            if (action != nullptr && action->data().toInt() == bytes_per_row) {
                action->setChecked(true);
                break;
            }
        }
    }
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
    SearchExecution execution = SearchExecution::FindNext;
    bool selection_only = false;
    if (!prompt_for_search_pattern(
            QStringLiteral("Find"),
            QStringLiteral("Pattern"),
            pattern,
            hex_mode,
            &execution,
            &selection_only,
            false)) {
        return;
    }

    last_search_hex_mode_ = hex_mode;
    last_search_pattern_ = pattern;
    if (execution == SearchExecution::FindAll) {
        run_find_all(selection_only);
    } else {
        run_search(true, false, selection_only);
    }
}

void MainWindow::find_in_selection() {
    if (!hex_view_ || !hex_view_->has_document() || !hex_view_->has_selection()) {
        statusBar()->showMessage(QStringLiteral("Select a range before searching in selection."), 2500);
        return;
    }

    QByteArray pattern;
    bool hex_mode = false;
    SearchExecution execution = SearchExecution::FindNext;
    bool selection_only = true;
    if (!prompt_for_search_pattern(
            QStringLiteral("Find"),
            QStringLiteral("Pattern"),
            pattern,
            hex_mode,
            &execution,
            &selection_only,
            true)) {
        return;
    }

    last_search_hex_mode_ = hex_mode;
    last_search_pattern_ = pattern;
    if (execution == SearchExecution::FindAll) {
        run_find_all(selection_only);
    } else {
        run_search(true, false, selection_only);
    }
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

    if (hex_view_->is_read_only()) {
        statusBar()->showMessage(QStringLiteral("Document is read-only."), 2500);
        return;
    }

    QByteArray before;
    QByteArray after;
    bool hex_mode = false;
    SearchExecution execution = SearchExecution::FindNext;
    bool selection_only = false;
    if (!prompt_for_replace_operation(before, after, hex_mode, execution, selection_only)) {
        return;
    }

    last_search_pattern_ = before;
    last_search_hex_mode_ = hex_mode;

    if (execution == SearchExecution::FindAll) {
        QProgressDialog progress_dialog(this);
        progress_dialog.setWindowTitle(QStringLiteral("Replace All"));
        progress_dialog.setLabelText(QStringLiteral("Replacing..."));
        progress_dialog.setCancelButtonText(QStringLiteral("Cancel"));
        progress_dialog.setRange(0, 1000);
        progress_dialog.setValue(0);
        progress_dialog.setMinimumDuration(0);
        progress_dialog.setWindowModality(Qt::ApplicationModal);
        progress_dialog.setMinimumWidth(460);
        progress_dialog.setMaximumWidth(460);

        auto format_bytes = [](qint64 bytes) -> QString {
            static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
            double value = static_cast<double>(bytes);
            int unit = 0;
            while (value >= 1024.0 && unit < 4) {
                value /= 1024.0;
                ++unit;
            }
            return QStringLiteral("%1 %2").arg(unit == 0 ? QString::number(static_cast<qint64>(value)) : QString::number(value, 'f', 1)).arg(QString::fromLatin1(units[unit]));
        };

        QElapsedTimer ui_timer;
        ui_timer.start();
        qint64 last_ui_completed = -1;
        bool canceled = false;
        const auto progress_callback = [&](qint64 completed, qint64 total) -> bool {
            constexpr qint64 kUiUpdateBytes = 16LL * 1024 * 1024;
            constexpr qint64 kUiUpdateMs = 120;
            const bool force_update = completed <= 0 || completed >= total;
            if (!force_update && last_ui_completed >= 0 &&
                (completed - last_ui_completed) < kUiUpdateBytes &&
                ui_timer.elapsed() < kUiUpdateMs) {
                return true;
            }

            last_ui_completed = completed;
            ui_timer.restart();
            progress_dialog.setValue(total > 0 ? qBound(0, static_cast<int>((completed * 1000) / total), 1000) : 0);
            const QString label = total > 0
                ? QStringLiteral("Replacing... scanned %1 of %2").arg(format_bytes(completed)).arg(format_bytes(total))
                : QStringLiteral("Replacing...");
            progress_dialog.setLabelText(label);
            statusBar()->showMessage(label);
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            if (progress_dialog.wasCanceled()) {
                canceled = true;
                return false;
            }
            return true;
        };

        QApplication::setOverrideCursor(Qt::WaitCursor);
        progress_dialog.show();
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        bool replace_canceled = false;
        QVector<qint64> replaced_offsets;
        const qint64 replaced = hex_view_->replace_all(before, after, selection_only, &replaced_offsets, &replace_canceled, progress_callback);
        progress_dialog.setValue(1000);
        QApplication::restoreOverrideCursor();
        if (canceled || replace_canceled) {
            if (replaced > 0) {
                show_search_matches(
                    QStringLiteral("Replaced %1 occurrence(s) before canceling%2.")
                        .arg(replaced)
                        .arg(selection_only ? QStringLiteral(" in the current selection") : QString()),
                    replaced_offsets,
                    QStringLiteral("Replace"));
                statusBar()->showMessage(QStringLiteral("Replace canceled after %1 occurrence(s).").arg(replaced), 3000);
            } else {
                statusBar()->showMessage(QStringLiteral("Replace canceled."), 2500);
            }
            return;
        }
        if (replaced <= 0) {
            statusBar()->showMessage(QStringLiteral("No matches replaced."), 2500);
            return;
        }

        show_search_matches(
            QStringLiteral("Replaced %1 occurrence(s)%2.")
                .arg(replaced)
                .arg(selection_only ? QStringLiteral(" in the current selection") : QString()),
            replaced_offsets,
            QStringLiteral("Replace"));
        statusBar()->showMessage(QStringLiteral("Replaced %1 occurrence(s).").arg(replaced), 3000);
        return;
    }

    QProgressDialog progress_dialog(this);
    progress_dialog.setWindowTitle(QStringLiteral("Replace"));
    progress_dialog.setLabelText(QStringLiteral("Searching for next match..."));
    progress_dialog.setCancelButtonText(QStringLiteral("Cancel"));
    progress_dialog.setRange(0, 1000);
    progress_dialog.setValue(0);
    progress_dialog.setMinimumDuration(0);
    progress_dialog.setWindowModality(Qt::ApplicationModal);
    progress_dialog.setMinimumWidth(460);
    progress_dialog.setMaximumWidth(460);

    auto format_bytes = [](qint64 bytes) -> QString {
        static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 4) {
            value /= 1024.0;
            ++unit;
        }
        return QStringLiteral("%1 %2").arg(unit == 0 ? QString::number(static_cast<qint64>(value)) : QString::number(value, 'f', 1)).arg(QString::fromLatin1(units[unit]));
    };
    QElapsedTimer ui_timer;
    ui_timer.start();
    qint64 last_ui_completed = -1;
    bool canceled = false;
    const auto progress_callback = [&](qint64 completed, qint64 total) -> bool {
        constexpr qint64 kUiUpdateBytes = 16LL * 1024 * 1024;
        constexpr qint64 kUiUpdateMs = 120;
        const bool force_update = completed <= 0 || completed >= total;
        if (!force_update && last_ui_completed >= 0 &&
            (completed - last_ui_completed) < kUiUpdateBytes &&
            ui_timer.elapsed() < kUiUpdateMs) {
            return true;
        }
        last_ui_completed = completed;
        ui_timer.restart();
        progress_dialog.setValue(total > 0 ? qBound(0, static_cast<int>((completed * 1000) / total), 1000) : 0);
        const QString label = total > 0
            ? QStringLiteral("Searching for next match... %1 of %2").arg(format_bytes(completed)).arg(format_bytes(total))
            : QStringLiteral("Searching for next match...");
        progress_dialog.setLabelText(label);
        statusBar()->showMessage(label);
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if (progress_dialog.wasCanceled()) {
            canceled = true;
            return false;
        }
        return true;
    };

    QApplication::setOverrideCursor(Qt::WaitCursor);
    progress_dialog.show();
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    qint64 found_offset = -1;
    bool search_canceled = false;
    const bool found = selection_only
        ? hex_view_->find_pattern_in_selection(before, true, true, &found_offset, &search_canceled, progress_callback)
        : hex_view_->find_pattern(before, true, true, &found_offset, &search_canceled, progress_callback);
    if (!found) {
        const bool fallback_found = selection_only
            ? hex_view_->find_pattern_in_selection(before, true, false, &found_offset, &search_canceled, progress_callback)
            : hex_view_->find_pattern(before, true, false, &found_offset, &search_canceled, progress_callback);
        progress_dialog.setValue(1000);
        QApplication::restoreOverrideCursor();
        if (canceled || search_canceled) {
            statusBar()->showMessage(QStringLiteral("Replace canceled."), 2500);
            return;
        }
        if (!fallback_found) {
            statusBar()->showMessage(QStringLiteral("No match available to replace."), 3000);
            return;
        }
    } else {
        progress_dialog.setValue(1000);
        QApplication::restoreOverrideCursor();
    }

    if (!hex_view_->replace_range(found_offset, before, after)) {
        statusBar()->showMessage(QStringLiteral("Replace failed."), 3000);
        return;
    }

    show_search_matches(
        QStringLiteral("Replaced %1 bytes at 0x%2")
            .arg(after.size())
            .arg(found_offset, 8, 16, QChar(u'0'))
            .toUpper(),
        QVector<qint64>{found_offset},
        QStringLiteral("Replace"));
    statusBar()->showMessage(QStringLiteral("Replaced match at 0x%1").arg(found_offset, 0, 16), 2500);
}

void MainWindow::replace_all() {
    replace();
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

void MainWindow::set_bookmark_gutter_visible(bool visible) {
    if (hex_view_ != nullptr) {
        hex_view_->set_bookmark_gutter_visible(visible);
    }
}

void MainWindow::set_row_numbers_visible(bool visible) {
    if (hex_view_ != nullptr) {
        hex_view_->set_row_numbers_visible(visible);
    }
}

void MainWindow::set_offsets_visible(bool visible) {
    if (hex_view_ != nullptr) {
        hex_view_->set_offsets_visible(visible);
    }
}

void MainWindow::increase_bytes_per_row() {
    if (hex_view_ == nullptr) {
        return;
    }
    const QList<int> widths = {8, 16, 24, 32, 48};
    const int current = static_cast<int>(hex_view_->bytes_per_row());
    for (const int width : widths) {
        if (width > current) {
            set_bytes_per_row(width);
            return;
        }
    }
}

void MainWindow::decrease_bytes_per_row() {
    if (hex_view_ == nullptr) {
        return;
    }
    const QList<int> widths = {8, 16, 24, 32, 48};
    const int current = static_cast<int>(hex_view_->bytes_per_row());
    for (auto it = widths.crbegin(); it != widths.crend(); ++it) {
        if (*it < current) {
            set_bytes_per_row(*it);
            return;
        }
    }
}

void MainWindow::reset_view() {
    if (hex_view_ == nullptr) {
        return;
    }

    hex_view_->reset_view_layout();
    if (bookmarks_dock_ != nullptr) {
        bookmarks_dock_->hide();
    }
    if (show_bookmark_gutter_action_ != nullptr) {
        show_bookmark_gutter_action_->setChecked(true);
    } else {
        hex_view_->set_bookmark_gutter_visible(true);
    }
    if (show_row_numbers_action_ != nullptr) {
        show_row_numbers_action_->setChecked(false);
    } else {
        hex_view_->set_row_numbers_visible(false);
    }
    if (show_offsets_action_ != nullptr) {
        show_offsets_action_->setChecked(true);
    } else {
        hex_view_->set_offsets_visible(true);
    }
    set_bytes_per_row(16);
    statusBar()->showMessage(QStringLiteral("View reset to defaults."), 2500);
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

    updating_inspector_tree_ = true;
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
                auto* item = new QTreeWidgetItem(current_group, QStringList{row.field, row.value});
                item->setData(0, Qt::UserRole, row.section);
                item->setData(1, Qt::UserRole, row.field);
                const bool editable =
                    (row.section == QStringLiteral("Overview") && (row.field == QStringLiteral("Bytes") || row.field == QStringLiteral("ASCII"))) ||
                    row.section == QStringLiteral("Integers") ||
                    (row.section == QStringLiteral("Network") && row.field == QStringLiteral("IPv4")) ||
                    row.section == QStringLiteral("Floating Point");
                if (editable) {
                    item->setFlags(item->flags() | Qt::ItemIsEditable);
                }
            }
        }
    }

    if (inspector_tree_->topLevelItemCount() == 0) {
        auto* item = new QTreeWidgetItem(inspector_tree_, QStringList{QStringLiteral("Inspector"), text.trimmed()});
        item->setFirstColumnSpanned(text.trimmed().isEmpty() || text.trimmed() == QStringLiteral("Inspector"));
    }

    inspector_tree_->expandAll();
    updating_inspector_tree_ = false;
}

void MainWindow::update_bookmarks_view() {
    if (bookmarks_tree_ == nullptr) {
        return;
    }

    bookmarks_tree_->clear();
    if (hex_view_ == nullptr || !hex_view_->has_document()) {
        auto* item = new QTreeWidgetItem(bookmarks_tree_, QStringList{QString(), QStringLiteral("Open a file to add bookmarks."), QString(), QString()});
        item->setFirstColumnSpanned(false);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setData(0, Qt::UserRole, -1);
        return;
    }

    const QVector<HexView::BookmarkRow> rows = hex_view_->bookmark_rows();
    if (rows.isEmpty()) {
        auto* item = new QTreeWidgetItem(bookmarks_tree_, QStringList{QString(), QStringLiteral("No bookmarks yet."), QString(), QStringLiteral("Use Ctrl+B at the caret.")});
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        item->setData(0, Qt::UserRole, -1);
        return;
    }

    for (const HexView::BookmarkRow& row : rows) {
        auto* item = new QTreeWidgetItem(bookmarks_tree_, QStringList{
            QStringLiteral("  "),
            QStringLiteral("0x%1").arg(row.offset, 8, 16, QChar(u'0')).toUpper(),
            QString::number(row.row),
            row.label
        });
        item->setData(0, Qt::UserRole, row.offset);
        item->setBackground(0, QBrush(row.color));
        item->setToolTip(0, row.color.name());
        item->setToolTip(1, item->text(1));
    }
}

void MainWindow::update_analysis_view(bool selection_only) {
    if (analysis_tree_ == nullptr) {
        return;
    }

    analysis_tree_->clear();
    if (hex_view_ == nullptr) {
        return;
    }

    QString current_section;
    QTreeWidgetItem* current_group = nullptr;
    const QVector<HexView::AnalysisRow> rows = hex_view_->analysis_rows(selection_only);
    for (const HexView::AnalysisRow& row : rows) {
        if (row.section != current_section) {
            current_section = row.section;
            current_group = new QTreeWidgetItem(analysis_tree_, QStringList{current_section, QString()});
            current_group->setFirstColumnSpanned(true);
            current_group->setFlags(current_group->flags() & ~Qt::ItemIsSelectable);
        }

        if (current_group == nullptr) {
            continue;
        }

        auto* item = new QTreeWidgetItem(current_group, QStringList{row.field, row.value});
        item->setData(0, Qt::UserRole, row.section);
        item->setData(1, Qt::UserRole, row.field);
    }

    analysis_tree_->expandAll();
}

void MainWindow::handle_inspector_item_changed(QTreeWidgetItem* item, int column) {
    if (updating_inspector_tree_ || item == nullptr || column != 1 || hex_view_ == nullptr) {
        return;
    }

    const QString section = item->data(0, Qt::UserRole).toString();
    const QString field = item->data(1, Qt::UserRole).toString();
    if (section.isEmpty() || field.isEmpty()) {
        return;
    }

    QString error_message;
    if (!hex_view_->apply_inspector_edit(section, field, item->text(1), &error_message)) {
        statusBar()->showMessage(error_message.isEmpty() ? QStringLiteral("Inspector edit failed.") : error_message, 4000);
        update_inspector_view(QString());
        return;
    }

    statusBar()->showMessage(QStringLiteral("Updated %1.").arg(field), 2500);
}

void MainWindow::activate_bookmark_item() {
    if (bookmarks_tree_ == nullptr || hex_view_ == nullptr) {
        return;
    }
    QTreeWidgetItem* item = bookmarks_tree_->currentItem();
    if (item == nullptr) {
        return;
    }
    const qint64 offset = item->data(0, Qt::UserRole).toLongLong();
    if (offset < 0) {
        return;
    }
    hex_view_->go_to_offset(offset);
    statusBar()->showMessage(QStringLiteral("Moved to bookmark at 0x%1").arg(offset, 0, 16), 2000);
}

void MainWindow::rename_current_bookmark() {
    if (bookmarks_tree_ == nullptr || hex_view_ == nullptr) {
        return;
    }
    QTreeWidgetItem* item = bookmarks_tree_->currentItem();
    if (item == nullptr) {
        return;
    }
    const qint64 offset = item->data(0, Qt::UserRole).toLongLong();
    if (offset < 0) {
        return;
    }
    bool ok = false;
    const QString label = QInputDialog::getText(this, QStringLiteral("Rename Bookmark"), QStringLiteral("Label"), QLineEdit::Normal, item->text(3), &ok);
    if (!ok) {
        return;
    }
    if (hex_view_->set_bookmark_label(offset, label)) {
        statusBar()->showMessage(QStringLiteral("Bookmark updated."), 2000);
    }
}

void MainWindow::recolor_current_bookmark() {
    if (bookmarks_tree_ == nullptr || hex_view_ == nullptr) {
        return;
    }
    QTreeWidgetItem* item = bookmarks_tree_->currentItem();
    if (item == nullptr) {
        return;
    }
    const qint64 offset = item->data(0, Qt::UserRole).toLongLong();
    if (offset < 0) {
        return;
    }
    const QColor current = item->background(0).color();
    const QColor chosen = QColorDialog::getColor(current, this, QStringLiteral("Choose Bookmark Color"));
    if (!chosen.isValid()) {
        return;
    }
    if (hex_view_->set_bookmark_color(offset, chosen)) {
        statusBar()->showMessage(QStringLiteral("Bookmark color updated."), 2000);
    }
}

void MainWindow::remove_current_bookmark() {
    if (bookmarks_tree_ == nullptr || hex_view_ == nullptr) {
        return;
    }
    QTreeWidgetItem* item = bookmarks_tree_->currentItem();
    if (item == nullptr) {
        return;
    }
    const qint64 offset = item->data(0, Qt::UserRole).toLongLong();
    if (offset < 0) {
        return;
    }
    if (hex_view_->remove_bookmark(offset)) {
        statusBar()->showMessage(QStringLiteral("Bookmark removed."), 2000);
    }
}

void MainWindow::copy_current_tree_value(QTreeWidget* tree) {
    if (tree == nullptr) {
        return;
    }

    QTreeWidgetItem* item = tree->currentItem();
    if (item == nullptr && !tree->selectedItems().isEmpty()) {
        item = tree->selectedItems().first();
    }
    if (item == nullptr) {
        return;
    }

    QString value = item->text(1);
    if (value.isEmpty()) {
        const int current_column = tree->currentColumn();
        value = item->text(current_column >= 0 ? current_column : 0);
    }
    if (value.isEmpty()) {
        return;
    }

    QApplication::clipboard()->setText(value);
    statusBar()->showMessage(QStringLiteral("Copied value to clipboard."), 2000);
}

void MainWindow::close_search_results_tab(int index) {
    if (search_results_tabs_ == nullptr || index < 0 || index >= search_results_tabs_->count()) {
        return;
    }

    QWidget* page = search_results_tabs_->widget(index);
    search_results_tabs_->removeTab(index);
    delete page;
    if (search_results_tabs_->count() == 0) {
        show_search_summary(QStringLiteral("No active search."), QStringLiteral("Summary"));
    }
}

void MainWindow::show_search_summary(const QString& summary, const QString& tab_title) {
    if (search_results_tabs_ == nullptr) {
        return;
    }

    auto* tree = new QTreeWidget(search_results_tabs_);
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setUniformRowHeights(true);
    tree->setColumnCount(3);
    tree->setHeaderLabels({QStringLiteral("Offset"), QStringLiteral("Match"), QStringLiteral("Preview")});
    tree->header()->setStretchLastSection(true);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem*, int) { activate_search_result_item(); });
    connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem*, int) { activate_search_result_item(); });

    auto* item = new QTreeWidgetItem(tree, QStringList{QStringLiteral("Summary"), summary, QString()});
    item->setFirstColumnSpanned(false);

    const QString title = tab_title.isEmpty() ? QStringLiteral("Search %1").arg(++search_results_counter_) : tab_title;
    const int index = search_results_tabs_->addTab(tree, title);
    search_results_tabs_->setCurrentIndex(index);
}

void MainWindow::show_search_matches(const QString& summary, const QVector<qint64>& matches, const QString& tab_title) {
    if (search_results_tabs_ == nullptr) {
        return;
    }

    auto* tree = new QTreeWidget(search_results_tabs_);
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setUniformRowHeights(true);
    tree->setColumnCount(3);
    tree->setHeaderLabels({QStringLiteral("Offset"), QStringLiteral("Match"), QStringLiteral("Preview")});
    tree->header()->setStretchLastSection(true);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem*, int) { activate_search_result_item(); });
    connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem*, int) { activate_search_result_item(); });

    auto* summary_item = new QTreeWidgetItem(tree, QStringList{QStringLiteral("Summary"), summary, QString()});
    summary_item->setFirstColumnSpanned(false);

    auto* results_root = new QTreeWidgetItem(
        tree,
        QStringList{QStringLiteral("Results"), QStringLiteral("%1 match(es)").arg(matches.size()), QString()});
    results_root->setExpanded(true);

    const int max_results = 512;
    const int shown = qMin(matches.size(), max_results);
    for (int index = 0; index < shown; ++index) {
        const qint64 offset = matches.at(index);
        const QByteArray preview_bytes = hex_view_ != nullptr ? hex_view_->read_bytes(offset, qMax(16, last_search_pattern_.size())) : QByteArray();
        QString preview;
        for (int byte_index = 0; byte_index < preview_bytes.size(); ++byte_index) {
            if (byte_index > 0) {
                preview += QLatin1Char(' ');
            }
            preview += QStringLiteral("%1")
                           .arg(static_cast<quint8>(preview_bytes.at(byte_index)), 2, 16, QChar(u'0'))
                           .toUpper();
        }
        auto* item = new QTreeWidgetItem(
            results_root,
            QStringList{
                QStringLiteral("0x%1").arg(offset, 8, 16, QChar(u'0')).toUpper(),
                QStringLiteral("Match %1").arg(index + 1),
                preview});
        item->setData(0, Qt::UserRole, offset);
        item->setData(1, Qt::UserRole, last_search_pattern_.size());
    }

    if (matches.size() > max_results) {
        new QTreeWidgetItem(
            results_root,
            QStringList{
                QStringLiteral("More"),
                QStringLiteral("Showing first %1 matches").arg(max_results),
                QString()});
    }

    tree->expandAll();
    const QString title = tab_title.isEmpty() ? QStringLiteral("Search %1").arg(++search_results_counter_) : tab_title;
    const int index = search_results_tabs_->addTab(tree, title);
    search_results_tabs_->setCurrentIndex(index);
}

void MainWindow::activate_search_result_item() {
    if (search_results_tabs_ == nullptr || hex_view_ == nullptr || !hex_view_->has_document()) {
        return;
    }

    auto* tree = qobject_cast<QTreeWidget*>(search_results_tabs_->currentWidget());
    if (tree == nullptr) {
        return;
    }

    QTreeWidgetItem* item = tree->currentItem();
    if (item == nullptr) {
        return;
    }

    const QVariant offset_value = item->data(0, Qt::UserRole);
    if (!offset_value.isValid()) {
        return;
    }

    const qint64 offset = offset_value.toLongLong();
    const qint64 match_length = item->data(1, Qt::UserRole).toLongLong();
    if (match_length > 0) {
        hex_view_->select_range(offset, match_length);
    } else {
        hex_view_->go_to_offset(offset);
    }
    statusBar()->showMessage(QStringLiteral("Moved to search result at 0x%1").arg(offset, 0, 16), 2500);
}

bool MainWindow::prompt_for_replace_operation(
    QByteArray& before,
    QByteArray& after,
    bool& find_hex_mode,
    SearchExecution& execution,
    bool& selection_only) {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Replace"));
    dialog.resize(560, 0);

    auto* root = new QVBoxLayout(&dialog);

    auto build_mode_section = [&](QWidget* parent,
                                  const QString& title,
                                  const QString& label,
                                  const QString& initial_text,
                                  SearchInputMode initial_mode,
                                  SearchTextEncoding initial_encoding,
                                  NumericSearchType initial_numeric_type,
                                  SearchByteOrder initial_byte_order,
                                  const QStringList& history,
                                  QComboBox*& text_input,
                                  QComboBox*& mode_combo,
                                  QComboBox*& encoding_combo,
                                  QComboBox*& numeric_type_combo,
                                  QComboBox*& byte_order_combo) {
        auto* group = new QGroupBox(title, parent);
        auto* form = new QFormLayout(group);

        text_input = new QComboBox(group);
        text_input->setEditable(true);
        text_input->setInsertPolicy(QComboBox::NoInsert);
        text_input->addItems(history);
        text_input->setCurrentText(initial_text);
        if (text_input->lineEdit() != nullptr) {
            text_input->lineEdit()->setClearButtonEnabled(true);
        }

        mode_combo = new QComboBox(group);
        mode_combo->addItem(QStringLiteral("Text"), static_cast<int>(SearchInputMode::Text));
        mode_combo->addItem(QStringLiteral("Hex Bytes"), static_cast<int>(SearchInputMode::Hex));
        mode_combo->addItem(QStringLiteral("Typed Value"), static_cast<int>(SearchInputMode::Value));
        mode_combo->setCurrentIndex(mode_combo->findData(static_cast<int>(initial_mode)));

        auto* options_stack = new QStackedWidget(group);

        auto* text_page = new QWidget(options_stack);
        auto* text_form = new QFormLayout(text_page);
        encoding_combo = new QComboBox(text_page);
        encoding_combo->addItem(encoding_label(SearchTextEncoding::Ascii), static_cast<int>(SearchTextEncoding::Ascii));
        encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf8), static_cast<int>(SearchTextEncoding::Utf8));
        encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf16Le), static_cast<int>(SearchTextEncoding::Utf16Le));
        encoding_combo->addItem(encoding_label(SearchTextEncoding::Utf16Be), static_cast<int>(SearchTextEncoding::Utf16Be));
        encoding_combo->setCurrentIndex(encoding_combo->findData(static_cast<int>(initial_encoding)));
        text_form->addRow(QStringLiteral("Text encoding"), encoding_combo);
        options_stack->addWidget(text_page);

        auto* hex_page = new QWidget(options_stack);
        auto* hex_form = new QFormLayout(hex_page);
        auto* hex_hint = new QLabel(QStringLiteral("Use byte sequences like `4D 5A`, `DE AD BE EF`, or `0xCAFEBABE`."), hex_page);
        hex_hint->setWordWrap(true);
        hex_form->addRow(QStringLiteral("Hex mode"), hex_hint);
        options_stack->addWidget(hex_page);

        auto* value_page = new QWidget(options_stack);
        auto* value_form = new QFormLayout(value_page);
        numeric_type_combo = new QComboBox(value_page);
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
        numeric_type_combo->setCurrentIndex(numeric_type_combo->findData(static_cast<int>(initial_numeric_type)));

        byte_order_combo = new QComboBox(value_page);
        byte_order_combo->addItem(QStringLiteral("Little Endian"), static_cast<int>(SearchByteOrder::Little));
        byte_order_combo->addItem(QStringLiteral("Big Endian"), static_cast<int>(SearchByteOrder::Big));
        byte_order_combo->setCurrentIndex(byte_order_combo->findData(static_cast<int>(initial_byte_order)));

        value_form->addRow(QStringLiteral("Value type"), numeric_type_combo);
        value_form->addRow(QStringLiteral("Byte order"), byte_order_combo);
        options_stack->addWidget(value_page);

        form->addRow(label, text_input);
        form->addRow(QStringLiteral("Interpret as"), mode_combo);
        form->addRow(QStringLiteral("Mode options"), options_stack);

        auto update_mode_page = [=]() {
            const SearchInputMode mode = static_cast<SearchInputMode>(mode_combo->currentData().toInt());
            switch (mode) {
            case SearchInputMode::Text:
                options_stack->setCurrentWidget(text_page);
                break;
            case SearchInputMode::Hex:
                options_stack->setCurrentWidget(hex_page);
                break;
            case SearchInputMode::Value:
                options_stack->setCurrentWidget(value_page);
                break;
            }
        };
        connect(mode_combo, &QComboBox::currentIndexChanged, &dialog, update_mode_page);
        update_mode_page();
        root->addWidget(group);
    };

    QComboBox* find_text_input = nullptr;
    QComboBox* find_mode_combo = nullptr;
    QComboBox* find_encoding_combo = nullptr;
    QComboBox* find_numeric_type_combo = nullptr;
    QComboBox* find_byte_order_combo = nullptr;
    build_mode_section(
        &dialog,
        QStringLiteral("Find"),
        QStringLiteral("Find pattern"),
        last_search_display_text_,
        last_search_input_mode_,
        last_search_text_encoding_,
        last_search_numeric_type_,
        last_search_numeric_byte_order_,
        search_history_,
        find_text_input,
        find_mode_combo,
        find_encoding_combo,
        find_numeric_type_combo,
        find_byte_order_combo);

    QComboBox* replace_text_input = nullptr;
    QComboBox* replace_mode_combo = nullptr;
    QComboBox* replace_encoding_combo = nullptr;
    QComboBox* replace_numeric_type_combo = nullptr;
    QComboBox* replace_byte_order_combo = nullptr;
    build_mode_section(
        &dialog,
        QStringLiteral("Replace With"),
        QStringLiteral("Replace with"),
        last_replace_display_text_,
        last_replace_input_mode_,
        last_replace_text_encoding_,
        last_replace_numeric_type_,
        last_replace_numeric_byte_order_,
        replace_history_,
        replace_text_input,
        replace_mode_combo,
        replace_encoding_combo,
        replace_numeric_type_combo,
        replace_byte_order_combo);

    auto* options_group = new QGroupBox(QStringLiteral("Replace Options"), &dialog);
    auto* options_form = new QFormLayout(options_group);
    auto* action_combo = new QComboBox(options_group);
    action_combo->addItem(QStringLiteral("Replace Next"), static_cast<int>(SearchExecution::FindNext));
    action_combo->addItem(QStringLiteral("Replace All"), static_cast<int>(SearchExecution::FindAll));
    action_combo->setCurrentIndex(action_combo->findData(static_cast<int>(last_replace_execution_)));
    options_form->addRow(QStringLiteral("Action"), action_combo);

    QCheckBox* selection_only_check = nullptr;
    if (hex_view_ != nullptr && hex_view_->has_selection()) {
        selection_only_check = new QCheckBox(QStringLiteral("Search in selection only"), options_group);
        selection_only_check->setChecked(last_replace_selection_only_);
        options_form->addRow(QString(), selection_only_check);
    }
    root->addWidget(options_group);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const QString find_text = find_text_input->currentText().trimmed();
    const QString replace_text = replace_text_input->currentText().trimmed();
    if (find_text.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Enter a find pattern."), 3000);
        return false;
    }

    auto encode_input = [&](const QString& text,
                            QComboBox* mode_combo,
                            QComboBox* encoding_combo,
                            QComboBox* numeric_type_combo,
                            QComboBox* byte_order_combo,
                            QByteArray& bytes,
                            SearchInputMode& saved_mode,
                            SearchTextEncoding& saved_encoding,
                            NumericSearchType& saved_numeric_type,
                            SearchByteOrder& saved_byte_order) -> bool {
        const SearchInputMode mode = static_cast<SearchInputMode>(mode_combo->currentData().toInt());
        saved_mode = mode;
        switch (mode) {
        case SearchInputMode::Text:
            saved_encoding = static_cast<SearchTextEncoding>(encoding_combo->currentData().toInt());
            bytes = encode_search_text(text, saved_encoding);
            return true;
        case SearchInputMode::Hex:
            return try_parse_hex_bytes(text, bytes);
        case SearchInputMode::Value:
            saved_numeric_type = static_cast<NumericSearchType>(numeric_type_combo->currentData().toInt());
            saved_byte_order = static_cast<SearchByteOrder>(byte_order_combo->currentData().toInt());
            return encode_numeric_search_value(text, saved_numeric_type, saved_byte_order, bytes);
        }
        return false;
    };

    SearchInputMode find_mode = last_search_input_mode_;
    if (!encode_input(
            find_text,
            find_mode_combo,
            find_encoding_combo,
            find_numeric_type_combo,
            find_byte_order_combo,
            before,
            find_mode,
            last_search_text_encoding_,
            last_search_numeric_type_,
            last_search_numeric_byte_order_)) {
        statusBar()->showMessage(QStringLiteral("Invalid find value."), 3000);
        return false;
    }

    SearchInputMode replace_mode = SearchInputMode::Text;
    SearchTextEncoding replace_encoding = last_search_text_encoding_;
    NumericSearchType replace_numeric_type = last_search_numeric_type_;
    SearchByteOrder replace_byte_order = last_search_numeric_byte_order_;
    if (!encode_input(
            replace_text,
            replace_mode_combo,
            replace_encoding_combo,
            replace_numeric_type_combo,
            replace_byte_order_combo,
            after,
            replace_mode,
            replace_encoding,
            replace_numeric_type,
            replace_byte_order)) {
        statusBar()->showMessage(QStringLiteral("Invalid replacement value."), 3000);
        return false;
    }

    if (hex_view_->edit_mode() == HexView::EditMode::Overwrite && replace_mode == SearchInputMode::Hex) {
        if (after.size() > before.size()) {
            QMessageBox::warning(
                this,
                QStringLiteral("Replacement Too Long"),
                QStringLiteral("Raw hex replacement cannot be longer than the source pattern in overwrite mode."));
            return false;
        }
        if (after.size() < before.size()) {
            const auto response = QMessageBox::question(
                this,
                QStringLiteral("Pad Replacement"),
                QStringLiteral("The replacement byte sequence is shorter than the source. Pad the remaining bytes with 00?"),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                QMessageBox::Yes);
            if (response == QMessageBox::Cancel) {
                return false;
            }
            if (response == QMessageBox::Yes) {
                after.append(QByteArray(before.size() - after.size(), '\0'));
            }
        }
    }

    if (hex_view_->edit_mode() == HexView::EditMode::Overwrite && before.size() != after.size()) {
        statusBar()->showMessage(QStringLiteral("Replacement must match the original length in overwrite mode."), 4000);
        return false;
    }

    find_hex_mode = (find_mode == SearchInputMode::Hex || find_mode == SearchInputMode::Value);
    execution = static_cast<SearchExecution>(action_combo->currentData().toInt());
    selection_only = selection_only_check != nullptr && selection_only_check->isChecked();
    last_search_input_mode_ = find_mode;
    last_search_display_text_ = find_text;
    last_search_execution_ = execution;
    last_search_selection_only_ = selection_only;
    last_replace_display_text_ = replace_text;
    last_replace_input_mode_ = replace_mode;
    last_replace_text_encoding_ = replace_encoding;
    last_replace_numeric_type_ = replace_numeric_type;
    last_replace_numeric_byte_order_ = replace_byte_order;
    last_replace_execution_ = execution;
    last_replace_selection_only_ = selection_only;
    add_search_history(find_text);
    add_replace_history(replace_text);
    return true;
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

    QProgressDialog progress_dialog(this);
    progress_dialog.setWindowTitle(QStringLiteral("Search"));
    progress_dialog.setLabelText(QStringLiteral("Searching..."));
    progress_dialog.setCancelButtonText(QStringLiteral("Cancel"));
    progress_dialog.setRange(0, 1000);
    progress_dialog.setValue(0);
    progress_dialog.setMinimumDuration(0);
    progress_dialog.setWindowModality(Qt::ApplicationModal);
    progress_dialog.setMinimumWidth(460);
    progress_dialog.setMaximumWidth(460);

    auto format_bytes = [](qint64 bytes) -> QString {
        static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 4) {
            value /= 1024.0;
            ++unit;
        }
        return QStringLiteral("%1 %2").arg(unit == 0 ? QString::number(static_cast<qint64>(value)) : QString::number(value, 'f', 1)).arg(QString::fromLatin1(units[unit]));
    };

    QElapsedTimer ui_timer;
    ui_timer.start();
    qint64 last_ui_completed = -1;
    bool canceled = false;
    const auto progress_callback = [&](qint64 completed, qint64 total) -> bool {
        constexpr qint64 kUiUpdateBytes = 16LL * 1024 * 1024;
        constexpr qint64 kUiUpdateMs = 120;
        const bool force_update = completed <= 0 || completed >= total;
        if (!force_update && last_ui_completed >= 0 &&
            (completed - last_ui_completed) < kUiUpdateBytes &&
            ui_timer.elapsed() < kUiUpdateMs) {
            return true;
        }

        last_ui_completed = completed;
        ui_timer.restart();
        progress_dialog.setValue(total > 0 ? qBound(0, static_cast<int>((completed * 1000) / total), 1000) : 0);
        const QString label = total > 0
            ? QStringLiteral("Searching... %1 of %2").arg(format_bytes(completed)).arg(format_bytes(total))
            : QStringLiteral("Searching...");
        progress_dialog.setLabelText(label);
        statusBar()->showMessage(label);
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if (progress_dialog.wasCanceled()) {
            canceled = true;
            return false;
        }
        return true;
    };

    statusBar()->showMessage(QStringLiteral("Searching..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    progress_dialog.show();
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    qint64 found_offset = -1;
    bool search_canceled = false;
    const bool found = selection_only
        ? hex_view_->find_pattern_in_selection(last_search_pattern_, forward, from_caret, &found_offset, &search_canceled, progress_callback)
        : hex_view_->find_pattern(last_search_pattern_, forward, from_caret, &found_offset, &search_canceled, progress_callback);
    progress_dialog.setValue(1000);
    QApplication::restoreOverrideCursor();
    if (canceled || search_canceled) {
        statusBar()->showMessage(QStringLiteral("Search canceled."), 2500);
        return;
    }
    if (!found) {
        show_search_summary(
            QStringLiteral("No matches found%1.")
                .arg(selection_only ? QStringLiteral(" in the current selection") : QString()),
            QStringLiteral("Find"));
        statusBar()->showMessage(QStringLiteral("No matches found."), 2500);
        return;
    }

    show_search_matches(
        QStringLiteral("1 match at 0x%1").arg(found_offset, 8, 16, QChar(u'0')).toUpper(),
        QVector<qint64>{found_offset},
        QStringLiteral("Find"));
    statusBar()->showMessage(QStringLiteral("Match at 0x%1").arg(found_offset, 0, 16), 2500);
}

void MainWindow::run_find_all(bool selection_only) {
    if (!hex_view_ || !hex_view_->has_document()) {
        statusBar()->showMessage(QStringLiteral("Open a file before searching."), 2500);
        return;
    }

    if (last_search_pattern_.isEmpty()) {
        find();
        return;
    }

    QProgressDialog progress_dialog(this);
    progress_dialog.setWindowTitle(QStringLiteral("Find All"));
    progress_dialog.setLabelText(QStringLiteral("Searching..."));
    progress_dialog.setCancelButtonText(QStringLiteral("Cancel"));
    progress_dialog.setRange(0, 1000);
    progress_dialog.setValue(0);
    progress_dialog.setMinimumDuration(0);
    progress_dialog.setWindowModality(Qt::ApplicationModal);
    progress_dialog.setMinimumWidth(460);
    progress_dialog.setMaximumWidth(460);

    auto format_bytes = [](qint64 bytes) -> QString {
        static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 4) {
            value /= 1024.0;
            ++unit;
        }
        return QStringLiteral("%1 %2").arg(unit == 0 ? QString::number(static_cast<qint64>(value)) : QString::number(value, 'f', 1)).arg(QString::fromLatin1(units[unit]));
    };

    QElapsedTimer ui_timer;
    ui_timer.start();
    qint64 last_ui_completed = -1;
    bool canceled = false;
    const auto progress_callback = [&](qint64 completed, qint64 total) -> bool {
        constexpr qint64 kUiUpdateBytes = 16LL * 1024 * 1024;
        constexpr qint64 kUiUpdateMs = 120;
        const bool force_update = completed <= 0 || completed >= total;
        if (!force_update && last_ui_completed >= 0 &&
            (completed - last_ui_completed) < kUiUpdateBytes &&
            ui_timer.elapsed() < kUiUpdateMs) {
            return true;
        }

        last_ui_completed = completed;
        ui_timer.restart();
        progress_dialog.setValue(total > 0 ? qBound(0, static_cast<int>((completed * 1000) / total), 1000) : 0);
        const QString label = total > 0
            ? QStringLiteral("Searching... %1 of %2").arg(format_bytes(completed)).arg(format_bytes(total))
            : QStringLiteral("Searching...");
        progress_dialog.setLabelText(label);
        statusBar()->showMessage(label);
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if (progress_dialog.wasCanceled()) {
            canceled = true;
            return false;
        }
        return true;
    };

    statusBar()->showMessage(QStringLiteral("Searching..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    progress_dialog.show();
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    bool search_canceled = false;
    const QVector<qint64> matches = hex_view_->find_all_patterns(last_search_pattern_, selection_only, &search_canceled, progress_callback);
    progress_dialog.setValue(1000);
    QApplication::restoreOverrideCursor();
    if (canceled || search_canceled) {
        if (!matches.isEmpty()) {
            show_search_matches(
                QStringLiteral("%1 match(es) found before canceling%2.")
                    .arg(matches.size())
                    .arg(selection_only ? QStringLiteral(" in the current selection") : QString()),
                matches,
                QStringLiteral("Find"));
            statusBar()->showMessage(QStringLiteral("Search canceled after %1 match(es).").arg(matches.size()), 3000);
        } else {
            statusBar()->showMessage(QStringLiteral("Search canceled."), 2500);
        }
        return;
    }
    if (matches.isEmpty()) {
        show_search_summary(
            QStringLiteral("No matches found%1.")
                .arg(selection_only ? QStringLiteral(" in the current selection") : QString()),
            QStringLiteral("Find"));
    } else {
        show_search_matches(
            QStringLiteral("%1 match(es)%2.")
                .arg(matches.size())
                .arg(selection_only ? QStringLiteral(" in the current selection") : QString()),
            matches,
            QStringLiteral("Find"));
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
