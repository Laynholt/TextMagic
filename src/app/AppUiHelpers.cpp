#include "AppUiHelpers.h"

#include "AppVersion.h"
#include "EncodingUtils.h"
#include "Localization.h"

#include <windowsx.h>
#include <commdlg.h>

namespace {
const wchar_t* T(const wchar_t* key) {
    return Localization::GetTextByName(key);
}
} // namespace

std::wstring BuildDialogFilter(std::initializer_list<DialogFilterEntry> entries) {
    std::wstring filter;
    for (const DialogFilterEntry& entry : entries) {
        filter += T(entry.labelKey);
        filter.push_back(L'\0');
        filter += entry.pattern ? entry.pattern : L"*.*";
        filter.push_back(L'\0');
    }
    filter.push_back(L'\0');
    return filter;
}

namespace {
void UpdateListBoxVerticalScrollbar(HWND listBox) {
    if (!listBox || !IsWindow(listBox)) {
        return;
    }

    RECT rect = {};
    GetClientRect(listBox, &rect);
    const int clientHeight = rect.bottom - rect.top;
    const int itemHeight = static_cast<int>(SendMessageW(listBox, LB_GETITEMHEIGHT, 0, 0));
    const LRESULT count = SendMessageW(listBox, LB_GETCOUNT, 0, 0);

    bool needScroll = false;
    if (clientHeight > 0 && itemHeight > 0 && count > 0) {
        int visibleItems = clientHeight / itemHeight;
        if (visibleItems < 1) {
            visibleItems = 1;
        }
        needScroll = static_cast<int>(count) > visibleItems;
    }

    ShowScrollBar(listBox, SB_VERT, needScroll ? TRUE : FALSE);
    SetWindowPos(
        listBox,
        nullptr,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
    );
    RedrawWindow(listBox, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
}

bool SaveUtf8TextFile(const std::wstring& filePath, const std::wstring& text, std::wstring* error) {
    HANDLE fileHandle = CreateFileW(
        filePath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (fileHandle == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = std::wstring(T(L"ui.error.file_open_prefix")) + std::to_wstring(GetLastError());
        }
        return false;
    }

    const std::string utf8 = EncodingUtils::WideToUtf8(text);
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;
    if (!WriteFile(fileHandle, bom, sizeof(bom), &written, nullptr)) {
        CloseHandle(fileHandle);
        if (error) {
            *error = std::wstring(T(L"ui.error.write_bom_prefix")) + std::to_wstring(GetLastError());
        }
        return false;
    }

    if (!utf8.empty()) {
        written = 0;
        if (!WriteFile(fileHandle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr)) {
            CloseHandle(fileHandle);
            if (error) {
                *error = std::wstring(T(L"ui.error.file_write_prefix")) + std::to_wstring(GetLastError());
            }
            return false;
        }
    }

    CloseHandle(fileHandle);
    return true;
}
} // namespace

void CopyEditSelectionOrAll(HWND editControl) {
    if (!editControl || !IsWindow(editControl)) {
        return;
    }

    SetFocus(editControl);
    const LRESULT selection = SendMessageW(editControl, EM_GETSEL, 0, 0);
    const int start = static_cast<int>(LOWORD(selection));
    const int end = static_cast<int>(HIWORD(selection));
    const bool noSelection = start == end;

    if (noSelection) {
        SendMessageW(editControl, EM_SETSEL, 0, -1);
    }
    SendMessageW(editControl, WM_COPY, 0, 0);
    if (noSelection) {
        SendMessageW(editControl, EM_SETSEL, start, start);
    }
}

bool SaveTextWithDialog(HWND ownerWindow, const std::wstring& text, std::wstring* savedPath, std::wstring* error) {
    if (savedPath) {
        savedPath->clear();
    }
    if (error) {
        error->clear();
    }

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t defaultName[128] = {};
    swprintf_s(defaultName, TM_APP_NAME_W L"-logs-%04u%02u%02u-%02u%02u%02u.txt",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    wchar_t filePath[MAX_PATH] = {};
    wcscpy_s(filePath, defaultName);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ownerWindow;
    const std::wstring filter = BuildDialogFilter({
        { L"ui.dialog.filter.text_files", L"*.txt" },
        { L"app.dialog.filter.all_files", L"*.*" }
    });
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrDefExt = L"txt";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetSaveFileNameW(&ofn)) {
        const DWORD dialogError = CommDlgExtendedError();
        if (dialogError != 0 && error) {
            *error = std::wstring(T(L"ui.error.save_dialog_prefix")) + std::to_wstring(dialogError);
        }
        return false;
    }

    if (!SaveUtf8TextFile(filePath, text, error)) {
        return false;
    }

    if (savedPath) {
        *savedPath = filePath;
    }
    return true;
}

void FillListBoxWithText(HWND listBox, const std::wstring& text) {
    if (!listBox || !IsWindow(listBox)) {
        return;
    }

    SendMessageW(listBox, WM_SETREDRAW, FALSE, 0);
    SendMessageW(listBox, LB_RESETCONTENT, 0, 0);

    int maxLineWidth = 0;
    HDC hdc = GetDC(listBox);
    HFONT oldFont = nullptr;
    if (hdc) {
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(listBox, WM_GETFONT, 0, 0));
        if (font) {
            oldFont = static_cast<HFONT>(SelectObject(hdc, font));
        }
    }

