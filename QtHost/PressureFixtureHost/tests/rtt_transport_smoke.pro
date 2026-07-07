QT += core network

CONFIG += console c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = RttTransportSmoke

INCLUDEPATH += $$PWD/../include

SOURCES += \
    rtt_transport_smoke.cpp \
    ../src/PressureFixtureModel.cpp \
    ../src/UsbControlProtocol.cpp \
    ../src/WindowsSerialTransport.cpp

HEADERS += \
    ../include/UsbControlProtocol.h \
    ../include/WindowsSerialTransport.h
