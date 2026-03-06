if(NOT DEFINED TM_OUTPUT_FILE)
    message(FATAL_ERROR "TM_OUTPUT_FILE is required")
endif()

if(NOT DEFINED TM_LANG_FILES)
    message(FATAL_ERROR "TM_LANG_FILES is required")
endif()

string(REPLACE "|" ";" TM_LANG_FILES "${TM_LANG_FILES}")

get_filename_component(TM_OUTPUT_DIR "${TM_OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${TM_OUTPUT_DIR}")

set(TM_HEADER_CONTENT
"#pragma once\n\
\n\
#include <cstddef>\n\
\n\
namespace Localization::EmbeddedLanguageFiles {\n\
struct File {\n\
    const wchar_t* languageCode;\n\
    const unsigned char* utf8Data;\n\
    size_t utf8Size;\n\
};\n\
\n")

foreach(TM_LANG_FILE IN LISTS TM_LANG_FILES)
    get_filename_component(TM_LANGUAGE_CODE "${TM_LANG_FILE}" NAME_WE)
    get_filename_component(TM_LANGUAGE_NAME "${TM_LANG_FILE}" NAME_WE)
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" TM_VAR_SUFFIX "${TM_LANGUAGE_NAME}")
    if(TM_VAR_SUFFIX MATCHES "^[0-9]")
        set(TM_VAR_SUFFIX "_${TM_VAR_SUFFIX}")
    endif()

    file(READ "${TM_LANG_FILE}" TM_FILE_HEX HEX)
    string(LENGTH "${TM_FILE_HEX}" TM_HEX_LENGTH)
    math(EXPR TM_BYTE_COUNT "${TM_HEX_LENGTH} / 2")
    string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1, " TM_BYTE_LIST "${TM_FILE_HEX}")

    string(APPEND TM_HEADER_CONTENT
"inline constexpr unsigned char k${TM_VAR_SUFFIX}Data[] = { ${TM_BYTE_LIST}};\n")
endforeach()

string(APPEND TM_HEADER_CONTENT "\ninline constexpr File kFiles[] = {\n")

foreach(TM_LANG_FILE IN LISTS TM_LANG_FILES)
    get_filename_component(TM_LANGUAGE_CODE "${TM_LANG_FILE}" NAME_WE)
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" TM_VAR_SUFFIX "${TM_LANGUAGE_CODE}")
    if(TM_VAR_SUFFIX MATCHES "^[0-9]")
        set(TM_VAR_SUFFIX "_${TM_VAR_SUFFIX}")
    endif()

    string(APPEND TM_HEADER_CONTENT
"    { L\"${TM_LANGUAGE_CODE}\", k${TM_VAR_SUFFIX}Data, sizeof(k${TM_VAR_SUFFIX}Data) },\n")
endforeach()

string(APPEND TM_HEADER_CONTENT
"};\n\
\n\
inline constexpr size_t kFileCount = sizeof(kFiles) / sizeof(kFiles[0]);\n\
} // namespace Localization::EmbeddedLanguageFiles\n")

file(WRITE "${TM_OUTPUT_FILE}" "${TM_HEADER_CONTENT}")
