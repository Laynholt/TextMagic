#pragma once

#include <exception>
#include <thread>
#include <utility>

namespace BackgroundTask {
template <typename Work, typename FailureHandler>
bool StartDetached(Work&& work, FailureHandler&& failureHandler) noexcept {
    try {
        std::thread(
            [task = std::forward<Work>(work),
             onFailure = std::forward<FailureHandler>(failureHandler)]() mutable noexcept {
                try {
                    task();
                } catch (...) {
                    try {
                        onFailure(std::current_exception());
                    } catch (...) {
                    }
                }
            }
        ).detach();
        return true;
    } catch (...) {
        return false;
    }
}
}
