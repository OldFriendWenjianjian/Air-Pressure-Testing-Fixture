cmake_minimum_required(VERSION 3.20)

project(PressureFixtureHost VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_add_executable(PressureFixtureHost
    src/main.cpp
    src/ArchitectureView.cpp
    src/IntelHexValidator.cpp
    src/MainWindow.cpp
    src/PressureFixtureModel.cpp
    src/UsbControlProtocol.cpp
    src/WindowsSerialTransport.cpp
    assets.qrc
)

target_include_directories(PressureFixtureHost PRIVATE include)
target_link_libraries(PressureFixtureHost PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

if(WIN32)
    target_compile_definitions(PressureFixtureHost PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

qt_add_executable(ProtocolSmoke
    tests/protocol_smoke.cpp
    src/PressureFixtureModel.cpp
    src/UsbControlProtocol.cpp
)
target_include_directories(ProtocolSmoke PRIVATE include)
target_link_libraries(ProtocolSmoke PRIVATE Qt6::Core)
