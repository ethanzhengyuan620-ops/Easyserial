cmake_minimum_required(VERSION 3.28)

project(EasySerialCpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INSTALL_BINDIR ".")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets SerialPort)
qt_standard_project_setup()
include(cmake/Protobuf.cmake)

add_library(EasySerialLib STATIC
    src/Settings.h src/Settings.cpp
    src/Theme.h
    src/core/HexUtils.h src/core/HexUtils.cpp
    src/core/FrameParser.h src/core/FrameParser.cpp
    src/core/SerialController.h src/core/SerialController.cpp
    src/core/AutoReplyEngine.h src/core/AutoReplyEngine.cpp
    src/core/ColorEngine.h src/core/ColorEngine.cpp
    src/core/LogWriter.h src/core/LogWriter.cpp
    src/core/TaskProtoDecoder.h src/core/TaskProtoDecoder.cpp
    src/core/ProtoSchema.h src/core/ProtoSchema.cpp
    src/model/ReceiveModel.h src/model/ReceiveModel.cpp
    src/ui/ReceiveView.h src/ui/ReceiveView.cpp
    src/ui/SearchBar.h src/ui/SearchBar.cpp
    src/ui/SerialPanel.h src/ui/SerialPanel.cpp
    src/ui/SendPanel.h src/ui/SendPanel.cpp
    src/ui/AutoReplyWidget.h src/ui/AutoReplyWidget.cpp
    src/ui/ToolsDialog.h src/ui/ToolsDialog.cpp
    src/ui/RuleEditorDialog.h src/ui/RuleEditorDialog.cpp
    src/ui/AppearanceDialog.h src/ui/AppearanceDialog.cpp
    src/ui/MainWindow.h src/ui/MainWindow.cpp
    src/ui/MonitorWindow.h src/ui/MonitorWindow.cpp
    src/ui/PbDecoderWidget.h src/ui/PbDecoderWidget.cpp
)

target_include_directories(EasySerialLib PUBLIC src)
target_link_libraries(EasySerialLib PUBLIC
    EasyProtoImport
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::SerialPort
)

if(MSVC)
    target_compile_options(EasySerialLib PUBLIC /W4 /permissive- /utf-8)
else()
    target_compile_options(EasySerialLib PUBLIC -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(EasySerial WIN32 src/main.cpp resources.qrc "${CMAKE_CURRENT_BINARY_DIR}/protobuf-resources.qrc")
target_link_libraries(EasySerial PRIVATE EasySerialLib)
set_target_properties(EasySerial PROPERTIES OUTPUT_NAME EasySerial)
if(WIN32)
    enable_language(RC)
    find_package(Qt6 REQUIRED COMPONENTS Svg)
    add_executable(EasySerialIconGenerator scripts/make_icon.cpp)
    target_link_libraries(EasySerialIconGenerator PRIVATE Qt6::Core Qt6::Gui Qt6::Svg)
    set(APP_ICON "${CMAKE_CURRENT_BINARY_DIR}/generated/easyserial.ico")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_custom_command(OUTPUT "${APP_ICON}"
        COMMAND EasySerialIconGenerator "${CMAKE_CURRENT_SOURCE_DIR}/assets/easyserial.svg" "${APP_ICON}"
        DEPENDS EasySerialIconGenerator "${CMAKE_CURRENT_SOURCE_DIR}/assets/easyserial.svg"
        VERBATIM)
    add_custom_target(EasySerialIcon DEPENDS "${APP_ICON}")
    add_dependencies(EasySerial EasySerialIcon)
    configure_file(assets/EasySerial.rc.in generated/EasySerial.rc @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/EasySerial.rc"
        PROPERTIES OBJECT_DEPENDS "${APP_ICON}")
    target_sources(EasySerial PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/EasySerial.rc")
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(EasySerialTests tests/RegressionTests.cpp resources.qrc "${CMAKE_CURRENT_BINARY_DIR}/protobuf-resources.qrc")
    target_link_libraries(EasySerialTests PRIVATE EasySerialLib Qt6::Test)
    target_compile_definitions(EasySerialTests PRIVATE EASYSERIAL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(WIN32)
        add_test(NAME regression COMMAND EasySerialTests -platform windows -o regression-results.txt,txt)
    else()
        add_test(NAME regression COMMAND EasySerialTests -platform offscreen -o regression-results.txt,txt)
    endif()
    set_tests_properties(regression PROPERTIES TIMEOUT 60)
endif()

install(TARGETS EasySerial
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)

qt_generate_deploy_app_script(
    TARGET EasySerial
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
