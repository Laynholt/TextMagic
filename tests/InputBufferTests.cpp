#include "InputBuffer.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}
}

int main() {
    constexpr InputBuffer::ContextId editorContext = 1;
    constexpr InputBuffer::ContextId terminalContext = 2;

    InputBuffer buffer;
    buffer.AppendText(editorContext, L"hello world   ");

    InputBuffer::PreviousWordCapture capture;
    Expect(buffer.TryPeekPreviousWord(editorContext, &capture), "expected previous word capture");
    Expect(capture.word == L"world", "expected word before trailing spaces");
    Expect(capture.trailing == L"   ", "expected trailing spaces to be preserved");
    Expect(capture.deleteChars == 8, "expected delete count to include word and trailing spaces");
    Expect(buffer.IsCaptureCurrent(editorContext, capture), "expected current editor capture");
    Expect(!buffer.IsCaptureCurrent(terminalContext, capture), "capture must be bound to its input context");
    Expect(buffer.TextForTest() == L"hello world   ", "peek must not mutate the buffer");

    Expect(buffer.CommitReplacement(editorContext, capture, L"WORLD   "), "expected replacement commit");
    Expect(buffer.TextForTest() == L"hello WORLD   ", "expected committed replacement in buffer");

    Expect(buffer.TryPeekPreviousWord(editorContext, &capture), "expected second previous word capture");
    buffer.AppendText(editorContext, L"again");
    Expect(!buffer.IsCaptureCurrent(editorContext, capture), "capture must become stale after further input");
    Expect(!buffer.CommitReplacement(editorContext, capture, L"IGNORED"), "stale capture must not commit");
    Expect(buffer.TextForTest() == L"hello WORLD   again", "stale commit must not mutate buffer");

    buffer.Clear();
    buffer.AppendText(editorContext, L"abc ghbdtn ");
    Expect(buffer.TryPeekPreviousWord(editorContext, &capture), "expected qwerty word capture");
    const std::wstring ruHello = L"\u043f\u0440\u0438\u0432\u0435\u0442 ";
    Expect(buffer.CommitReplacement(editorContext, capture, ruHello), "expected first layout replacement");
    Expect(buffer.TextForTest() == L"abc " + ruHello, "expected ru replacement");
    Expect(buffer.TryPeekPreviousWord(editorContext, &capture), "expected ru word capture");
    Expect(capture.deleteChars == 7, "expected ru delete count to include trailing space only once");
    Expect(buffer.CommitReplacement(editorContext, capture, L"ghbdtn "), "expected second layout replacement");
    Expect(buffer.TextForTest() == L"abc ghbdtn ", "expected en replacement without left drift");

    buffer.Clear();
    buffer.AppendText(editorContext, L"editor ");
    Expect(buffer.TryPeekPreviousWord(editorContext, &capture), "expected second editor capture");
    buffer.AppendText(terminalContext, L"terminal ");
    Expect(buffer.TextForTest() == L"terminal ", "context switch must start a fresh session");
    Expect(!buffer.IsCaptureCurrent(editorContext, capture), "old-window capture must be stale");

    Expect(buffer.TryPeekPreviousWord(terminalContext, &capture), "expected terminal capture");
    Expect(buffer.CommitReplacement(terminalContext, capture, L"TERMINAL "),
           "expected terminal replacement commit");
    Expect(buffer.TextForTest() == L"TERMINAL ", "expected committed terminal text");

    buffer.Clear();
    Expect(!buffer.IsCaptureCurrent(terminalContext, capture), "clear must stale captures");
    buffer.AppendText(editorContext, L"   ");
    Expect(!buffer.TryPeekPreviousWord(editorContext, &capture), "separator-only buffer must not capture a word");

    buffer.Clear();
    buffer.AppendText(editorContext, L"one two three");
    Expect(buffer.TryPeekAllText(editorContext, &capture), "expected all-text capture");
    Expect(capture.word == L"one two three", "all-text capture must contain the complete session");
    Expect(capture.trailing.empty(), "all-text capture must not add trailing text");
    Expect(capture.deleteChars == 13 && capture.replaceOffset == 0,
           "all-text capture must replace the complete session");
    Expect(buffer.CommitReplacement(editorContext, capture, L"ONE TWO THREE"),
           "expected all-text replacement commit");
    Expect(buffer.TextForTest() == L"ONE TWO THREE", "expected committed all-text replacement");

    buffer.PopCharacter(editorContext);
    Expect(buffer.TextForTest() == L"ONE TWO THRE", "Backspace must remove one tracked character");

    buffer.Clear();
    buffer.AppendText(editorContext, std::wstring(20000, L'x'));
    Expect(buffer.TextForTest().size() == 20000, "20,000 characters must remain tracked");
    buffer.AppendText(editorContext, L"y");
    Expect(buffer.TextForTest().empty(), "overflow must clear the whole session");
    buffer.AppendText(editorContext, L"z");
    Expect(buffer.TextForTest() == L"z", "typing after overflow must start a new session");

    buffer.Clear();
    buffer.AppendText(editorContext, std::wstring(L"abc") + wchar_t{3} + L"def");
    Expect(buffer.TextForTest() == L"abcdef",
           "control characters from keyboard translation must not enter tracked text");

    return 0;
}
