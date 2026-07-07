QT += core network

CONFIG += console c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = SerialTransportSmoke

INCLUDEPATH += $$PWD/../include

SOURCES += \
    serial_transport_smoke.cpp \
    ../src/PressureFixtureModel.cpp \
    ../src/UsbControlProtocol.cpp \
    ../src/WindowsSerialTransport.cpp

HEADERS += \
    ../include/PressureFixtureModel.h \
    ../include/UsbControlProtocol.h \
    ../include/WindowsSerialTransport.h

win32 {
    DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN
}
