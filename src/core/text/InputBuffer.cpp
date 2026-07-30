#include "InputBuffer.h"

#include <cwctype>

namespace {
constexpr size_t MAX_INPUT_BUFFER_CHARS = 2048;
}

void InputBuffer::Clear() {
    m_text.clear();
}

void InputBuffer::PopCharacter() {
    if (!m_text.empty()) {
        m_text.pop_back();
    }
}

void InputBuffer::AppendText(const std::wstring& text) {
    if (text.empty()) {
        return;
    }
    m_text += text;
    TrimToLimit();
}

bool InputBuffer::TryPeekPreviousWord(PreviousWordCapture* capture) const {
    if (capture) {
        *capture = PreviousWordCapture();
    }
    if (m_text.empty()) {
        return false;
    }

    size_t wordEnd = m_text.size();
    while (wordEnd > 0 && IsWordSeparator(m_text[wordEnd - 1])) {
        --wordEnd;
    }
    if (wordEnd == 0) {
        return false;
    }

    size_t wordStart = wordEnd;
    while (wordStart > 0 && !IsWordSeparator(m_text[wordStart - 1])) {
        --wordStart;
    }
    if (wordStart >= wordEnd) {
        return false;
    }

    if (capture) {
        capture->word = m_text.substr(wordStart, wordEnd - wordStart);
        capture->trailing = m_text.substr(wordEnd);
        capture->deleteChars = capture->word.size() + capture->trailing.size();
        capture->replaceOffset = wordStart;
        capture->expectedSize = m_text.size();
    }
    return true;
}

bool InputBuffer::IsCaptureCurrent(const PreviousWordCapture& capture) const {
    if (capture.expectedSize != m_text.size() || capture.replaceOffset > m_text.size()) {
        return false;
    }
    const std::wstring expectedTail = capture.word + capture.trailing;
    return m_text.compare(capture.replaceOffset, expectedTail.size(), expectedTail) == 0;
}

bool InputBuffer::CommitReplacement(const PreviousWordCapture& capture, const std::wstring& replacement) {
    if (!IsCaptureCurrent(capture)) {
        return false;
    }
    m_text.replace(capture.replaceOffset, capture.deleteChars, replacement);
    TrimToLimit();
    return true;
}

bool InputBuffer::IsWordSeparator(wchar_t ch) {
    return iswspace(ch) != 0;
}

void InputBuffer::TrimToLimit() {
    if (m_text.size() <= MAX_INPUT_BUFFER_CHARS) {
        return;
    }
    const size_t keepFrom = m_text.size() - MAX_INPUT_BUFFER_CHARS;
    m_text.erase(0, keepFrom);
}
