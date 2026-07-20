QT += core
QT -= gui

CONFIG += console c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = IntelHexValidatorSmoke

INCLUDEPATH += $$PWD/../include

SOURCES += \
    intel_hex_validator_smoke.cpp \
    ../src/IntelHexValidator.cpp

HEADERS += \
    ../include/IntelHexValidator.h
