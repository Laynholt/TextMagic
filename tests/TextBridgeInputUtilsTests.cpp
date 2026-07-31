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

struct FakeSelectionInput {
    bool targetCurrent = true;
    bool shiftHeld = false;
    bool leftHeld = false;
    bool selectionCollapsed = false;
    bool acknowledgeSelection = true;
    bool loseTargetWhileWaiting = false;
    size_t leftPairResult = 2;
    int shiftReleaseFailures = 0;

    TextBridgeInputUtils::SelectionOperations Operations() {
        return {
            [this]() {
                return targetCurrent;
            },
            [this]() {
                shiftHeld = true;
                return true;
            },
            [this]() {
                leftHeld = leftPairResult == 1;
                return leftPairResult;
            },
            [this](size_t) {
                if (loseTargetWhileWaiting) {
                    targetCurrent = false;
                }
                return acknowledgeSelection;
            },
            [this]() {
                leftHeld = false;
                return true;
            },
            [this]() {
                if (shiftReleaseFailures > 0) {
                    --shiftReleaseFailures;
                    return false;
                }
                shiftHeld = false;
                return true;
            },
            [this]() {
                selectionCollapsed = true;
                return true;
            }
        };
    }
};
}

int main() {
    using TextBridgeInputUtils::RunLongSelection;
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

    FakeSelectionInput partialInput;
    partialInput.leftPairResult = 1;
    const auto partialOperations = partialInput.Operations();
    Expect(!RunLongSelection(1, partialOperations),
           "partial Left input must fail the selection transaction");
    Expect(!partialInput.leftHeld && !partialInput.shiftHeld,
           "partial Left input must release held keys");
    Expect(partialInput.selectionCollapsed,
           "partial Left input must collapse the partial selection");

    FakeSelectionInput acknowledgementFailure;
    acknowledgementFailure.acknowledgeSelection = false;
    const auto acknowledgementOperations = acknowledgementFailure.Operations();
    Expect(!RunLongSelection(1, acknowledgementOperations),
           "missing selection acknowledgement must fail closed");
    Expect(!acknowledgementFailure.shiftHeld,
           "acknowledgement failure must release Shift");
    Expect(acknowledgementFailure.selectionCollapsed,
           "acknowledgement failure must collapse the selection");

    FakeSelectionInput modifierReleaseFailure;
    modifierReleaseFailure.acknowledgeSelection = false;
    modifierReleaseFailure.shiftReleaseFailures = 2;
    const auto modifierOperations = modifierReleaseFailure.Operations();
    Expect(!RunLongSelection(1, modifierOperations),
           "modifier release failure must fail closed");
    Expect(modifierReleaseFailure.shiftHeld,
           "failed Shift release must remain observable");
    Expect(!modifierReleaseFailure.selectionCollapsed,
           "selection must not collapse while Shift may remain held");

    FakeSelectionInput changedTarget;
    changedTarget.acknowledgeSelection = false;
    changedTarget.loseTargetWhileWaiting = true;
    const auto changedTargetOperations = changedTarget.Operations();
    Expect(!RunLongSelection(1, changedTargetOperations),
           "foreground change must abort the selection transaction");
    Expect(!changedTarget.shiftHeld,
           "foreground change must still release Shift");
    Expect(!changedTarget.selectionCollapsed,
           "foreground change must not move the caret in the new target");
    return 0;
}
