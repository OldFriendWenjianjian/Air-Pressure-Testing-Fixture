QT += core

CONFIG += console c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = ProtocolSmoke

INCLUDEPATH += $$PWD/../include

SOURCES += \
    protocol_smoke.cpp \
    ../src/PressureFixtureModel.cpp \
    ../src/UsbControlProtocol.cpp
