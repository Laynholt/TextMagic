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
    int modifierWaitPauses = 0;
    const TextBridgeInputUtils::ModifierWaitOperations modifierWaitOperations{
        []() { return true; },
        [&]() { return modifierWaitPauses < 80; },
        [&]() { ++modifierWaitPauses; }
    };
    Expect(TextBridgeInputUtils::WaitForModifiersRelease(
               TextBridgeInputUtils::WAIT_INDEFINITELY,
               modifierWaitOperations),
           "unbounded input replacement wait must survive held modifiers");
    Expect(modifierWaitPauses == 80,
           "input replacement must resume when modifiers are released");

    std::string completionOrder;
    const bool inputReady = TextBridgeInputUtils::WaitForInputReady(
        true,
        [&]() {
            completionOrder += "wait";
            return true;
        }
    );
    completionOrder += "complete";
    Expect(inputReady && completionOrder == "waitcomplete",
           "modifier release must be checked before input-buffer completion");

    bool unnecessaryWaitCalled = false;
    Expect(TextBridgeInputUtils::WaitForInputReady(
               false,
               [&]() {
                   unnecessaryWaitCalled = true;
                   return false;
               }),
           "non-input-buffer completion must be ready immediately");
    Expect(!unnecessaryWaitCalled,
           "non-input-buffer completion must not wait for modifiers");

    int replacementBatches = 0;
    size_t deletedCharacters = 0;
    std::wstring insertedText;
    const TextBridgeInputUtils::ReplacementOperations replacementOperations{
        []() { return true; },
        [&](size_t deleteCount, const std::wstring& replacement) {
            ++replacementBatches;
            deletedCharacters = deleteCount;
            insertedText = replacement;
            return TextBridgeInputUtils::InputBatchResult::Complete;
        },
        []() { return true; }
    };
    Expect(TextBridgeInputUtils::RunRecoverableReplacement(
               101, L"replacement", replacementOperations),
           "recoverable replacement must succeed");
    Expect(replacementBatches == 1,
           "delete and insert must use one input batch");
    Expect(deletedCharacters == 101 && insertedText == L"replacement",
           "the input batch must contain the complete replacement");

    int rollbackAttempts = 0;
    const TextBridgeInputUtils::ReplacementOperations partialOperations{
        []() { return true; },
        [](size_t, const std::wstring&) {
            return TextBridgeInputUtils::InputBatchResult::Partial;
        },
        [&]() {
            ++rollbackAttempts;
            return true;
        }
    };
    Expect(!TextBridgeInputUtils::RunRecoverableReplacement(
               3, L"replacement", partialOperations),
           "a partially delivered replacement must fail");
    Expect(rollbackAttempts == 1,
           "a partially delivered replacement must attempt exactly one rollback");

    int unsentRollbackAttempts = 0;
    const TextBridgeInputUtils::ReplacementOperations unsentOperations{
        []() { return true; },
        [](size_t, const std::wstring&) {
            return TextBridgeInputUtils::InputBatchResult::NotSent;
        },
        [&]() {
            ++unsentRollbackAttempts;
            return true;
        }
    };
    Expect(!TextBridgeInputUtils::RunRecoverableReplacement(
               3, L"replacement", unsentOperations),
           "an undelivered replacement must fail");
    Expect(unsentRollbackAttempts == 0,
           "an undelivered replacement must not alter the target with rollback");

    Expect(TextBridgeInputUtils::SelectionDeleteCount(L"text") == 0,
           "typing text must replace the current selection");
    Expect(TextBridgeInputUtils::SelectionDeleteCount(L"") == 1,
           "an empty replacement must delete the current selection");

    return 0;
}
