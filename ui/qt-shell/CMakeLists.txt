cmake_minimum_required(VERSION 3.27)
project(hex_master_qt_shell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_standard_project_setup()

set(HEX_MASTER_RUST_LIB "${CMAKE_SOURCE_DIR}/../../target/debug/hexapp_ffi.dll.lib")
set(HEX_MASTER_RUST_DLL "${CMAKE_SOURCE_DIR}/../../target/debug/hexapp_ffi.dll")

qt_add_executable(hex_master_qt_shell WIN32
    resources/app_icon.rc
    resources/app_resources.qrc
    src/file_byte_source.cpp
    src/hex_view.cpp
    src/main.cpp
    src/main_window.cpp
    include/file_byte_source.hpp
    include/hex_bridge.h
    include/hex_view.hpp
    include/main_window.hpp
)

target_include_directories(hex_master_qt_shell PRIVATE include)
target_link_libraries(hex_master_qt_shell PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets "${HEX_MASTER_RUST_LIB}")

add_custom_command(TARGET hex_master_qt_shell POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${HEX_MASTER_RUST_DLL}"
        "$<TARGET_FILE_DIR:hex_master_qt_shell>/hexapp_ffi.dll"
)
