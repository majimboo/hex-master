#include <QApplication>
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

#include "main_window.hpp"

#ifndef HEX_MASTER_VERSION
#define HEX_MASTER_VERSION "1.0.0"
#endif

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Hex Master");
    app.setApplicationVersion(QStringLiteral(HEX_MASTER_VERSION));
    app.setOrganizationName("Hex Master");
    app.setWindowIcon(QIcon(QStringLiteral(":/appicon.png")));
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(236, 239, 244));
    palette.setColor(QPalette::WindowText, QColor(34, 44, 61));
    palette.setColor(QPalette::Base, QColor(252, 252, 248));
    palette.setColor(QPalette::AlternateBase, QColor(229, 234, 240));
    palette.setColor(QPalette::Text, QColor(23, 31, 43));
    palette.setColor(QPalette::Button, QColor(236, 239, 244));
    palette.setColor(QPalette::ButtonText, QColor(34, 44, 61));
    palette.setColor(QPalette::Highlight, QColor(73, 115, 177));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::ToolTipBase, QColor(255, 252, 234));
    palette.setColor(QPalette::ToolTipText, QColor(34, 44, 61));
    app.setPalette(palette);

    QFont ui_font(QStringLiteral("Segoe UI"), 9);
    app.setFont(ui_font);
    app.setStyleSheet(
        "QMainWindow { background: #eceff4; }"
        "QMenuBar { background: #e3e7ee; border-bottom: 1px solid #bcc7d6; color: #243244; }"
        "QMenuBar::item { padding: 4px 8px; background: transparent; }"
        "QMenuBar::item:selected { background: #d7e1ef; }"
        "QToolBar { background: #e4e9f0; border-bottom: 1px solid #bcc7d6; spacing: 6px; padding: 4px 6px; }"
        "QToolButton { background: #f7f9fc; border: 1px solid #bcc7d6; border-radius: 3px; padding: 4px 10px; color: #233246; }"
        "QToolButton:hover { background: #ebf1f8; border-color: #9eb4cf; }"
        "QToolButton:pressed { background: #d7e3f2; }"
        "QLineEdit { background: #fcfcf8; border: 1px solid #b7c3d4; border-radius: 3px; padding: 4px 6px; color: #243244; }"
        "QMenu { background: #fcfcf8; border: 1px solid #b7c3d4; padding: 4px; }"
        "QMenu::item { padding: 5px 26px 5px 22px; }"
        "QMenu::item:selected { background: #dbe7f7; color: #1f2d3f; }"
        "QStatusBar { background: #e7ebf1; border-top: 1px solid #bcc7d6; color: #233246; }"
        "QDockWidget { color: #233246; }"
        "QDockWidget::title { background: #dde4ee; border: 1px solid #bcc7d6; padding: 5px 8px; text-align: left; }"
        "QTextEdit { background: #fbfbf8; border: 1px solid #c5ced9; color: #334155; selection-background-color: #6d93c7; }"
        "QScrollBar:vertical { background: #edf1f6; width: 14px; margin: 0px; }"
        "QScrollBar::handle:vertical { background: #b7c5d8; min-height: 28px; border-radius: 4px; margin: 2px; }"
        "QScrollBar::handle:vertical:hover { background: #98acc6; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    );

    MainWindow window;
    window.setWindowIcon(QIcon(QStringLiteral(":/appicon.png")));
    window.resize(1440, 900);
    window.show();

    return app.exec();
}
