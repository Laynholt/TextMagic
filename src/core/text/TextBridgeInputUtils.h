#pragma once

#include <cstddef>

namespace TextBridgeInputUtils {
constexpr size_t DIRECT_BACKSPACE_LIMIT = 100;
constexpr size_t SELECTION_CHUNK_SIZE = 32;

constexpr bool ShouldSelectBeforeDelete(size_t count) noexcept {
    return count > DIRECT_BACKSPACE_LIMIT;
}

constexpr bool ShouldWaitForSelectionConsumption(size_t selected, size_t total) noexcept {
    return selected > 0 && (selected % SELECTION_CHUNK_SIZE == 0 || selected == total);
}
}
