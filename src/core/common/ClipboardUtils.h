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

    bool IsComplete() const noexcept;
    bool Restore();

private:
    struct FormatData {
        UINT format = 0;
        HANDLE handle = nullptr;
    };

    std::vector<FormatData> m_formats;
    std::wstring m_text;
    bool m_complete = false;
    bool m_hasText = false;
    bool m_wasEmpty = false;
    bool m_restoreAttempted = false;
    bool m_restored = false;
};

bool Clear(HWND ownerWindow);
bool WriteText(HWND ownerWindow, const std::wstring& text);
bool ReadText(HWND ownerWindow, std::wstring* text);

namespace Detail {
constexpr SIZE_T kMaxSnapshotBytes = 64ull * 1024ull * 1024ull;

enum class ClipboardProbeResult {
    Empty,
    NonEmpty,
    Unavailable,
};

bool TryAccumulateSnapshotBytes(
    SIZE_T current,
    SIZE_T next,
    SIZE_T limit,
    SIZE_T* total) noexcept;
bool DecodeTextBlock(UINT format, const void* raw, SIZE_T bytes, std::wstring* text);
bool IsClipboardEnumerationComplete(DWORD terminalError) noexcept;
ClipboardProbeResult ClassifyClipboardProbe(bool opened, bool empty) noexcept;
}
}
