QT += core gui widgets

CONFIG += c++17
CONFIG -= app_bundle

TEMPLATE = app
TARGET = PressureFixtureHost

INCLUDEPATH += $$PWD/include

SOURCES += \
    src/main.cpp \
    src/ArchitectureView.cpp \
    src/MainWindow.cpp \
    src/PressureFixtureModel.cpp \
    src/Simulator.cpp \
    src/UsbControlProtocol.cpp \
    src/WindowsSerialTransport.cpp

HEADERS += \
    include/ArchitectureView.h \
    include/MainWindow.h \
    include/PressureFixtureModel.h \
    include/Simulator.h \
    include/UsbControlProtocol.h \
    include/WindowsSerialTransport.h

win32 {
    DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN
}
