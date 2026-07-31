#pragma once

#include <cstddef>

namespace TextBridgeInputUtils {
constexpr size_t DIRECT_BACKSPACE_LIMIT = 100;

constexpr bool ShouldSelectBeforeDelete(size_t count) noexcept {
    return count > DIRECT_BACKSPACE_LIMIT;
}
}
