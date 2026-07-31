#include "ScriptInputSource.h"

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
    using ScriptInputSource::Choose;
    using ScriptInputSource::Type;

    Expect(Choose(false, true, true) == Type::Selection,
           "explicit selection must take precedence over tracked input");
    Expect(Choose(false, false, true) == Type::TrackedInput,
           "tracked input must be the no-selection fallback");
    Expect(Choose(false, false, false) == Type::None,
           "missing selection and tracked input must not fall back to the whole field");
    Expect(Choose(true, true, true) == Type::Clipboard,
           "clipboard-only execution must ignore active-control sources");
    return 0;
}
