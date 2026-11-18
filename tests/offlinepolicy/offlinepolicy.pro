QT += core testlib
QT -= gui
TEMPLATE = app
TARGET = tst_offlinepolicy
CONFIG += console testcase c++11
CONFIG -= app_bundle

SOURCES += tst_offlinepolicy.cpp \
           ../../src/common/offlinepolicy.cpp
HEADERS += ../../src/common/offlinepolicy.h
INCLUDEPATH += ../../src/common
