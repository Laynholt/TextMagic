#include "ClipboardUtils.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace {
bool OpenClipboardWithRetry(HWND ownerWindow) {
    for (int attempt = 0; attempt < 12; ++attempt) {
        if (OpenClipboard(ownerWindow)) {
            return true;
        }
        Sleep(10);
    }
    return false;
}

ClipboardUtils::Detail::ClipboardProbeResult ProbeClipboardEmpty(HWND ownerWindow) {
    if (!OpenClipboardWithRetry(ownerWindow)) {
        return ClipboardUtils::Detail::ClipboardProbeResult::Unavailable;
    }

    SetLastError(ERROR_SUCCESS);
    const int formatCount = CountClipboardFormats();
    const DWORD probeError = GetLastError();
    const auto result = ClipboardUtils::Detail::ClassifyClipboardProbe(
        true,
        formatCount == 0
    );
    CloseClipboard();
    return probeError == ERROR_SUCCESS
        ? result
        : ClipboardUtils::Detail::ClipboardProbeResult::Unavailable;
}

void FreeClipboardData(UINT format, HANDLE handle) {
    if (!handle) {
        return;
    }
    if (format == CF_BITMAP || format == CF_DSPBITMAP || format == CF_PALETTE) {
        DeleteObject(handle);
    } else if (format == CF_ENHMETAFILE || format == CF_DSPENHMETAFILE) {
        DeleteEnhMetaFile(static_cast<HENHMETAFILE>(handle));
    } else if (format == CF_METAFILEPICT || format == CF_DSPMETAFILEPICT) {
        auto* metafile = static_cast<METAFILEPICT*>(GlobalLock(handle));
        if (metafile) {
            DeleteMetaFile(metafile->hMF);
            GlobalUnlock(handle);
        }
        GlobalFree(handle);
    } else {
        GlobalFree(handle);
    }
}
}

namespace ClipboardUtils {
Snapshot::Snapshot() {
    if (OpenClipboardWithRetry(nullptr)) {
        m_complete = true;
        SetLastError(ERROR_SUCCESS);
        const int formatCount = CountClipboardFormats();
        const DWORD countError = GetLastError();
        m_wasEmpty = formatCount == 0 && countError == ERROR_SUCCESS;
        if (countError != ERROR_SUCCESS) {
            m_complete = false;
        }

        UINT format = 0;
        for (;;) {
            SetLastError(ERROR_SUCCESS);
            format = EnumClipboardFormats(format);
            if (format == 0) {
                if (!Detail::IsClipboardEnumerationComplete(GetLastError())) {
                    m_complete = false;
                }
                break;
            }

            HANDLE source = GetClipboardData(format);
            if (!source) {
                m_complete = false;
                continue;
            }

            HANDLE duplicate = OleDuplicateData(
                source,
                static_cast<CLIPFORMAT>(format),
                0
            );
            if (!duplicate) {
                m_complete = false;
                continue;
            }
            m_formats.push_back({format, duplicate});
        }
        CloseClipboard();
    }

    m_hasText = ReadText(nullptr, &m_text);
    if (!m_hasText && m_formats.empty() && m_wasEmpty && m_complete) {
        m_text.clear();
        if (ProbeClipboardEmpty(nullptr) == Detail::ClipboardProbeResult::Unavailable) {
            m_complete = false;
        }
    }
}

Snapshot::~Snapshot() {
    Restore();
    for (const FormatData& item : m_formats) {
        FreeClipboardData(item.format, item.handle);
    }
}

bool Snapshot::IsComplete() const noexcept {
    return m_complete;
}

bool Snapshot::Restore() {
    if (m_restored) {
        return true;
    }
    if (m_restoreAttempted || !m_complete) {
        return false;
    }

    if (!m_formats.empty()) {
        if (!OpenClipboardWithRetry(nullptr)) {
            return false;
        }

        if (!EmptyClipboard()) {
            CloseClipboard();
            return false;
        }

        m_restoreAttempted = true;
        bool restoredAll = true;
        for (FormatData& item : m_formats) {
            if (item.handle && SetClipboardData(item.format, item.handle)) {
                item.handle = nullptr;
            } else {
                restoredAll = false;
            }
        }
        CloseClipboard();

        if (restoredAll) {
            m_restored = true;
        }
        return restoredAll;
    }

    if (m_hasText) {
        if (WriteText(nullptr, m_text)) {
            m_restored = true;
            return true;
        }
        return false;
    }

    if (m_wasEmpty) {
        if (Clear(nullptr)) {
            m_restored = true;
            return true;
        }
    }
    return false;
}

bool Clear(HWND ownerWindow) {
    if (!OpenClipboardWithRetry(ownerWindow)) {
        return false;
    }

    const bool cleared = EmptyClipboard() != FALSE;
    CloseClipboard();
    return cleared;
}

bool WriteText(HWND ownerWindow, const std::wstring& text) {
    if (!OpenClipboardWithRetry(ownerWindow)) {
        return false;
    }

    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memoryHandle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memoryHandle) {
        CloseClipboard();
        return false;
    }

