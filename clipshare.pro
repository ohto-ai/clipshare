######################################################################
# Automatically generated by qmake (3.1) Fri May 6 09:45:08 2022
######################################################################

TEMPLATE = app
TARGET = clipshare
DESTDIR = bin/
INCLUDEPATH += src \
    src/3rd/include/

CONFIG += c++14

OBJECTS_DIR = build/obj/
MOC_DIR = build/
RCC_DIR = build/
UI_DIR = build/

DEFINES += QT_DEPRECATED_WARNINGS

QT += core \
    network \
    gui \
    widgets

# Input
HEADERS += src/*.h
FORMS += src/ClipShareWindow.ui
SOURCES += src/*.cpp
RESOURCES += src/*.qrc
