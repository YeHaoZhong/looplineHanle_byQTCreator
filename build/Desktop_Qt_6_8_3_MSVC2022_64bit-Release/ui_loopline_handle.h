/********************************************************************************
** Form generated from reading UI file 'loopline_handle.ui'
**
** Created by: Qt User Interface Compiler version 6.8.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_LOOPLINE_HANDLE_H
#define UI_LOOPLINE_HANDLE_H

#include <QtCore/QVariant>
#include <QtGui/QIcon>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_loopline_handle
{
public:
    QWidget *centralwidget;
    QMenuBar *menubar;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *loopline_handle)
    {
        if (loopline_handle->objectName().isEmpty())
            loopline_handle->setObjectName("loopline_handle");
        loopline_handle->resize(800, 600);
        QIcon icon;
        icon.addFile(QString::fromUtf8(":/i18n/loopline_handle_zh_CN.qm"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        loopline_handle->setWindowIcon(icon);
        centralwidget = new QWidget(loopline_handle);
        centralwidget->setObjectName("centralwidget");
        loopline_handle->setCentralWidget(centralwidget);
        menubar = new QMenuBar(loopline_handle);
        menubar->setObjectName("menubar");
        menubar->setGeometry(QRect(0, 0, 800, 22));
        loopline_handle->setMenuBar(menubar);
        statusbar = new QStatusBar(loopline_handle);
        statusbar->setObjectName("statusbar");
        loopline_handle->setStatusBar(statusbar);

        retranslateUi(loopline_handle);

        QMetaObject::connectSlotsByName(loopline_handle);
    } // setupUi

    void retranslateUi(QMainWindow *loopline_handle)
    {
        loopline_handle->setWindowTitle(QCoreApplication::translate("loopline_handle", "loopline_handle", nullptr));
    } // retranslateUi

};

namespace Ui {
    class loopline_handle: public Ui_loopline_handle {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_LOOPLINE_HANDLE_H
