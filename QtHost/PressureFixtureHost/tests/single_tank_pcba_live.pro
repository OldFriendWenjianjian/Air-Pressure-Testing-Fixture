QT += core network
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = SingleTankPcbaLive
INCLUDEPATH += $$PWD/../include
SOURCES += \
    single_tank_pcba_live.cpp \
    ../src/PressureFixtureModel.cpp \
    ../src/UsbControlProtocol.cpp \
    ../src/WindowsSerialTransport.cpp
HEADERS += ../include/WindowsSerialTransport.h
