QT       += core gui network concurrent core5compat

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    caritemswritethread.cpp \
    dataprocessmain.cpp \
    devicemanager.cpp \
    licensemanager.cpp \
    logger.cpp \
    main.cpp \
    loopline_handle.cpp \
    otherfunction.cpp \
    plccontrol.cpp \
    requestapi.cpp \
    snap7.cpp \
    socketclinet.cpp \
    sqlconnection.cpp \
    sqlconnectionpool.cpp \
    steplogger.cpp

HEADERS += \
    StructInfo.h \
    caritemswritethread.h \
    dataprocessmain.h \
    devicemanager.h \
    licensemanager.h \
    logger.h \
    loopline_handle.h \
    plccontrol.h \
    requestapi.h \
    snap7.h \
    socketclinet.h \
    sqlconnection.h \
    sqlconnectionpool.h \
    steplogger.h

FORMS += \
    loopline_handle.ui

TRANSLATIONS += \
    loopline_handle_zh_CN.ts
CONFIG += lrelease
CONFIG += embed_translations
INCLUDEPATH += D:/vcpkg/installed/x64-windows/include
MYSQL_DIR = "D:/Program Files/MySQL/MySQL Server 8.0"
INCLUDEPATH += $$MYSQL_DIR/include
LIBS += -L$$MYSQL_DIR/lib -llibmysql
LIBS += -LD:/vcpkg/installed/x64-windows/lib -lspdlog
LIBS += "D:/vcpkg/packages/libsodium_x64-windows/lib/libsodium.lib"
LIBS += "D:/project/company/2025/QT/loopline_handle/snap7.lib"
RC_ICONS = "D:/project/company/2025/QT/loopline_handle/resource/logo.ico"
# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
