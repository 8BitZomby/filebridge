cmake_minimum_required(VERSION 3.20)

project(FileBridge
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

qt_add_executable(fb
    src/main.cpp
)

target_link_libraries(fb PRIVATE
    Qt6::Widgets
    Qt6::Network
)

set_target_properties(fb PROPERTIES
    MACOSX_BUNDLE ON
)
