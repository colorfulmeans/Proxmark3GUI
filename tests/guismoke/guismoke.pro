QT += core gui widgets serialport testlib
CONFIG += console testcase c++11
TEMPLATE = app
TARGET = tst_guismoke

GUI_SOURCE_DIR = $$clean_path($$PWD/../../src)
INCLUDEPATH += $$GUI_SOURCE_DIR

# Use the production GUI classes, forms and resources, with a test entry point.
# src/main.cpp is deliberately excluded: these tests never launch a PM3 client.
SOURCES += tst_guismoke.cpp \
           $$files($$GUI_SOURCE_DIR/common/*.cpp) \
           $$files($$GUI_SOURCE_DIR/module/*.cpp) \
           $$files($$GUI_SOURCE_DIR/ui/*.cpp)
HEADERS += $$files($$GUI_SOURCE_DIR/common/*.h) \
           $$files($$GUI_SOURCE_DIR/module/*.h) \
           $$files($$GUI_SOURCE_DIR/ui/*.h)
FORMS += $$files($$GUI_SOURCE_DIR/ui/*.ui)
RESOURCES += ../../config/config.qrc \
             ../../i18n/language.qrc \
             ../../src/qdarkstyle/dark/darkstyle.qrc \
             ../../src/qdarkstyle/light/lightstyle.qrc
