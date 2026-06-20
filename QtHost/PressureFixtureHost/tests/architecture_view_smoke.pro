QT += core gui widgets testlib

CONFIG += console c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = ArchitectureViewSmoke

INCLUDEPATH += $$PWD/../include

SOURCES += \
    architecture_view_smoke.cpp \
    ../src/ArchitectureView.cpp \
    ../src/PressureFixtureModel.cpp

HEADERS += \
    ../include/ArchitectureView.h \
    ../include/PressureFixtureModel.h
