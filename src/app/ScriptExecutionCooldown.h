#pragma once

#include <cstdint>

namespace ScriptExecutionCooldown {
constexpr std::uint64_t DurationMs = 250;

constexpr bool CanStart(std::uint64_t lastCompletion, std::uint64_t now) noexcept {
    return lastCompletion == 0 || now - lastCompletion >= DurationMs;
}
}