    auto addLine = [&](const std::wstring& line) {
        SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
        if (hdc) {
            SIZE size = {};
            if (GetTextExtentPoint32W(hdc, line.c_str(), static_cast<int>(line.size()), &size)) {
                if (size.cx > maxLineWidth) {
                    maxLineWidth = size.cx;
                }
            }
        }
    };

    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        const bool atEnd = (i == text.size());
        const bool isBreak = !atEnd && (text[i] == L'\r' || text[i] == L'\n');
        if (!atEnd && !isBreak) {
            continue;
        }

        addLine(text.substr(start, i - start));

        if (!atEnd && text[i] == L'\r' && (i + 1) < text.size() && text[i + 1] == L'\n') {
            ++i;
        }
        start = i + 1;
    }

    if (hdc) {
        if (oldFont) {
            SelectObject(hdc, oldFont);
        }
        ReleaseDC(listBox, hdc);
    }

    SendMessageW(listBox, LB_SETHORIZONTALEXTENT, static_cast<WPARAM>(maxLineWidth + 24), 0);

    const LRESULT count = SendMessageW(listBox, LB_GETCOUNT, 0, 0);
    if (count > 0) {
        SendMessageW(listBox, LB_SETTOPINDEX, static_cast<WPARAM>(count - 1), 0);
    }
    UpdateListBoxVerticalScrollbar(listBox);
    SendMessageW(listBox, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(listBox, nullptr, TRUE);
}

namespace {
int MeasureLineWidth(HDC hdc, const wchar_t* text, int length) {
    if (!hdc || !text || length <= 0) {
        return 0;
    }
    SIZE size = {};
    if (!GetTextExtentPoint32W(hdc, text, length, &size)) {
        return 0;
    }
    return size.cx;
}

size_t FindWrapEnd(HDC hdc, const std::wstring& line, size_t start, int maxWidthPx) {
    if (start >= line.size()) {
        return start;
    }
    if (maxWidthPx <= 0) {
        return start + 1;
    }

    size_t low = start + 1;
    size_t high = line.size();
    size_t best = start + 1;

    while (low <= high) {
        const size_t mid = low + (high - low) / 2;
        const int width = MeasureLineWidth(
            hdc,
            line.c_str() + start,
            static_cast<int>(mid - start)
        );
        if (width <= maxWidthPx) {
            best = mid;
            low = mid + 1;
        } else {
            if (mid == 0) {
                break;
            }
            high = mid - 1;
        }
    }

    if (best >= line.size()) {
        return best;
    }

    const size_t breakSpace = line.find_last_of(L" \t", best - 1);
    if (breakSpace != std::wstring::npos && breakSpace >= start + 1) {
        return breakSpace;
    }
    return best;
}
} // namespace

