#include "OutputLayout.h"

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
    Check(DetectOutputLayout(L"hello") == OutputLayout::English,
          "Latin output selects English");
    Check(DetectOutputLayout(L"\u043F\u0440\u0438\u0432\u0435\u0442") == OutputLayout::Russian,
          "Cyrillic output selects Russian");
    Check(DetectOutputLayout(L"123 !") == OutputLayout::Unchanged,
          "non-letters keep the layout");
    Check(DetectOutputLayout(L"abc\u0430\u0431\u0432") == OutputLayout::Unchanged,
          "equal alphabets keep the layout");
    Check(DetectOutputLayout(L"hello \u043C\u0438\u0440") == OutputLayout::English,
          "Latin majority selects English");
    Check(DetectOutputLayout(L"hello \u043F\u0440\u0438\u0432\u0435\u0442") == OutputLayout::Russian,
          "Cyrillic majority selects Russian");
    Check(ChooseOutputLayoutForAppliedScript(
              true, false, true, L"\u043F\u0440\u0438\u0432\u0435\u0442") == OutputLayout::Russian,
          "successful in-place opt-in selects the output language");
    Check(ChooseOutputLayoutForAppliedScript(
              false, false, true, L"\u043F\u0440\u0438\u0432\u0435\u0442") == OutputLayout::Unchanged,
          "scripts must opt in");
    Check(ChooseOutputLayoutForAppliedScript(
              true, true, true, L"\u043F\u0440\u0438\u0432\u0435\u0442") == OutputLayout::Unchanged,
          "clipboard mode never switches layout");
    Check(ChooseOutputLayoutForAppliedScript(
              true, false, false, L"\u043F\u0440\u0438\u0432\u0435\u0442") == OutputLayout::Unchanged,
          "failed replacement never switches layout");

    const HKL english = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x00000409));
    const HKL russian = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x00000419));
    Check(FindInstalledOutputLayout(OutputLayout::English, { russian, english }) == english,
          "English selects an installed English layout");
    Check(FindInstalledOutputLayout(OutputLayout::Russian, { english, russian }) == russian,
          "Russian selects an installed Russian layout");
    Check(FindInstalledOutputLayout(OutputLayout::English, { russian }) == nullptr,
          "a missing target language keeps the current layout");
    return 0;
}
