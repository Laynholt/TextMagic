cmake_minimum_required(VERSION 3.16)

project(TextMagic
    VERSION 0.1.0
    DESCRIPTION "Global text transformer host for Windows hotkey scripts"
    LANGUAGES CXX RC
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TM_APP_NAME "TextMagic")
set(TM_GITHUB_OWNER "Laynholt")
set(TM_GITHUB_REPO "TextMagic")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/AppVersion.h.in
    ${CMAKE_BINARY_DIR}/generated/AppVersion.h
    @ONLY
)

file(GLOB TM_LANGUAGE_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/lang/*.ini"
)
list(SORT TM_LANGUAGE_SOURCE_FILES)
string(JOIN "|" TM_LANGUAGE_SOURCE_FILES_ARG ${TM_LANGUAGE_SOURCE_FILES})

set(TM_EMBEDDED_LANG_HEADER "${CMAKE_BINARY_DIR}/generated/EmbeddedLanguages.h")

add_custom_command(
    OUTPUT "${TM_EMBEDDED_LANG_HEADER}"
    COMMAND ${CMAKE_COMMAND}
        -DTM_OUTPUT_FILE=${TM_EMBEDDED_LANG_HEADER}
        -DTM_LANG_FILES=${TM_LANGUAGE_SOURCE_FILES_ARG}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedLanguages.cmake
    DEPENDS
        ${TM_LANGUAGE_SOURCE_FILES}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedLanguages.cmake"
    VERBATIM
)

add_custom_target(TextMagicEmbeddedLanguages
    DEPENDS "${TM_EMBEDDED_LANG_HEADER}"
)

add_definitions(-DUNICODE -D_UNICODE)

set(SOURCES
    src/app/main.cpp
    src/app/Application.cpp
    src/app/AppUiHelpers.cpp
    src/app/FullscreenUtils.cpp
    src/app/Localization.cpp
    src/core/common/ClipboardUtils.cpp
    src/core/common/ApplicationBlacklist.cpp
    src/core/common/EncodingUtils.cpp
    src/core/common/LogFile.cpp
    src/core/common/PowerShellUtils.cpp
    src/core/scripts/ScriptManifest.cpp
    src/core/scripts/ScriptRunner.cpp
    src/core/text/InputBuffer.cpp
    src/core/text/TextBridge.cpp
    src/core/update/UpdateService.cpp
    src/ui/ToolTip.cpp
    src/ui/UiRenderer.cpp
    src/resources/resource.rc
)

add_executable(${PROJECT_NAME} WIN32
    ${SOURCES}
)
add_dependencies(${PROJECT_NAME} TextMagicEmbeddedLanguages)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scripts
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/text
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/update
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/resources
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    comctl32
    dwmapi
    gdi32
    gdiplus
    ole32
    shell32
    crypt32
    uxtheme
    user32
    winhttp
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4
        /permissive-
        /utf-8
    )
elseif(MINGW)
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_options(${PROJECT_NAME} PRIVATE -mwindows)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${TM_APP_NAME}"
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts"
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

enable_testing()

add_executable(TextMagicInputBufferTests
    src/core/text/InputBuffer.cpp
    tests/InputBufferTests.cpp
)

target_include_directories(TextMagicInputBufferTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/text
)

target_compile_features(TextMagicInputBufferTests PRIVATE cxx_std_17)

add_test(NAME TextMagicInputBufferTests COMMAND TextMagicInputBufferTests)

add_executable(TextMagicLogFileTests
    src/core/common/EncodingUtils.cpp
    src/core/common/LogFile.cpp
    tests/LogFileTests.cpp
)

target_include_directories(TextMagicLogFileTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
)

target_compile_features(TextMagicLogFileTests PRIVATE cxx_std_17)

add_test(NAME TextMagicLogFileTests COMMAND TextMagicLogFileTests)

add_executable(TextMagicApplicationBlacklistTests
    src/core/common/ApplicationBlacklist.cpp
    src/core/common/EncodingUtils.cpp
    tests/ApplicationBlacklistTests.cpp
)

target_include_directories(TextMagicApplicationBlacklistTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
)

target_compile_features(TextMagicApplicationBlacklistTests PRIVATE cxx_std_17)

add_test(NAME TextMagicApplicationBlacklistTests COMMAND TextMagicApplicationBlacklistTests)

add_executable(TextMagicScriptExecutionGateTests
    tests/ScriptExecutionGateTests.cpp
)

target_include_directories(TextMagicScriptExecutionGateTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

target_compile_features(TextMagicScriptExecutionGateTests PRIVATE cxx_std_17)

add_test(NAME TextMagicScriptExecutionGateTests COMMAND TextMagicScriptExecutionGateTests)

add_executable(TextMagicScriptInputSourceTests
    tests/ScriptInputSourceTests.cpp
)

target_include_directories(TextMagicScriptInputSourceTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

target_compile_features(TextMagicScriptInputSourceTests PRIVATE cxx_std_17)

add_test(NAME TextMagicScriptInputSourceTests COMMAND TextMagicScriptInputSourceTests)

add_executable(TextMagicTextBridgeInputUtilsTests
    tests/TextBridgeInputUtilsTests.cpp
)

target_include_directories(TextMagicTextBridgeInputUtilsTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/text
)

target_compile_features(TextMagicTextBridgeInputUtilsTests PRIVATE cxx_std_17)

add_test(NAME TextMagicTextBridgeInputUtilsTests COMMAND TextMagicTextBridgeInputUtilsTests)

add_executable(TextMagicFullscreenUtilsTests
    src/app/FullscreenUtils.cpp
    tests/FullscreenUtilsTests.cpp
)

target_include_directories(TextMagicFullscreenUtilsTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

target_compile_features(TextMagicFullscreenUtilsTests PRIVATE cxx_std_17)
target_link_libraries(TextMagicFullscreenUtilsTests PRIVATE dwmapi user32)

add_test(NAME TextMagicFullscreenUtilsTests COMMAND TextMagicFullscreenUtilsTests)

add_executable(TextMagicLocalizationTests
    src/app/Localization.cpp
    src/core/common/EncodingUtils.cpp
    tests/LocalizationTests.cpp
)
add_dependencies(TextMagicLocalizationTests TextMagicEmbeddedLanguages)

target_include_directories(TextMagicLocalizationTests PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
)

target_compile_features(TextMagicLocalizationTests PRIVATE cxx_std_17)

add_test(NAME TextMagicLocalizationTests COMMAND TextMagicLocalizationTests)

add_executable(TextMagicScriptRunnerTests
    src/app/Localization.cpp
    src/core/common/EncodingUtils.cpp
    src/core/scripts/ScriptRunner.cpp
    tests/ScriptRunnerTests.cpp
)
add_dependencies(TextMagicScriptRunnerTests TextMagicEmbeddedLanguages)

target_include_directories(TextMagicScriptRunnerTests PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scripts
)

target_compile_features(TextMagicScriptRunnerTests PRIVATE cxx_std_17)
target_link_libraries(TextMagicScriptRunnerTests PRIVATE crypt32)

add_test(NAME TextMagicScriptRunnerTests COMMAND TextMagicScriptRunnerTests)
