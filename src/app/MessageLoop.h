#pragma once

#include <windows.h>

enum class MessageReadResult {
    Dispatch,
    Quit,
    Error
};

constexpr MessageReadResult ClassifyMessageRead(BOOL value) noexcept {
    return value > 0 ? MessageReadResult::Dispatch
        : value == 0 ? MessageReadResult::Quit
                     : MessageReadResult::Error;
}
