#include "CompletionDelivery.h"
#include "OutputLayout.h"
#include "MessageLoop.h"

#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

struct TrackedCompletion {
    explicit TrackedCompletion(int* destructionCount)
        : destructionCount(destructionCount) {}
    ~TrackedCompletion() { ++*destructionCount; }

    int* destructionCount;
};
}

int main() {
    Check(CompletionOwnershipTransferred(true, kCompletionHandled),
          "the handled sentinel transfers completion ownership");
    Check(!CompletionOwnershipTransferred(false, kCompletionHandled),
          "delivery failure retains completion ownership");
    Check(!CompletionOwnershipTransferred(false, 0),
          "delivery timeout retains completion ownership");
    Check(!CompletionOwnershipTransferred(true, 0),
          "an unhandled delivery retains completion ownership");

    CompletionRegistry completions;
    int destructionCount = 0;
    const std::uintptr_t handledToken = completions.Store(
        std::make_unique<TrackedCompletion>(&destructionCount));
    Check(handledToken != 0, "an enabled registry issues a nonzero token");
    auto handledPayload = completions.Take<TrackedCompletion>(handledToken);
    Check(handledPayload != nullptr,
          "the handler atomically takes a registered payload");
    Check(!completions.Remove(handledToken),
          "sender cleanup cannot reclaim a handler-owned payload");
    handledPayload.reset();
    Check(destructionCount == 1, "a handler-owned payload is destroyed once");

    const std::uintptr_t timedOutToken = completions.Store(
        std::make_unique<TrackedCompletion>(&destructionCount));
    Check(completions.Remove(timedOutToken),
          "sender cleanup reclaims an unhandled timed-out payload");
    Check(!completions.Take<TrackedCompletion>(timedOutToken),
          "a late handler cannot reclaim a sender-owned payload");
    Check(destructionCount == 2, "a timed-out payload is destroyed once");

    const std::uintptr_t pendingToken = completions.Store(
        std::make_unique<TrackedCompletion>(&destructionCount));
    Check(pendingToken != 0, "a pending payload is registered before shutdown");
    completions.DisableAndClear();
    Check(destructionCount == 3, "shutdown destroys pending payloads");
    Check(completions.Store(std::make_unique<TrackedCompletion>(&destructionCount)) == 0,
          "shutdown rejects new completion payloads");
    Check(destructionCount == 4, "a rejected payload remains locally owned and is destroyed");

    Check(ClassifyMessageRead(1) == MessageReadResult::Dispatch,
          "positive GetMessageW result dispatches");
    Check(ClassifyMessageRead(0) == MessageReadResult::Quit,
          "zero GetMessageW result quits");
    Check(ClassifyMessageRead(-1) == MessageReadResult::Error,
          "negative GetMessageW result reports an error");

    Check(DetectOutputLayout(L"hello") == OutputLayout::English,
          "Latin output selects English");
    Check(DetectOutputLayout(L"\u043F\u0440\u0438\u0432\u0435\u0442") == OutputLayout::Russian,
          "Cyrillic output selects Russian");
    Check(DetectOutputLayout(L"123 !") == OutputLayout::Unchanged,
          "non-letters keep the layout");
    Check(DetectOutputLayout(L"abc\u0430\u0431\u0432") == OutputLayout::Unchanged,
          "equal alphabets keep the layout");
    Check(DetectOutputLayout(L"hello \u043C\u0438\u0440") == OutputLayout::English,
          "Latin majority selects English");
    Check(DetectOutputLayout(L"hello \u043F\u0440\u0438\u0432\u0435\u0442") == OutputLayout::Russian,
          "Cyrillic majority selects Russian");
    Check(ChooseOutputLayoutForAppliedScript(
              true, false, true, L"\u043F\u0440\u0438\u0432\u0435\u0442") == OutputLayout::Russian,
          "successful in-place opt-in selects the output language");
    Check(ChooseOutputLayoutForAppliedScript(
              false, false, true, L"\u043F\u0440\u0438\u0432\u0435\u0442") == OutputLayout::Unchanged,
          "scripts must opt in");
    Check(ChooseOutputLayoutForAppliedScript(
              true, true, true, L"\u043F\u0440\u0438\u0432\u0435\u0442") == OutputLayout::Unchanged,
          "clipboard mode never switches layout");
    Check(ChooseOutputLayoutForAppliedScript(
              true, false, false, L"\u043F\u0440\u0438\u0432\u0435\u0442") == OutputLayout::Unchanged,
          "failed replacement never switches layout");

    const HKL english = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x00000409));
    const HKL russian = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x00000419));
    Check(FindInstalledOutputLayout(OutputLayout::English, { russian, english }) == english,
          "English selects an installed English layout");
    Check(FindInstalledOutputLayout(OutputLayout::Russian, { english, russian }) == russian,
          "Russian selects an installed Russian layout");
    Check(FindInstalledOutputLayout(OutputLayout::English, { russian }) == nullptr,
          "a missing target language keeps the current layout");

    const HKL german = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x00000407));
    Check(FindNextInstalledLayout(english, { english, russian, german }) == russian,
          "cycle selects the next installed layout");
    Check(FindNextInstalledLayout(german, { english, russian, german }) == english,
          "cycle wraps to the first installed layout");
    Check(FindNextInstalledLayout(english, { english }) == nullptr,
          "one installed layout is a no-op");
    Check(FindNextInstalledLayout(german, { english, russian }) == nullptr,
          "missing current layout is a no-op");
    return 0;
}
