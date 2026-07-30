#pragma once

#include <cstdint>
#include <string>

class InputBuffer {
public:
    using ContextId = std::uintptr_t;

    struct PreviousWordCapture {
        std::wstring word;
        std::wstring trailing;
        size_t deleteChars = 0;
        size_t replaceOffset = 0;
        size_t expectedSize = 0;
        ContextId contextId = 0;
        std::uint64_t generation = 0;
    };

    void Clear();
    void PopCharacter(ContextId contextId);
    void AppendText(ContextId contextId, const std::wstring& text);

    bool TryPeekPreviousWord(ContextId contextId, PreviousWordCapture* capture) const;
    bool TryPeekAllText(ContextId contextId, PreviousWordCapture* capture) const;
    bool IsCaptureCurrent(ContextId contextId, const PreviousWordCapture& capture) const;
    bool CommitReplacement(ContextId contextId,
                           const PreviousWordCapture& capture,
                           const std::wstring& replacement);

    const std::wstring& TextForTest() const { return m_text; }

private:
    static bool IsWordSeparator(wchar_t ch);
    void SwitchContext(ContextId contextId);
    void ClearIfOverLimit();

    std::wstring m_text;
    ContextId m_contextId = 0;
    std::uint64_t m_generation = 0;
};
