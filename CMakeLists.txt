cmake_minimum_required(VERSION 3.16)

project(TextMagic
    VERSION 1.1.0
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

file(GLOB TM_LANGUAGE_SOURCE_FILES CONFIGURE_DEPENDS
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
    bcrypt
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

function(textmagic_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /utf-8
        )
    elseif(MINGW)
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
endfunction()

textmagic_apply_warnings(${PROJECT_NAME})

if(MINGW)
    target_link_options(${PROJECT_NAME} PRIVATE -mwindows)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${TM_APP_NAME}"
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts"
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

enable_testing()

function(textmagic_add_test target)
    add_executable(${target} ${ARGN})
    target_compile_features(${target} PRIVATE cxx_std_17)
    textmagic_apply_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
endfunction()

textmagic_add_test(TextMagicAppVersionTests
    tests/AppVersionTests.cpp
)

target_include_directories(TextMagicAppVersionTests PRIVATE
    ${CMAKE_BINARY_DIR}/generated
)

textmagic_add_test(TextMagicInputBufferTests
    src/core/text/InputBuffer.cpp
    tests/InputBufferTests.cpp
)

target_include_directories(TextMagicInputBufferTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/text
)

textmagic_add_test(TextMagicLogFileTests
    src/core/common/EncodingUtils.cpp
    src/core/common/LogFile.cpp
    tests/LogFileTests.cpp
)

target_include_directories(TextMagicLogFileTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
)

textmagic_add_test(TextMagicApplicationBlacklistTests
    src/core/common/ApplicationBlacklist.cpp
    src/core/common/EncodingUtils.cpp
    tests/ApplicationBlacklistTests.cpp
)

target_include_directories(TextMagicApplicationBlacklistTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
)

textmagic_add_test(TextMagicRunningApplicationTests
    src/core/common/ApplicationBlacklist.cpp
    src/core/common/EncodingUtils.cpp
    tests/RunningApplicationTests.cpp
)

target_include_directories(TextMagicRunningApplicationTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
)

textmagic_add_test(TextMagicScriptExecutionGateTests
    tests/ScriptExecutionGateTests.cpp
)

target_include_directories(TextMagicScriptExecutionGateTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

textmagic_add_test(TextMagicTextBridgeInputUtilsTests
    tests/TextBridgeInputUtilsTests.cpp
)

target_include_directories(TextMagicTextBridgeInputUtilsTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/text
)

textmagic_add_test(TextMagicFullscreenUtilsTests
    src/app/FullscreenUtils.cpp
    tests/FullscreenUtilsTests.cpp
)

target_include_directories(TextMagicFullscreenUtilsTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

target_link_libraries(TextMagicFullscreenUtilsTests PRIVATE dwmapi user32)

textmagic_add_test(TextMagicLocalizationTests
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

textmagic_add_test(TextMagicScriptRunnerTests
    src/app/Localization.cpp
    src/core/common/EncodingUtils.cpp
    src/core/scripts/ScriptManifest.cpp
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

target_compile_definitions(TextMagicScriptRunnerTests PRIVATE
    TEXTMAGIC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(TextMagicScriptRunnerTests PRIVATE crypt32)

textmagic_add_test(TextMagicClipboardUtilsTests
    src/core/common/ClipboardUtils.cpp
    tests/ClipboardUtilsTests.cpp
)

target_include_directories(TextMagicClipboardUtilsTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
)

target_link_libraries(TextMagicClipboardUtilsTests PRIVATE ole32 user32)

textmagic_add_test(TextMagicUpdateServiceTests
    src/app/Localization.cpp
    src/core/common/EncodingUtils.cpp
    src/core/common/PowerShellUtils.cpp
    src/core/update/UpdateService.cpp
    tests/UpdateServiceTests.cpp
)
add_dependencies(TextMagicUpdateServiceTests TextMagicEmbeddedLanguages)

target_include_directories(TextMagicUpdateServiceTests PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/update
)

target_link_libraries(TextMagicUpdateServiceTests PRIVATE bcrypt winhttp)

textmagic_add_test(TextMagicScriptManifestTests
    src/app/Localization.cpp
    src/core/common/EncodingUtils.cpp
    src/core/scripts/ScriptManifest.cpp
    tests/ScriptManifestTests.cpp
)
add_dependencies(TextMagicScriptManifestTests TextMagicEmbeddedLanguages)

target_include_directories(TextMagicScriptManifestTests PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scripts
)

textmagic_add_test(TextMagicPopupMenuNavigationTests
    tests/PopupMenuNavigationTests.cpp
)

target_include_directories(TextMagicPopupMenuNavigationTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)

textmagic_add_test(TextMagicOutputLayoutTests
    tests/OutputLayoutTests.cpp
)

target_include_directories(TextMagicOutputLayoutTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

textmagic_add_test(TextMagicModifierGestureResolverTests
    tests/ModifierGestureResolverTests.cpp
)

target_include_directories(TextMagicModifierGestureResolverTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scripts
)

textmagic_add_test(TextMagicInfoWindowModelTests
    tests/InfoWindowModelTests.cpp
)

target_include_directories(TextMagicInfoWindowModelTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

textmagic_add_test(TextMagicInfoWindowLayoutTests
    tests/InfoWindowLayoutTests.cpp
)

target_include_directories(TextMagicInfoWindowLayoutTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)
