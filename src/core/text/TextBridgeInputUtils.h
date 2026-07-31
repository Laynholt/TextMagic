#pragma once

#include <cstddef>
#include <functional>

namespace TextBridgeInputUtils {
constexpr size_t DIRECT_BACKSPACE_LIMIT = 100;
constexpr size_t SELECTION_CHUNK_SIZE = 32;

constexpr bool ShouldSelectBeforeDelete(size_t count) noexcept {
    return count > DIRECT_BACKSPACE_LIMIT;
}

constexpr bool ShouldWaitForSelectionConsumption(size_t selected, size_t total) noexcept {
    return selected > 0 && (selected % SELECTION_CHUNK_SIZE == 0 || selected == total);
}

struct SelectionOperations {
    std::function<bool()> isTargetCurrent;
    std::function<bool()> pressShift;
    std::function<size_t()> sendLeftPair;
    std::function<bool(size_t)> waitForSelection;
    std::function<bool()> releaseLeft;
    std::function<bool()> releaseShift;
    std::function<bool()> collapseSelection;
};

inline bool RunLongSelection(size_t count, const SelectionOperations& operations) {
    if (count == 0) {
        return operations.isTargetCurrent();
    }
    if (!operations.isTargetCurrent() || !operations.pressShift()) {
        return false;
    }

    const auto releaseTwice = [](const std::function<bool()>& release) {
        return release() || release();
    };
    const auto fail = [&](bool leftMayBeDown, bool hasSelection) {
        const bool leftReleased = !leftMayBeDown || releaseTwice(operations.releaseLeft);
        const bool shiftReleased = releaseTwice(operations.releaseShift);
        if (leftReleased && shiftReleased && hasSelection
            && operations.isTargetCurrent()) {
            operations.collapseSelection();
        }
        return false;
    };

    size_t selected = 0;
    while (selected < count) {
        if (!operations.isTargetCurrent()) {
            return fail(false, selected > 0);
        }

        const size_t sent = operations.sendLeftPair();
        if (sent != 2) {
            return fail(sent > 0, selected > 0 || sent > 0);
        }
        ++selected;

        if (ShouldWaitForSelectionConsumption(selected, count)) {
            if (!operations.waitForSelection(selected)
                || !operations.isTargetCurrent()) {
                return fail(false, true);
            }
        }
    }

    if (!operations.isTargetCurrent()) {
        return fail(false, true);
    }
    if (!operations.releaseShift()) {
        const bool shiftReleased = operations.releaseShift();
        if (shiftReleased && operations.isTargetCurrent()) {
            operations.collapseSelection();
        }
        return false;
    }
    return operations.isTargetCurrent();
}
}
