#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace TextBridgeInputUtils {
constexpr int WAIT_INDEFINITELY = 0;

struct ModifierWaitOperations {
    std::function<bool()> isTargetCurrent;
    std::function<bool()> areModifiersPressed;
    std::function<void()> pause;
};

inline bool WaitForModifiersRelease(
    int maxAttempts,
    const ModifierWaitOperations& operations
) {
    for (int attempt = 0;
         maxAttempts == WAIT_INDEFINITELY || attempt < maxAttempts;
         ++attempt) {
        if (!operations.isTargetCurrent()) {
            return false;
        }
        if (!operations.areModifiersPressed()) {
            return operations.isTargetCurrent();
        }
        operations.pause();
    }
    return false;
}

inline bool WaitForInputReady(
    bool inputBufferMode,
    const std::function<bool()>& waitForModifiersRelease
) {
    return !inputBufferMode || waitForModifiersRelease();
}

struct AtomicReplacementOperations {
    std::function<bool()> isTargetCurrent;
    std::function<bool(size_t, const std::wstring&)> sendBatch;
};

inline bool RunAtomicReplacement(
    size_t deleteCount,
    const std::wstring& replacement,
    const AtomicReplacementOperations& operations
) {
    return operations.isTargetCurrent()
        && operations.sendBatch(deleteCount, replacement)
        && operations.isTargetCurrent();
}

}
