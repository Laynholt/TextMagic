#include "ScriptExecutionGate.h"

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
    return 0;
}
