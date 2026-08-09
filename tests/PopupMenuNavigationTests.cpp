#include "PopupMenuNavigation.h"

#include <cstdlib>
#include <iostream>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    const std::vector<UiRenderer::PopupMenuItem> items{
        { 10, L"First", false, false, false },
        { 0, L"", true, false, false },
        { 20, L"Second", false, false, true }
    };

    Expect(PopupMenuNavigation::MoveSelection(items, 0, 1) == 10,
           "down selects the first command");
    Expect(PopupMenuNavigation::MoveSelection(items, 10, 1) == 20,
           "down skips separators");
    Expect(PopupMenuNavigation::MoveSelection(items, 20, 1) == 10,
           "down wraps to the first command");
    Expect(PopupMenuNavigation::MoveSelection(items, 10, -1) == 20,
           "up wraps to the last command");
    return 0;
}
