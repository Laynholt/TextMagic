#include "ScriptExecutionGate.h"

#include <windows.h>

#include <cstdlib>
#include <iostream>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}
}

int main() {
    ScriptExecutionGate gate;
    constexpr std::uint64_t reserveTick = 100;
    constexpr std::uint64_t releaseTick = 200;
    Expect(gate.TryReserve(reserveTick), "first dispatch must reserve");
    for (int attempt = 0; attempt < 100; ++attempt) {
        Expect(!gate.TryReserve(reserveTick), "same-tick spam must be suppressed");
    }

    gate.Release(releaseTick);
    for (int attempt = 0; attempt < 100; ++attempt) {
        Expect(!gate.TryReserve(releaseTick + ScriptExecutionGate::CooldownMs - 1),
               "cooldown spam must be suppressed");
    }

    int boundaryReservations = 0;
    for (int attempt = 0; attempt < 100; ++attempt) {
        boundaryReservations += gate.TryReserve(releaseTick + ScriptExecutionGate::CooldownMs) ? 1 : 0;
    }
    Expect(boundaryReservations == 1, "cooldown boundary must reserve exactly once");

    ScriptExecutionGate zeroReleaseGate;
    Expect(zeroReleaseGate.TryReserve(0), "zero-time dispatch must reserve");
    zeroReleaseGate.Release(0);
    Expect(!zeroReleaseGate.TryReserve(1), "release at zero must start cooldown");
    Expect(zeroReleaseGate.TryReserve(250), "zero-time cooldown boundary must reserve");

    Expect(!HotkeyDispatch::ShouldTrackInput(true), "blocked input must bypass tracking");
    Expect(HotkeyDispatch::Decide(true, true, false) == HotkeyDispatch::Action::PassThrough,
           "blocked hotkey must pass through");
    Expect(HotkeyDispatch::Decide(false, true, false) == HotkeyDispatch::Action::Consume,
           "busy matched hotkey must be consumed");
    Expect(HotkeyDispatch::Decide(false, true, true) == HotkeyDispatch::Action::Dispatch,
           "reserved matched hotkey must dispatch");
    Expect(HotkeyDispatch::ShouldRearmBlockedKeyEvent(true),
           "blocked key-up must rearm before passing through");
    Expect(!HotkeyDispatch::ShouldRearmBlockedKeyEvent(false),
           "blocked key-down must not rearm");

    bool armed = true;
    Expect(HotkeyDispatch::BeginMatchedPress(armed) == HotkeyDispatch::Action::Consume,
           "matched press must default to silent consume");
    Expect(!armed, "matched busy press must disarm");
    Expect(HotkeyDispatch::ShouldConsumeHeldRepeat(armed, 'K', 'K'),
           "repeat key-down before primary key-up must stay consumed");
    Expect(!HotkeyDispatch::RearmOnReleasedKey(armed, 'K', VK_CONTROL),
           "modifier release must not rearm a held primary key");
    Expect(!armed, "matched press must stay disarmed until primary release");
    Expect(HotkeyDispatch::RearmOnReleasedKey(armed, 'K', 'K'),
           "primary key-up must rearm while modifiers remain held");
    Expect(!HotkeyDispatch::ShouldConsumeHeldRepeat(armed, 'K', 'K'),
           "primary key-up must end held-repeat consumption");

    armed = false;
    Expect(HotkeyDispatch::RearmOnReleasedKey(armed, VK_CONTROL, VK_LCONTROL),
           "generic modifier primary must rearm on physical-side key-up");
    return 0;
}
