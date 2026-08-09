#include "ClipboardUtils.h"

#include <cstring>

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

bool IsClipboardEmpty(HWND ownerWindow) {
    if (!OpenClipboardWithRetry(ownerWindow)) {
        return false;
    }
    const bool empty = CountClipboardFormats() == 0;
    CloseClipboard();
    return empty;
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
        m_wasEmpty = CountClipboardFormats() == 0;
        UINT format = 0;
        while ((format = EnumClipboardFormats(format)) != 0) {
            HANDLE source = GetClipboardData(format);
            HANDLE duplicate = source
                ? OleDuplicateData(source, static_cast<CLIPFORMAT>(format), 0)
                : nullptr;
            if (duplicate) {
                m_formats.push_back({format, duplicate});
            }
        }
        CloseClipboard();
    }

    m_hasText = ReadText(nullptr, &m_text);
    if (!m_hasText) {
        m_text.clear();
        if (m_formats.empty()) {
            m_wasEmpty = IsClipboardEmpty(nullptr);
        }
    }
}

Snapshot::~Snapshot() {
    Restore();
    for (const FormatData& item : m_formats) {
        FreeClipboardData(item.format, item.handle);
    }
}

void Snapshot::Restore() {
    if (m_restored) {
        return;
    }

    if (!m_formats.empty() && OpenClipboardWithRetry(nullptr)) {
        bool restoredAny = false;
        if (EmptyClipboard()) {
            for (FormatData& item : m_formats) {
                if (item.handle && SetClipboardData(item.format, item.handle)) {
                    item.handle = nullptr;
                    restoredAny = true;
                }
            }
        }
        CloseClipboard();
        if (restoredAny) {
            m_restored = true;
            return;
        }
    }

    if (m_hasText) {
        if (WriteText(nullptr, m_text)) {
            m_restored = true;
        }
        return;
    }

    if (m_wasEmpty) {
        if (Clear(nullptr)) {
            m_restored = true;
        }
    }
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

bool ReadText(HWND ownerWindow, std::wstring* text) {
    if (text) {
        text->clear();
    }
    if (!OpenClipboardWithRetry(ownerWindow)) {
        return false;
    }

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        const wchar_t* raw = static_cast<const wchar_t*>(GlobalLock(handle));
        if (!raw) {
            CloseClipboard();
            return false;
        }
        if (text) {
            *text = raw;
        }
        GlobalUnlock(handle);
        CloseClipboard();
        return true;
    }

    handle = GetClipboardData(CF_TEXT);
    if (!handle) {
        CloseClipboard();
        return false;
    }

    const char* rawAnsi = static_cast<const char*>(GlobalLock(handle));
    if (!rawAnsi) {
        CloseClipboard();
        return false;
    }

    const int requiredChars = MultiByteToWideChar(CP_ACP, 0, rawAnsi, -1, nullptr, 0);
    if (requiredChars <= 0) {
        GlobalUnlock(handle);
        CloseClipboard();
        return false;
    }

    if (text) {
        std::wstring wideText(static_cast<size_t>(requiredChars), L'\0');
        MultiByteToWideChar(CP_ACP, 0, rawAnsi, -1, &wideText[0], requiredChars);
        if (!wideText.empty() && wideText.back() == L'\0') {
            wideText.pop_back();
        }
        *text = wideText;
    }

    GlobalUnlock(handle);
    CloseClipboard();
    return true;
}
}
