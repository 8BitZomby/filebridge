cmake_minimum_required(VERSION 3.20)

project(FileBridge
    VERSION 0.2.0
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

# Generate a C++ header containing the semantic version declared by project().
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.hpp"
    @ONLY
)

# FileBridge uses Qt for its cross-platform GUI and networking.
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# Apply Qt's standard CMake setup, including automatic MOC/UIC handling.
qt_standard_project_setup()

# Create the FileBridge executable and list every source file that belongs to it.
qt_add_executable(fb
    include/ConnectionManager.hpp
    include/MainWindow.hpp
    include/NetworkInfo.hpp
    include/PeerConnection.hpp
    include/PeerConnector.hpp
    include/Protocol.hpp
    include/TcpListener.hpp
    include/TransferManager.hpp
    src/main.cpp
    src/ConnectionManager.cpp
    src/MainWindow.cpp
    src/NetworkInfo.cpp
    src/PeerConnection.cpp
    src/PeerConnector.cpp
    src/Protocol.cpp
    src/TcpListener.cpp
    src/TransferManager.cpp
)

# Allow project headers to be included without relative paths.
target_include_directories(fb PRIVATE
    include
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
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
