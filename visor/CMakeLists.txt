cmake_minimum_required(VERSION 3.21)

project(visor VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "visor only supports Windows.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.10 QUIET COMPONENTS Core Gui Qml Quick)
if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "Qt 6.10+ was not found.\n"
        "Run the bootstrap script to install the pinned Qt into .deps/:\n"
        "    pwsh etc/bootstrap.ps1\n"
        "then configure with a preset (cmake --preset debug), or point "
        "CMAKE_PREFIX_PATH at your own Qt install.")
endif()

qt_standard_project_setup(REQUIRES 6.10)

add_subdirectory(src)
