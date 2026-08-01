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
    int replacementBatches = 0;
    size_t deletedCharacters = 0;
    std::wstring insertedText;
    const TextBridgeInputUtils::AtomicReplacementOperations replacementOperations{
        []() { return true; },
        [&](size_t deleteCount, const std::wstring& replacement) {
            ++replacementBatches;
            deletedCharacters = deleteCount;
            insertedText = replacement;
            return true;
        }
    };
    Expect(TextBridgeInputUtils::RunAtomicReplacement(
               101, L"replacement", replacementOperations),
           "atomic replacement must succeed");
    Expect(replacementBatches == 1,
           "delete and insert must use one input batch");
    Expect(deletedCharacters == 101 && insertedText == L"replacement",
           "the atomic batch must contain the complete replacement");

    return 0;
}
