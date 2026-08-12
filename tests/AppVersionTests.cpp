#include "AppVersion.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    Check(std::wstring_view(TM_APP_VERSION_W) == L"1.1.0",
          "runtime version is generated as 1.1.0");
    Check(TM_VERSION_MAJOR == 1 && TM_VERSION_MINOR == 1 && TM_VERSION_PATCH == 0,
          "numeric version components match 1.1.0");
    Check(TM_VERSION_MAJOR == 1 && TM_VERSION_MINOR == 1
              && TM_VERSION_PATCH == 0 && TM_VERSION_BUILD == 0,
          "Windows file version tuple matches 1.1.0.0");
    return 0;
}
