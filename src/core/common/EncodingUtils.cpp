#include "EncodingUtils.h"

#include <windows.h>

namespace EncodingUtils {
std::string WideToUtf8(const std::wstring& text, bool allowAnsiFallback) {
    if (text.empty()) {
        return std::string();
    }

    const int utf8Size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (utf8Size > 0) {
        std::string utf8(static_cast<size_t>(utf8Size), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            text.data(),
            static_cast<int>(text.size()),
            utf8.data(),
            utf8Size,
            nullptr,
            nullptr
        );
        return utf8;
    }

    if (!allowAnsiFallback) {
        return std::string();
    }

    const int ansiSize = WideCharToMultiByte(
        CP_ACP,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (ansiSize <= 0) {
        return std::string();
    }

    std::string ansi(static_cast<size_t>(ansiSize), '\0');
    WideCharToMultiByte(
        CP_ACP,
        0,
        text.data(),
        static_cast<int>(text.size()),
        ansi.data(),
        ansiSize,
        nullptr,
        nullptr
    );
    return ansi;
}

std::wstring Utf8ToWide(const std::string& text, bool strictUtf8, bool allowAnsiFallback) {
    if (text.empty()) {
        return std::wstring();
    }

    const DWORD utf8Flags = strictUtf8 ? MB_ERR_INVALID_CHARS : 0;
    const int utf8Size = MultiByteToWideChar(
        CP_UTF8,
        utf8Flags,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );
    if (utf8Size > 0) {
        std::wstring wide(static_cast<size_t>(utf8Size), L'\0');
        MultiByteToWideChar(
            CP_UTF8,
            utf8Flags,
            text.data(),
            static_cast<int>(text.size()),
            wide.data(),
            utf8Size
        );
        if (!wide.empty() && wide.front() == 0xFEFF) {
            wide.erase(wide.begin());
        }
        return wide;
    }

    if (!allowAnsiFallback) {
        return std::wstring();
    }

    const int ansiSize = MultiByteToWideChar(
        CP_ACP,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );
    if (ansiSize <= 0) {
        return std::wstring();
    }

    std::wstring ansi(static_cast<size_t>(ansiSize), L'\0');
    MultiByteToWideChar(
        CP_ACP,
        0,
        text.data(),
        static_cast<int>(text.size()),
        ansi.data(),
        ansiSize
    );
    if (!ansi.empty() && ansi.front() == 0xFEFF) {
        ansi.erase(ansi.begin());
    }
    return ansi;
}
} // namespace EncodingUtils
