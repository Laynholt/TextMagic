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
    Expect(!ShouldSelectBeforeDelete(100),
           "100 characters must use direct Backspace");
    Expect(ShouldSelectBeforeDelete(101),
           "101 characters must use selection");
    return 0;
}
