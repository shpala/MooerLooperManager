cmake_minimum_required(VERSION 3.16)

project(MooerLooperManager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(MooerLooperManager
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/worker.cpp
    src/worker.h
    src/usb_device.cpp
    src/usb_device.h
    src/protocol.cpp
    src/protocol.h
    src/audio_utils.cpp
    src/audio_utils.h
    resources/resources.qrc
)

target_link_libraries(MooerLooperManager PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Network
    ${LIBUSB_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
)

target_include_directories(MooerLooperManager PRIVATE
    src
    ${LIBUSB_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
)

if(UNIX)
    install(TARGETS MooerLooperManager DESTINATION bin)
    install(FILES resources/MooerLooperManager.desktop DESTINATION share/applications)
    install(FILES resources/io.github.shpala.MooerLooperManager.metainfo.xml DESTINATION share/metainfo)
    install(FILES assets/MooerLooperManager.png DESTINATION share/icons/hicolor/256x256/apps)
    install(FILES LICENSE DESTINATION share/doc/mooerloopermanager)
endif()
