cmake_minimum_required(VERSION 3.16)

project(TextMagic
    VERSION 0.1.0
    DESCRIPTION "Global text transformer host for Windows hotkey scripts"
    LANGUAGES CXX RC
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
endif()

set(SOURCES
    src/app/main.cpp
    src/app/Application.cpp
    src/app/Localization.cpp
    src/core/scripts/ScriptManifest.cpp
    src/core/scripts/ScriptRunner.cpp
    src/core/text/TextBridge.cpp
    src/core/update/UpdateService.cpp
    src/ui/ToolTip.cpp
    src/ui/UiRenderer.cpp
    src/resources/resource.rc
)

set(HEADERS
    src/app/Application.h
    src/app/Localization.h
    src/core/scripts/ScriptManifest.h
    src/core/scripts/ScriptRunner.h
    src/core/text/TextBridge.h
    src/core/update/UpdateService.h
    src/ui/ToolTip.h
    src/ui/UiRenderer.h
    src/resources/resource.h
)

add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/scripts
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/text
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/update
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/resources
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        comctl32
        gdi32
        gdiplus
        ole32
        oleaut32
        shell32
        user32
        winhttp
    )
endif()

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
    OUTPUT_NAME "TextMagic"
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scripts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/lang"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/lang"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/lang"
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
