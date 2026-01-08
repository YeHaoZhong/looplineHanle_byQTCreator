#include "loopline_handle.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QStyleFactory>
#include <WinSock2.h>
#include "licensemanager.h"
#include "sqlconnectionpool.h"
#include <QIcon>
#include "logger.h"
int main(int argc, char *argv[])
{
#ifdef _WIN32
    WSADATA wsaData;
    int wsaRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaRet != 0) {
        Logger::getInstance().Log("---- [Error] WSAStartup failed: " + std::to_string(wsaRet));
        return -1;
        // 可视情况决定是否返回/退出
    }
#endif
    SqlConnectionPool::instance().init(10);
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/resource/logo.png"));
    // LicenseManager lic("license.json");
    // if (!lic.checkAndPrompt(nullptr))
    // {
    //     return -1;
    // }
    Logger::getInstance().Log("------------------------------------------------------ 窗口开启 ------------------------------------------------------");
    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "loopline_handle_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    QFont appFont("Segoe UI");
    appFont.setPointSizeF(10);
    a.setFont(appFont);
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QPalette pal;
    QColor base = QColor(250, 250, 250);
    QColor text = QColor(28, 28, 30);
    QColor mid = QColor(240, 240, 245);
    QColor accent = QColor(0, 120, 215);
    pal.setColor(QPalette::Window, base);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, QColor(255, 255, 255));
    pal.setColor(QPalette::AlternateBase, mid);
    pal.setColor(QPalette::ToolTipBase, QColor(255, 255, 220));
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, QColor(245, 245, 247));
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    a.setPalette(pal);
    a.setStyleSheet(R"(
* {
    outline: none;
    selection-background-color: #0078d7;
    selection-color: white;
}

/* 窗口与卡片背景（使用轻微灰） */
QWidget {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1,
                stop:0 rgba(252,252,253,255), stop:1 rgba(247,247,249,255));
    color: #1c1c1e;
    font-size: 10pt;
}

/* ---- 侧栏（导航） ---- */
QListWidget {
    background: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1,
                stop:0 rgba(250,250,252,255), stop:1 rgba(244,246,249,255));
    border-right: 1px solid rgba(0,0,0,0.06);
    padding-top: 12px;
    min-width: 180px;
}

/* 每个 item 基本样式 */
QListWidget::item {
    height: 42px;
    padding-left: 18px;
    padding-right: 12px;
    margin: 4px 8px;
    border-radius: 8px;
    color: #1c1c1e;
}

/* hover 通用 */
QListWidget::item:hover {
    background: rgba(0,0,0,0.04);
}
QListWidget::item:selected {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #006bb8, stop:1 #0057a3);
    color: #ffffff;
    font-weight: 700;
    padding-left: 14px;
}
QListWidget::item:selected:hover {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #0060a8, stop:1 #004f8f);
}

/* 分割把手 */
QSplitter::handle {
    background: transparent;
}
QSplitter::handle:hover {
    background: rgba(0,0,0,0.03);
}
QSplitter::handle:horizontal { width: 8px; }

/* ---- 按钮 ---- */
QPushButton {
    background: #0078d7;
    border-radius: 6px;
    color: white;
    padding: 8px 14px;
    min-height: 34px;
    border: none;
    font-weight: 600;
}
QPushButton:hover { background: #0067be; }
QPushButton:pressed { background: #005aa6; }
QPushButton[flat="true"] {
    background: transparent;
    color: #0078d7;
}

/* ---- 输入框 ---- */
QLineEdit, QTextEdit {
    background: #ffffff;

    border: 1px solid #e6e6ea;
    border-radius: 6px;
    padding: 8px;
    min-height: 28px;
}
QLineEdit:focus, QTextEdit:focus {
    border: 1px solid #8ec7ff;

}

/* 交替行 */
QTableView::item:alternate {
    background: rgba(0,0,0,0.01);
}

/* ---- ScrollArea ---- */
QScrollArea {
    border: none;
    background: transparent;
}

/* ---- 图片卡片 / 记录卡片 ---- */
.card {
    background: white;
    border-radius: 10px;
    border: 1px solid #ededf0;
    padding: 10px;
}
.card QLabel.title {
    font-weight: 700;
    font-size: 11pt;
}

/* Tooltip */
QToolTip {
    background-color: #2d2d2d;
    color: white;
    border-radius: 6px;
    padding: 6px;
}
    )");
    loopline_handle w;
    w.show();
    return a.exec();
}
