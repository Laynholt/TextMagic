#pragma once

#include <cstddef>

namespace TextBridgeInputUtils {
struct SelectionCleanupPlan {
    bool releaseLeft = false;
    bool releaseShift = false;
    bool collapseSelection = false;
};

constexpr SelectionCleanupPlan PlanPartialSelectionCleanup(
    size_t insertedInputCount,
    size_t totalInputCount
) noexcept {
    if (insertedInputCount >= totalInputCount) {
        return {};
    }
    return {
        insertedInputCount > 1 && insertedInputCount % 2 == 0,
        insertedInputCount > 0,
        insertedInputCount > 1
    };
}
}
