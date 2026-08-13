#include "BackgroundTask.h"
#include "ModalMessageLoop.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    std::promise<std::exception_ptr> taskFailure;
    auto taskFailureFuture = taskFailure.get_future();
    const bool started = BackgroundTask::StartDetached(
        []() { throw std::runtime_error("worker failure"); },
        [&](std::exception_ptr error) { taskFailure.set_value(error); }
    );
    Expect(started, "background worker must start");
    Expect(taskFailureFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
           "background exception must reach the failure handler");
    try {
        std::rethrow_exception(taskFailureFuture.get());
    } catch (const std::runtime_error& error) {
        Expect(std::string(error.what()) == "worker failure",
               "background failure must preserve the original exception");
    }

    int dispatchCount = 0;
    int repostedQuitCode = -1;
    MSG quitMessage = {};
    quitMessage.wParam = 23;
    const ModalMessageLoopOperations quitOperations{
        []() { return true; },
        [&](MSG& message) {
            message = quitMessage;
            return 0;
        },
        [](HWND, MSG*) { return FALSE; },
        [](const MSG*) {},
        [&](const MSG*) { ++dispatchCount; },
        [&](int quitCode) { repostedQuitCode = quitCode; },
        []() { return static_cast<DWORD>(0); }
    };
    const ModalMessageLoopResult quitResult = RunModalMessageLoop(nullptr, quitOperations);
    Expect(quitResult == ModalMessageLoopResult::Quit,
           "WM_QUIT must stop a modal message loop");
    Expect(repostedQuitCode == 23,
           "WM_QUIT must be reposted for the outer application loop");
    Expect(dispatchCount == 0,
           "WM_QUIT must not be dispatched as a regular message");

    DWORD reportedError = 0;
    const ModalMessageLoopOperations errorOperations{
        []() { return true; },
        [](MSG&) { return -1; },
        [](HWND, MSG*) { return FALSE; },
        [](const MSG*) {},
        [&](const MSG*) { ++dispatchCount; },
        [](int) {},
        []() { return static_cast<DWORD>(87); }
    };
    const ModalMessageLoopResult errorResult =
        RunModalMessageLoop(nullptr, errorOperations, &reportedError);
    Expect(errorResult == ModalMessageLoopResult::Error,
           "GetMessageW failure must stop a modal message loop");
    Expect(reportedError == 87,
           "GetMessageW failure must preserve the Win32 error");
    Expect(dispatchCount == 0,
           "GetMessageW failure must not dispatch a stale message");
    return 0;
}
