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
    Expect(gate.TryReserve(100), "first dispatch must reserve");
    Expect(!gate.TryReserve(101), "busy dispatch must be suppressed");
    gate.Release(200);
    Expect(!gate.TryReserve(449), "cooldown dispatch must be suppressed");
    Expect(gate.TryReserve(450), "dispatch after cooldown must reserve");

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
