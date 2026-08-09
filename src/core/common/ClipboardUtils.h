#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace ClipboardUtils {
class Snapshot {
public:
    Snapshot();
    ~Snapshot();

    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

private:
    struct FormatData {
        UINT format = 0;
        HANDLE handle = nullptr;
    };

    void Restore();

    std::vector<FormatData> m_formats;
    std::wstring m_text;
    bool m_hasText = false;
    bool m_wasEmpty = false;
    bool m_restored = false;
};

bool Clear(HWND ownerWindow);
bool WriteText(HWND ownerWindow, const std::wstring& text);
bool ReadText(HWND ownerWindow, std::wstring* text);
}
