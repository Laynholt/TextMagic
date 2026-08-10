#include "InfoWindowModel.h"

#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    Check(SplitLogLines(L"").empty(), "empty log has no rows");
    Check(SplitLogLines(L"one") == std::vector<std::wstring>{L"one"},
          "final line without newline is preserved");
    Check(SplitLogLines(L"one\r\ntwo\r\n") ==
              std::vector<std::wstring>{L"one", L"two"},
          "CRLF is split without a trailing empty row");
    Check(SplitLogLines(L"one\ntwo") ==
              std::vector<std::wstring>{L"one", L"two"},
          "LF input is supported");

    const std::vector<std::wstring> rows{L"zero", L"one", L"two"};
    Check(JoinLogLines(rows, {2, 0, 99, 2}) == L"zero\r\ntwo",
          "selected rows are copied once in visual order");
    Check(JoinLogLines(rows, {}).empty(), "empty selection copies nothing");

    Check(!ShouldShowVerticalScrollbar(4, 24, 96),
          "exactly visible rows do not show the scrollbar");
    Check(ShouldShowVerticalScrollbar(5, 24, 96),
          "overflowing rows show the scrollbar");
    Check(!ShouldShowVerticalScrollbar(5, 0, 96),
          "invalid item height hides the scrollbar");
    return 0;
}
