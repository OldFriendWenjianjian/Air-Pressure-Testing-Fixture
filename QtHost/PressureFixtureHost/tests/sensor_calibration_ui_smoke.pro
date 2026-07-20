QT += core gui widgets network charts

CONFIG += console c++17
CONFIG -= app_bundle
DEFINES += PRESSURE_FIXTURE_HOST_TEST_ACCESS

TEMPLATE = app
TARGET = SensorCalibrationUiSmoke

INCLUDEPATH += $$PWD/../include

SOURCES += \
    sensor_calibration_ui_smoke.cpp \
    ../src/ArchitectureView.cpp \
    ../src/IntelHexValidator.cpp \
    ../src/MainWindow.cpp \
    ../src/PressureFixtureModel.cpp \
    ../src/UsbControlProtocol.cpp \
    ../src/WindowsSerialTransport.cpp

HEADERS += \
    ../include/ArchitectureView.h \
    ../include/IntelHexValidator.h \
    ../include/MainWindow.h \
    ../include/PressureFixtureModel.h \
    ../include/UsbControlProtocol.h \
    ../include/WindowsSerialTransport.h

RESOURCES += ../assets.qrc

win32 {
    DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN
    RC_FILE = ../app_icon.rc
}