    void* memory = GlobalLock(memoryHandle);
    if (!memory) {
        GlobalFree(memoryHandle);
        CloseClipboard();
        return false;
    }

    std::memcpy(memory, text.c_str(), bytes);
    GlobalUnlock(memoryHandle);

    if (!SetClipboardData(CF_UNICODETEXT, memoryHandle)) {
        GlobalFree(memoryHandle);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

namespace Detail {
bool IsClipboardEnumerationComplete(DWORD terminalError) noexcept {
    return terminalError == ERROR_SUCCESS;
}

ClipboardProbeResult ClassifyClipboardProbe(bool opened, bool empty) noexcept {
    if (!opened) {
        return ClipboardProbeResult::Unavailable;
    }
    return empty ? ClipboardProbeResult::Empty : ClipboardProbeResult::NonEmpty;
}

bool DecodeTextBlock(UINT format, const void* raw, SIZE_T bytes, std::wstring* text) {
    if (text) {
        text->clear();
    }

    if (!raw) {
        return false;
    }

    if (format == CF_UNICODETEXT) {
        if (bytes == 0 || bytes % sizeof(wchar_t) != 0) {
            return false;
        }
        const auto* begin = static_cast<const wchar_t*>(raw);
        const size_t characterCount = bytes / sizeof(wchar_t);
        const wchar_t* end = begin + characterCount;
        const wchar_t* terminator = std::find(begin, end, L'\0');
        if (terminator == end) {
            return false;
        }
        if (text) {
            text->assign(begin, terminator);
        }
        return true;
    }

    if (format != CF_TEXT || bytes == 0) {
        return false;
    }

    const auto* begin = static_cast<const char*>(raw);
    const char* end = begin + bytes;
    const char* terminator = std::find(begin, end, '\0');
    if (terminator == end) {
        return false;
    }

    const size_t byteCount = static_cast<size_t>(terminator - begin);
    if (byteCount > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }

    if (byteCount == 0) {
        return true;
    }

    const int requiredChars = MultiByteToWideChar(
        CP_ACP,
        0,
        begin,
        static_cast<int>(byteCount),
        nullptr,
        0
    );
    if (requiredChars <= 0) {
        return false;
    }

    if (text) {
        std::wstring wideText(static_cast<size_t>(requiredChars), L'\0');
        const int convertedChars = MultiByteToWideChar(
            CP_ACP,
            0,
            begin,
            static_cast<int>(byteCount),
            &wideText[0],
            requiredChars
        );
        if (convertedChars != requiredChars) {
            return false;
        }
        *text = wideText;
    }
    return true;
}
}

bool ReadText(HWND ownerWindow, std::wstring* text) {
    if (text) {
        text->clear();
    }
    if (!OpenClipboardWithRetry(ownerWindow)) {
        return false;
    }

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        const SIZE_T bytes = GlobalSize(handle);
        const void* raw = GlobalLock(handle);
        if (!raw) {
            CloseClipboard();
            return false;
        }
        const bool decoded = Detail::DecodeTextBlock(CF_UNICODETEXT, raw, bytes, text);
        GlobalUnlock(handle);
        CloseClipboard();
        return decoded;
    }

    handle = GetClipboardData(CF_TEXT);
    if (!handle) {
        CloseClipboard();
        return false;
    }

    const SIZE_T bytes = GlobalSize(handle);
    const void* raw = GlobalLock(handle);
    if (!raw) {
        CloseClipboard();
        return false;
    }

    const bool decoded = Detail::DecodeTextBlock(CF_TEXT, raw, bytes, text);
    GlobalUnlock(handle);

    CloseClipboard();
    return decoded;
}
}
