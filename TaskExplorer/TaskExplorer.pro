# ----------------------------------------------------
# This file is generated by the Qt Visual Studio Tools.
# ------------------------------------------------------

TEMPLATE = app
TARGET = TaskExplorer

DESTDIR = ../x64/Debug

QT += core network gui widgets concurrent
CONFIG += debug
DEFINES += _UNICODE _ENABLE_EXTENDED_ALIGNED_STORAGE QT_CONCURRENT_LIB QT_NETWORK_LIB QT_WIDGETS_LIB

QMAKE_LFLAGS += -Wl,-rpath,'\$\$ORIGIN'
QMAKE_LFLAGS +=-rdynamic

INCLUDEPATH += ./GeneratedFiles \
    . \
    ./GeneratedFiles/$(ConfigurationName)
    
LIBS += -L$$OUT_PWD/../x64/Debug/
LIBS += \
    -lqwt \
    -lqtservice\
    -lqhexedit \
    -lz
    
CONFIG += precompile_header
PRECOMPILED_HEADER = stdafx.h
DEPENDPATH += .
MOC_DIR += ./GeneratedFiles
OBJECTS_DIR += debug
UI_DIR += ./GeneratedFiles
RCC_DIR += ./GeneratedFiles
include(TaskExplorer.pri)
