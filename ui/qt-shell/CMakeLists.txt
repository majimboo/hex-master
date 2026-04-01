cmake_minimum_required(VERSION 3.27)
project(hex_master_qt_shell VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_standard_project_setup()

set(HEX_MASTER_RUST_PROFILE "$<IF:$<CONFIG:Debug>,debug,release>")
set(HEX_MASTER_RUST_LIB "${CMAKE_SOURCE_DIR}/../../target/${HEX_MASTER_RUST_PROFILE}/hexapp_ffi.dll.lib")
set(HEX_MASTER_RUST_DLL "${CMAKE_SOURCE_DIR}/../../target/${HEX_MASTER_RUST_PROFILE}/hexapp_ffi.dll")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/app_version.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/app_version.rc"
    @ONLY
)

qt_add_executable(hex_master_qt_shell WIN32
    "${CMAKE_CURRENT_BINARY_DIR}/app_version.rc"
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
target_compile_definitions(hex_master_qt_shell PRIVATE HEX_MASTER_VERSION="${PROJECT_VERSION}")
target_link_libraries(hex_master_qt_shell PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets "${HEX_MASTER_RUST_LIB}")
set_target_properties(hex_master_qt_shell PROPERTIES OUTPUT_NAME "HexMaster")

add_custom_command(TARGET hex_master_qt_shell POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${HEX_MASTER_RUST_DLL}"
        "$<TARGET_FILE_DIR:hex_master_qt_shell>/hexapp_ffi.dll"
)
