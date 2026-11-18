QT += core testlib
QT -= gui
CONFIG += console testcase c++11
TEMPLATE = app
TARGET = tst_localdictionary

INCLUDEPATH += ../../src/common
SOURCES += tst_localdictionary.cpp \
           ../../src/common/localdictionary.cpp
HEADERS += ../../src/common/localdictionary.h