void FillListBoxWithWrappedText(HWND listBox, const std::wstring& text, bool scrollToBottom) {
    if (!listBox || !IsWindow(listBox)) {
        return;
    }

    SendMessageW(listBox, WM_SETREDRAW, FALSE, 0);
    SendMessageW(listBox, LB_RESETCONTENT, 0, 0);

    RECT rect = {};
    GetClientRect(listBox, &rect);
    const int clientWidth = (rect.right - rect.left) - 10;
    const int maxWidthPx = clientWidth > 1 ? clientWidth : 1;

    HDC hdc = GetDC(listBox);
    HFONT oldFont = nullptr;
    if (hdc) {
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(listBox, WM_GETFONT, 0, 0));
        if (font) {
            oldFont = static_cast<HFONT>(SelectObject(hdc, font));
        }
    }

    auto addLine = [&](const std::wstring& line) {
        SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    };

    auto pushWrapped = [&](const std::wstring& sourceLine) {
        if (sourceLine.empty()) {
            addLine(L"");
            return;
        }

        size_t start = 0;
        while (start < sourceLine.size()) {
            while (start < sourceLine.size() && (sourceLine[start] == L' ' || sourceLine[start] == L'\t')) {
                ++start;
            }
            if (start >= sourceLine.size()) {
                addLine(L"");
                break;
            }

            size_t end = FindWrapEnd(hdc, sourceLine, start, maxWidthPx);
            if (end <= start) {
                end = start + 1;
            }

            std::wstring wrapped = sourceLine.substr(start, end - start);
            while (!wrapped.empty() && (wrapped.back() == L' ' || wrapped.back() == L'\t')) {
                wrapped.pop_back();
            }
            addLine(wrapped);

            start = end;
            while (start < sourceLine.size() && (sourceLine[start] == L' ' || sourceLine[start] == L'\t')) {
                ++start;
            }
        }
    };

    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        const bool atEnd = (i == text.size());
        const bool isBreak = !atEnd && (text[i] == L'\r' || text[i] == L'\n');
        if (!atEnd && !isBreak) {
            continue;
        }

        pushWrapped(text.substr(start, i - start));

        if (!atEnd && text[i] == L'\r' && (i + 1) < text.size() && text[i + 1] == L'\n') {
            ++i;
        }
        start = i + 1;
    }

    if (hdc) {
        if (oldFont) {
            SelectObject(hdc, oldFont);
        }
        ReleaseDC(listBox, hdc);
    }

    SendMessageW(listBox, LB_SETHORIZONTALEXTENT, 0, 0);
    const LRESULT count = SendMessageW(listBox, LB_GETCOUNT, 0, 0);
    if (count > 0) {
        if (scrollToBottom) {
            SendMessageW(listBox, LB_SETTOPINDEX, static_cast<WPARAM>(count - 1), 0);
        } else {
            SendMessageW(listBox, LB_SETTOPINDEX, 0, 0);
        }
    }
    UpdateListBoxVerticalScrollbar(listBox);
    SendMessageW(listBox, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(listBox, nullptr, TRUE);
}

POINT ResolveContextMenuPoint(HWND control, LPARAM lParam) {
    POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    if (point.x == -1 && point.y == -1 && control && IsWindow(control)) {
        RECT rect = {};
        GetWindowRect(control, &rect);
        point.x = rect.left + 10;
        point.y = rect.top + 10;
    }
    return point;
}

std::wstring GetSelectedListBoxText(HWND listBox) {
    const LRESULT selectedIndex = SendMessageW(listBox, LB_GETCURSEL, 0, 0);
    if (selectedIndex == LB_ERR) {
        return std::wstring();
    }
    const LRESULT textLen = SendMessageW(listBox, LB_GETTEXTLEN, static_cast<WPARAM>(selectedIndex), 0);
    if (textLen <= 0) {
        return std::wstring();
    }
    std::wstring result(static_cast<size_t>(textLen) + 1, L'\0');
    SendMessageW(listBox, LB_GETTEXT, static_cast<WPARAM>(selectedIndex), reinterpret_cast<LPARAM>(result.data()));
    result.resize(static_cast<size_t>(textLen));
    return result;
}

bool IsTmscriptFilePath(const std::filesystem::path& path) {
    const std::wstring extension = path.extension().wstring();
    return _wcsicmp(extension.c_str(), L".tmscript") == 0;
}
