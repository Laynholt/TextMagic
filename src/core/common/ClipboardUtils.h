#pragma once

#include <windows.h>

#include <objidl.h>
#include <string>

namespace ClipboardUtils {
class Snapshot {
public:
    Snapshot();
    ~Snapshot();

    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

private:
    void Restore();

    IDataObject* m_dataObject = nullptr;
    std::wstring m_text;
    bool m_hasText = false;
    bool m_wasEmpty = false;
    bool m_restored = false;
    bool m_oleInitialized = false;
};

bool Clear(HWND ownerWindow);
bool WriteText(HWND ownerWindow, const std::wstring& text);
bool ReadText(HWND ownerWindow, std::wstring* text);
}
