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

    Expect(Choose(false, true, true) == Type::TrackedInput,
           "valid tracked input must avoid false editor selections");
    Expect(Choose(false, true, false) == Type::Selection,
           "selection remains the fallback without tracked input");
    Expect(Choose(false, false, false) == Type::None,
           "missing selection and tracked input must not fall back to the whole field");
    Expect(Choose(true, true, true) == Type::Clipboard,
           "clipboard-only execution must ignore active-control sources");
    return 0;
}
