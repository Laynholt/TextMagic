#include "ScriptExecutionCooldown.h"

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
    Expect(ScriptExecutionCooldown::CanStart(0, 100),
           "the first hotkey execution must start immediately");
    Expect(!ScriptExecutionCooldown::CanStart(100, 349),
           "hotkey spam inside the cooldown must be ignored");
    Expect(ScriptExecutionCooldown::CanStart(100, 350),
           "the hotkey must work after the short cooldown");
    return 0;
}
