#include "ClipboardUtils.h"

#include <cstring>
#include <iostream>

namespace {
bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

HGLOBAL CopyToGlobal(const void* source, size_t size) {
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!handle) {
        return nullptr;
    }
    void* destination = GlobalLock(handle);
    if (!destination) {
        GlobalFree(handle);
        return nullptr;
    }
    memcpy(destination, source, size);
    GlobalUnlock(handle);
    return handle;
}

bool SetTestClipboard(UINT customFormat) {
    if (!OpenClipboard(nullptr)) {
        return false;
    }
    EmptyClipboard();

    const wchar_t text[] = L"original";
    const DWORD customValue = 0x544D4743;
    HGLOBAL textHandle = CopyToGlobal(text, sizeof(text));
    HGLOBAL customHandle = CopyToGlobal(&customValue, sizeof(customValue));
    const bool ok = textHandle && customHandle
        && SetClipboardData(CF_UNICODETEXT, textHandle)
        && SetClipboardData(customFormat, customHandle);
    if (!ok) {
        if (textHandle && !IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            GlobalFree(textHandle);
        }
        if (customHandle && !IsClipboardFormatAvailable(customFormat)) {
            GlobalFree(customHandle);
        }
    }
    CloseClipboard();
    return ok;
}

bool HasCustomValue(UINT customFormat) {
    if (!OpenClipboard(nullptr)) {
        return false;
    }
    HANDLE handle = GetClipboardData(customFormat);
    const DWORD* value = handle ? static_cast<const DWORD*>(GlobalLock(handle)) : nullptr;
    const bool matches = value && *value == 0x544D4743;
    if (value) {
        GlobalUnlock(handle);
    }
    CloseClipboard();
    return matches;
}
}

int main() {
    ClipboardUtils::Snapshot originalClipboard;

    const UINT customFormat = RegisterClipboardFormatW(L"TextMagicClipboardUtilsTests");
    bool passed = Check(customFormat != 0, "custom clipboard format is registered")
        & Check(SetTestClipboard(customFormat), "test clipboard is prepared");

    {
        ClipboardUtils::Snapshot snapshot;
        passed &= Check(
            snapshot.IsComplete(),
            "fully duplicated snapshot is restorable"
        );
        passed &= Check(
            ClipboardUtils::WriteText(nullptr, L"temporary"),
            "clipboard is temporarily replaced"
        );
        passed &= Check(snapshot.Restore(), "snapshot restore reports success");
    }

    std::wstring restoredText;
    passed &= Check(
        ClipboardUtils::ReadText(nullptr, &restoredText) && restoredText == L"original",
        "text is restored"
    );
    passed &= Check(HasCustomValue(customFormat), "non-text format is restored");

    const wchar_t unterminatedUnicode[] = {L'u', L't'};
    std::wstring text;
    passed &= Check(
        !ClipboardUtils::Detail::DecodeTextBlock(
            CF_UNICODETEXT,
            unterminatedUnicode,
            sizeof(unterminatedUnicode),
            &text
        ),
        "unterminated Unicode is rejected"
    );

    const char unterminatedAnsi[] = {'a', 'n'};
    passed &= Check(
        !ClipboardUtils::Detail::DecodeTextBlock(
            CF_TEXT,
            unterminatedAnsi,
            sizeof(unterminatedAnsi),
            &text
        ),
        "unterminated ANSI is rejected"
    );

    const unsigned char oddUnicode[] = {0x41, 0x00, 0x00};
    passed &= Check(
        !ClipboardUtils::Detail::DecodeTextBlock(
            CF_UNICODETEXT,
            oddUnicode,
            sizeof(oddUnicode),
            &text
        ),
        "odd-sized Unicode is rejected"
    );

    const wchar_t emptyUnicode[] = {L'\0'};
    passed &= Check(
        ClipboardUtils::Detail::DecodeTextBlock(
            CF_UNICODETEXT,
            emptyUnicode,
            sizeof(emptyUnicode),
            &text
        ) && text.empty(),
        "empty Unicode is accepted"
    );
    passed &= Check(
        !ClipboardUtils::Detail::DecodeTextBlock(
            CF_UNICODETEXT,
            emptyUnicode,
            0,
            &text
        ),
        "zero-sized Unicode is rejected"
    );

    const char emptyAnsi[] = {'\0'};
    passed &= Check(
        ClipboardUtils::Detail::DecodeTextBlock(
            CF_TEXT,
            emptyAnsi,
            sizeof(emptyAnsi),
            &text
        ) && text.empty(),
        "empty ANSI is accepted"
    );
    passed &= Check(
        !ClipboardUtils::Detail::DecodeTextBlock(CF_TEXT, nullptr, 0, &text),
        "zero-sized ANSI is rejected"
    );

    passed &= Check(
        ClipboardUtils::Detail::IsClipboardEnumerationComplete(ERROR_SUCCESS),
        "successful clipboard enumeration is complete"
    );
    passed &= Check(
        !ClipboardUtils::Detail::IsClipboardEnumerationComplete(ERROR_ACCESS_DENIED),
        "failed clipboard enumeration is incomplete"
    );
    passed &= Check(
        ClipboardUtils::Detail::ClassifyClipboardProbe(false, false)
            == ClipboardUtils::Detail::ClipboardProbeResult::Unavailable,
        "clipboard probe open failure is unavailable"
    );
    passed &= Check(
        ClipboardUtils::Detail::ClassifyClipboardProbe(true, true)
            == ClipboardUtils::Detail::ClipboardProbeResult::Empty,
        "successful empty clipboard probe is empty"
    );
    passed &= Check(
        ClipboardUtils::Detail::ClassifyClipboardProbe(true, false)
            == ClipboardUtils::Detail::ClipboardProbeResult::NonEmpty,
        "successful non-empty clipboard probe is non-empty"
    );

    return passed ? 0 : 1;
}
