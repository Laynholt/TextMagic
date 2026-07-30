#pragma once

#include <string>

class InputBuffer {
public:
    struct PreviousWordCapture {
        std::wstring word;
        std::wstring trailing;
        size_t deleteChars = 0;
        size_t replaceOffset = 0;
        size_t expectedSize = 0;
    };

    void Clear();
    void PopCharacter();
    void AppendText(const std::wstring& text);

    bool TryPeekPreviousWord(PreviousWordCapture* capture) const;
    bool IsCaptureCurrent(const PreviousWordCapture& capture) const;
    bool CommitReplacement(const PreviousWordCapture& capture, const std::wstring& replacement);

    const std::wstring& TextForTest() const { return m_text; }

private:
    static bool IsWordSeparator(wchar_t ch);
    void TrimToLimit();

    std::wstring m_text;
};
