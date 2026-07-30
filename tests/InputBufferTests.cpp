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
    InputBuffer buffer;
    buffer.AppendText(L"hello world   ");

    InputBuffer::PreviousWordCapture capture;
    Expect(buffer.TryPeekPreviousWord(&capture), "expected previous word capture");
    Expect(capture.word == L"world", "expected word before trailing spaces");
    Expect(capture.trailing == L"   ", "expected trailing spaces to be preserved");
    Expect(capture.deleteChars == 8, "expected delete count to include word and trailing spaces");
    Expect(buffer.TextForTest() == L"hello world   ", "peek must not mutate the buffer");

    Expect(buffer.CommitReplacement(capture, L"WORLD   "), "expected replacement commit");
    Expect(buffer.TextForTest() == L"hello WORLD   ", "expected committed replacement in buffer");

    Expect(buffer.TryPeekPreviousWord(&capture), "expected second previous word capture");
    buffer.AppendText(L"again");
    Expect(!buffer.IsCaptureCurrent(capture), "capture must become stale after further input");
    Expect(!buffer.CommitReplacement(capture, L"IGNORED"), "stale capture must not commit");
    Expect(buffer.TextForTest() == L"hello WORLD   again", "stale commit must not mutate buffer");

    buffer.Clear();
    buffer.AppendText(L"abc ghbdtn ");
    Expect(buffer.TryPeekPreviousWord(&capture), "expected qwerty word capture");
    const std::wstring ruHello = L"\u043f\u0440\u0438\u0432\u0435\u0442 ";
    Expect(buffer.CommitReplacement(capture, ruHello), "expected first layout replacement");
    Expect(buffer.TextForTest() == L"abc " + ruHello, "expected ru replacement");
    Expect(buffer.TryPeekPreviousWord(&capture), "expected ru word capture");
    Expect(capture.deleteChars == 7, "expected ru delete count to include trailing space only once");
    Expect(buffer.CommitReplacement(capture, L"ghbdtn "), "expected second layout replacement");
    Expect(buffer.TextForTest() == L"abc ghbdtn ", "expected en replacement without left drift");

    buffer.Clear();
    buffer.AppendText(L"   ");
    Expect(!buffer.TryPeekPreviousWord(&capture), "separator-only buffer must not capture a word");

    return 0;
}
