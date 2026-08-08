#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace TextBridgeInputUtils {
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
