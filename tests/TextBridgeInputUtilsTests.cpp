#include "TextBridgeInputUtils.h"

#include <cstdlib>
#include <iostream>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}
}

int main() {
    using TextBridgeInputUtils::ShouldSelectBeforeDelete;
    using TextBridgeInputUtils::ShouldWaitForSelectionConsumption;
    Expect(!ShouldSelectBeforeDelete(100),
           "100 characters must use direct Backspace");
    Expect(ShouldSelectBeforeDelete(101),
           "101 characters must use selection");
    Expect(!ShouldWaitForSelectionConsumption(31, 101),
           "selection must continue before a full chunk");
    Expect(ShouldWaitForSelectionConsumption(32, 101),
           "selection must wait after a full chunk");
    Expect(!ShouldWaitForSelectionConsumption(33, 101),
           "selection must resume after acknowledged chunks");
    Expect(ShouldWaitForSelectionConsumption(101, 101),
           "selection must wait for final input consumption");
    return 0;
}
