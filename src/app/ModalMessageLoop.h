#pragma once

#include "MessageLoop.h"

#include <windows.h>

#include <functional>

enum class ModalMessageLoopResult {
    Closed,
    Quit,
    Error,
};

struct ModalMessageLoopOperations {
    std::function<bool()> isWindow;
    std::function<int(MSG&)> getMessage;
    std::function<BOOL(HWND, MSG*)> isDialogMessage;
    std::function<void(const MSG*)> translateMessage;
    std::function<void(const MSG*)> dispatchMessage;
    std::function<void(int)> repostQuit;
    std::function<DWORD()> getLastError;
};

inline ModalMessageLoopResult RunModalMessageLoop(
    HWND dialog,
    const ModalMessageLoopOperations& operations,
    DWORD* errorCode = nullptr
) {
    MSG message = {};
    while (operations.isWindow()) {
        const int readResult = operations.getMessage(message);
        switch (ClassifyMessageRead(readResult)) {
        case MessageReadResult::Error:
            if (errorCode) {
                *errorCode = operations.getLastError();
            }
            return ModalMessageLoopResult::Error;
        case MessageReadResult::Quit:
            operations.repostQuit(static_cast<int>(message.wParam));
            return ModalMessageLoopResult::Quit;
        case MessageReadResult::Dispatch:
            if (!operations.isDialogMessage(dialog, &message)) {
                operations.translateMessage(&message);
                operations.dispatchMessage(&message);
            }
            break;
        }
    }
    return ModalMessageLoopResult::Closed;
}

inline ModalMessageLoopResult RunModalMessageLoop(HWND dialog, DWORD* errorCode = nullptr) {
    const ModalMessageLoopOperations operations{
        [dialog]() { return IsWindow(dialog) != FALSE; },
        [](MSG& message) { return static_cast<int>(GetMessageW(&message, nullptr, 0, 0)); },
        [](HWND window, MSG* message) { return IsDialogMessageW(window, message); },
        [](const MSG* message) { TranslateMessage(message); },
        [](const MSG* message) { DispatchMessageW(message); },
        [](int quitCode) { PostQuitMessage(quitCode); },
        []() { return GetLastError(); }
    };
    return RunModalMessageLoop(dialog, operations, errorCode);
}
