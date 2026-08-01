#include "InputBuffer.h"

#include <cwctype>

namespace {
constexpr size_t MAX_INPUT_BUFFER_CHARS = 20000;
}

void InputBuffer::Clear() {
    m_text.clear();
    m_contextId = 0;
    ++m_generation;
}

void InputBuffer::PopCharacter(ContextId contextId) {
    SwitchContext(contextId);
    if (!m_text.empty()) {
        m_text.pop_back();
        ++m_generation;
    }
}

void InputBuffer::AppendText(ContextId contextId, const std::wstring& text) {
    if (text.empty() || contextId == 0) {
        return;
    }
    std::wstring printable;
    for (const wchar_t ch : text) {
        if (!iswcntrl(ch)) {
            printable.push_back(ch);
        }
    }
    if (printable.empty()) {
        return;
    }
    SwitchContext(contextId);
    m_text += printable;
    ++m_generation;
    ClearIfOverLimit();
}

bool InputBuffer::TryPeekPreviousWord(ContextId contextId, PreviousWordCapture* capture) const {
    if (capture) {
        *capture = PreviousWordCapture();
    }
    if (contextId == 0 || contextId != m_contextId || m_text.empty()) {
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
        capture->contextId = contextId;
        capture->generation = m_generation;
    }
    return true;
}

bool InputBuffer::TryPeekAllText(ContextId contextId, PreviousWordCapture* capture) const {
    if (capture) {
        *capture = PreviousWordCapture();
    }
    if (contextId == 0 || contextId != m_contextId || m_text.empty()) {
        return false;
    }
    if (capture) {
        capture->word = m_text;
        capture->deleteChars = m_text.size();
        capture->replaceOffset = 0;
        capture->expectedSize = m_text.size();
        capture->contextId = contextId;
        capture->generation = m_generation;
    }
    return true;
}

bool InputBuffer::IsCaptureCurrent(ContextId contextId, const PreviousWordCapture& capture) const {
    if (contextId == 0 || contextId != m_contextId || contextId != capture.contextId ||
        capture.generation != m_generation || capture.expectedSize != m_text.size() ||
        capture.replaceOffset > m_text.size()) {
        return false;
    }
    const std::wstring expectedTail = capture.word + capture.trailing;
    return m_text.compare(capture.replaceOffset, expectedTail.size(), expectedTail) == 0;
}

bool InputBuffer::CommitReplacement(ContextId contextId,
                                    const PreviousWordCapture& capture,
                                    const std::wstring& replacement) {
    if (!IsCaptureCurrent(contextId, capture)) {
        return false;
    }
    m_text.replace(capture.replaceOffset, capture.deleteChars, replacement);
    ++m_generation;
    ClearIfOverLimit();
    return true;
}

void InputBuffer::SwitchContext(ContextId contextId) {
    if (m_contextId == contextId) {
        return;
    }
    m_text.clear();
    m_contextId = contextId;
    ++m_generation;
}

bool InputBuffer::IsWordSeparator(wchar_t ch) {
    return iswspace(ch) != 0;
}

void InputBuffer::ClearIfOverLimit() {
    if (m_text.size() > MAX_INPUT_BUFFER_CHARS) {
        Clear();
    }
}
