######################################################################
# Automatically generated by qmake (3.1) Fri May 6 09:45:08 2022
######################################################################

TEMPLATE = app
TARGET = clipshare
INCLUDEPATH += src \
    src/3rd/include/

# The following define makes your compiler warn you if you use any
# feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

QT += core \
    network \
    gui \
    widgets

# Input
HEADERS += src/ClipShareWindow.h \
           src/SingleApplication.h \
           src/Utils.h
FORMS += src/ClipShareWindow.ui
SOURCES += src/ClipShareWindow.cpp \
           src/main.cpp \
           src/SingleApplication.cpp \
           src/Utils.cpp
RESOURCES += src/ClipShareWindow.qrc
