cmake_minimum_required(VERSION 3.16)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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
    src/app/ArchiveFileTransaction.cpp
    src/app/ArchiveImportPolicy.cpp
    src/app/AppUiHelpers.cpp
    src/app/FullscreenUtils.cpp
    src/app/Localization.cpp
    src/core/common/ClipboardUtils.cpp
    src/core/common/ApplicationBlacklist.cpp
    src/core/common/EncodingUtils.cpp
    src/core/common/FileSystemUtils.cpp
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
    set(multiValueArgs
        SOURCES
        INCLUDE_DIRS
        LINK_LIBRARIES
        COMPILE_DEFINITIONS
        DEPENDENCIES
    )
    cmake_parse_arguments(TEST "" "" "${multiValueArgs}" ${ARGN})
    add_executable(${target} ${TEST_SOURCES})
    target_compile_features(${target} PRIVATE cxx_std_17)
    textmagic_apply_warnings(${target})
    if(TEST_INCLUDE_DIRS)
        target_include_directories(${target} PRIVATE ${TEST_INCLUDE_DIRS})
    endif()
    if(TEST_LINK_LIBRARIES)
        target_link_libraries(${target} PRIVATE ${TEST_LINK_LIBRARIES})
    endif()
    if(TEST_COMPILE_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${TEST_COMPILE_DEFINITIONS})
    endif()
    if(TEST_DEPENDENCIES)
        add_dependencies(${target} ${TEST_DEPENDENCIES})
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

textmagic_add_test(TextMagicAppVersionTests
    SOURCES tests/AppVersionTests.cpp
    INCLUDE_DIRS ${CMAKE_BINARY_DIR}/generated
)

textmagic_add_test(TextMagicInputBufferTests
    SOURCES src/core/text/InputBuffer.cpp tests/InputBufferTests.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/text
)

textmagic_add_test(TextMagicLogFileTests
    SOURCES
        src/core/common/EncodingUtils.cpp
        src/core/common/LogFile.cpp
        tests/LogFileTests.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
)

textmagic_add_test(TextMagicFileSystemUtilsTests
    SOURCES src/core/common/FileSystemUtils.cpp tests/FileSystemUtilsTests.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
)

textmagic_add_test(TextMagicApplicationBlacklistTests
    SOURCES
        src/core/common/ApplicationBlacklist.cpp
        src/core/common/EncodingUtils.cpp
        tests/ApplicationBlacklistTests.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
)

textmagic_add_test(TextMagicRunningApplicationTests
    SOURCES
        src/core/common/ApplicationBlacklist.cpp
        src/core/common/EncodingUtils.cpp
        tests/RunningApplicationTests.cpp
    INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
)

textmagic_add_test(TextMagicScriptExecutionGateTests
    SOURCES tests/ScriptExecutionGateTests.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

textmagic_add_test(TextMagicTextBridgeInputUtilsTests
    SOURCES tests/TextBridgeInputUtilsTests.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/text
)

textmagic_add_test(TextMagicFullscreenUtilsTests
    SOURCES src/app/FullscreenUtils.cpp tests/FullscreenUtilsTests.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    LINK_LIBRARIES dwmapi user32
)

textmagic_add_test(TextMagicLocalizationTests
    SOURCES
        src/app/Localization.cpp
        src/core/common/EncodingUtils.cpp
        tests/LocalizationTests.cpp
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
    DEPENDENCIES TextMagicEmbeddedLanguages
)

textmagic_add_test(TextMagicScriptRunnerTests
    SOURCES
        src/app/Localization.cpp
        src/core/common/EncodingUtils.cpp
        src/core/common/FileSystemUtils.cpp
        src/core/scripts/ScriptManifest.cpp
        src/core/scripts/ScriptRunner.cpp
        tests/ScriptRunnerTests.cpp
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scripts
    COMPILE_DEFINITIONS TEXTMAGIC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    LINK_LIBRARIES crypt32
    DEPENDENCIES TextMagicEmbeddedLanguages
)

textmagic_add_test(TextMagicClipboardUtilsTests
    SOURCES src/core/common/ClipboardUtils.cpp tests/ClipboardUtilsTests.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
    LINK_LIBRARIES ole32 user32
)

textmagic_add_test(TextMagicUpdateServiceTests
    SOURCES
        src/app/Localization.cpp
        src/core/common/EncodingUtils.cpp
        src/core/common/PowerShellUtils.cpp
        src/core/update/UpdateService.cpp
        tests/UpdateServiceTests.cpp
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/update
    LINK_LIBRARIES bcrypt winhttp
    DEPENDENCIES TextMagicEmbeddedLanguages
)

textmagic_add_test(TextMagicScriptManifestTests
    SOURCES
        src/app/Localization.cpp
        src/core/common/EncodingUtils.cpp
        src/core/scripts/ScriptManifest.cpp
        tests/ScriptManifestTests.cpp
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scripts
    DEPENDENCIES TextMagicEmbeddedLanguages
)

textmagic_add_test(TextMagicPopupMenuNavigationTests
    SOURCES tests/PopupMenuNavigationTests.cpp
    INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)

textmagic_add_test(TextMagicOutputLayoutTests
    SOURCES tests/OutputLayoutTests.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

textmagic_add_test(TextMagicApplicationInfrastructureTests
    SOURCES tests/ApplicationInfrastructureTests.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

textmagic_add_test(TextMagicArchiveFileTransactionTests
    SOURCES
        src/app/ArchiveFileTransaction.cpp
        tests/ArchiveFileTransactionTests.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

textmagic_add_test(TextMagicArchiveImportPolicyTests
    SOURCES
        src/app/ArchiveImportPolicy.cpp
        src/app/Localization.cpp
        src/core/common/EncodingUtils.cpp
        src/core/common/FileSystemUtils.cpp
        src/core/common/PowerShellUtils.cpp
        src/core/scripts/ScriptManifest.cpp
        src/core/scripts/ScriptRunner.cpp
        tests/ArchiveImportPolicyTests.cpp
    INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/common
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scripts
    LINK_LIBRARIES crypt32
    DEPENDENCIES TextMagicEmbeddedLanguages
)

textmagic_add_test(TextMagicModifierGestureResolverTests
    SOURCES tests/ModifierGestureResolverTests.cpp
    INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scripts
)

textmagic_add_test(TextMagicInfoWindowModelTests
    SOURCES tests/InfoWindowModelTests.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)

textmagic_add_test(TextMagicInfoWindowLayoutTests
    SOURCES tests/InfoWindowLayoutTests.cpp
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/app
)
