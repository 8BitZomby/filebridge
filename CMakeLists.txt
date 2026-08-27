cmake_minimum_required(VERSION 3.20)

project(FileBridge
    VERSION 0.1.0
    LANGUAGES CXX
)

# Generate compile_commands.json for clangd and other development tools.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use portable C++20 without compiler-specific language extensions.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable CTest infrastructure so tests can be added cleanly later.
include(CTest)

# FileBridge uses Qt for its cross-platform GUI and networking.
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# Apply Qt's standard CMake setup, including automatic MOC/UIC handling.
qt_standard_project_setup()

# Create the FileBridge executable and list every source file that belongs to it.
qt_add_executable(fb
    include/NetworkInfo.hpp
    include/TcpListener.hpp
    src/main.cpp
    src/NetworkInfo.cpp
    src/TcpListener.cpp
)

# Allow project headers to be included without relative paths.
target_include_directories(fb PRIVATE
    include
)

target_link_libraries(fb PRIVATE
    Qt6::Widgets
    Qt6::Network
)

# Build a normal macOS application bundle without affecting Windows builds.
if(APPLE)
    set_target_properties(fb PROPERTIES
        MACOSX_BUNDLE ON
    )
endif()
