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
    using TextBridgeInputUtils::PlanPartialSelectionCleanup;

    const auto noneInserted = PlanPartialSelectionCleanup(0, 6);
    Expect(!noneInserted.releaseLeft && !noneInserted.releaseShift
               && !noneInserted.collapseSelection,
           "zero inserted events must not move the caret");

    const auto shiftOnly = PlanPartialSelectionCleanup(1, 6);
    Expect(!shiftOnly.releaseLeft && shiftOnly.releaseShift
               && !shiftOnly.collapseSelection,
           "Shift-only insertion must only release Shift");

    const auto leftHeld = PlanPartialSelectionCleanup(2, 6);
    Expect(leftHeld.releaseLeft && leftHeld.releaseShift
               && leftHeld.collapseSelection,
           "partial Left-down insertion must release both keys and restore the caret");

    const auto completeLeft = PlanPartialSelectionCleanup(3, 6);
    Expect(!completeLeft.releaseLeft && completeLeft.releaseShift
               && completeLeft.collapseSelection,
           "complete Left pair must release Shift and restore the caret");

    const auto secondLeftHeld = PlanPartialSelectionCleanup(4, 6);
    Expect(secondLeftHeld.releaseLeft && secondLeftHeld.releaseShift
               && secondLeftHeld.collapseSelection,
           "every partial Left-down event must be released");

    const auto success = PlanPartialSelectionCleanup(6, 6);
    Expect(!success.releaseLeft && !success.releaseShift && !success.collapseSelection,
           "complete batches must not schedule failure cleanup");
    return 0;
}
