QT += core gui widgets testlib serialport
TEMPLATE = app
TARGET = tst_processboundary
CONFIG += console testcase c++11
CONFIG -= app_bundle debug_and_release debug_and_release_target

SOURCES += tst_processboundary.cpp \
           ../../src/common/pm3process.cpp \
           ../../src/common/offlinepolicy.cpp \
           ../../src/common/localdictionary.cpp \
           ../../src/common/util.cpp
HEADERS += ../../src/common/pm3process.h \
           ../../src/common/offlinepolicy.h \
           ../../src/common/localdictionary.h \
           ../../src/common/util.h
FORMS += ../../src/ui/mainwindow.ui
INCLUDEPATH += ../../src/common

# Build a native helper with the SAME compiler as the test. A shell/Python
# fixture would correctly be rejected by OfflinePolicy. The helper has no Qt
# dependency and only echoes stdin; it does not use serial ports or networking.
FIXTURE_NAME = pm3fixture
win32: FIXTURE_NAME = pm3fixture.exe
fixture.target = $$FIXTURE_NAME
fixture.depends = $$PWD/fixtures/pm3fixture.cpp
fixture.commands = $(CXX) $(CXXFLAGS) $$shell_quote($$PWD/fixtures/pm3fixture.cpp) -o $$shell_quote($$FIXTURE_NAME) $(LFLAGS)
QMAKE_EXTRA_TARGETS += fixture
PRE_TARGETDEPS += $$FIXTURE_NAME
QMAKE_CLEAN += $$FIXTURE_NAME
