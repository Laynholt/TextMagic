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

    HotkeyDispatch::PressedKeyState hookKeys;
    hookKeys.Update(VK_LCONTROL, true);
    hookKeys.Update(VK_RMENU, true);
    Expect(hookKeys.Modifiers() == (MOD_CONTROL | MOD_ALT),
           "hook events must provide Ctrl+Alt before the async key state updates");
    Expect(HotkeyDispatch::MatchesVirtualKey('U', 'U')
               && hookKeys.Modifiers() == (MOD_CONTROL | MOD_ALT),
           "Ctrl+Alt+U must remain matchable from hook-owned key state");
    hookKeys.Update(VK_RMENU, false);
    Expect(hookKeys.Modifiers() == MOD_CONTROL,
           "modifier key-up must immediately update hook-owned state");
    Expect(HotkeyDispatch::NormalizeHookVirtualKey(VK_SHIFT, 0x2A, 0) == VK_LSHIFT,
           "left Shift hook events must retain their physical side");
    Expect(HotkeyDispatch::NormalizeHookVirtualKey(VK_SHIFT, 0x36, 0) == VK_RSHIFT,
           "right Shift hook events must retain their physical side");
    Expect(HotkeyDispatch::NormalizeHookVirtualKey(VK_CONTROL, 0, LLKHF_EXTENDED) == VK_RCONTROL,
           "extended Control hook events must map to right Control");
    Expect(HotkeyDispatch::NormalizeHookVirtualKey(VK_MENU, 0, LLKHF_EXTENDED) == VK_RMENU,
           "extended Alt hook events must map to right Alt");
    BYTE keyboardState[256] = {};
    keyboardState[VK_SHIFT] = 0x01;
    HotkeyDispatch::ApplyHookShiftState(keyboardState, true);
    Expect((keyboardState[VK_SHIFT] & 0x80) != 0,
           "hook Shift-down must reach ToUnicodeEx state");
    Expect((keyboardState[VK_SHIFT] & 0x01) != 0,
           "Shift overlay must preserve existing toggle bits");

    HotkeyDispatch::ApplyHookShiftState(keyboardState, false);
    Expect((keyboardState[VK_SHIFT] & 0x80) == 0,
           "hook Shift-up must clear stale GetKeyboardState state");
    Expect(HotkeyDispatch::IsModifierVirtualKey(VK_LCONTROL)
               && HotkeyDispatch::IsModifierVirtualKey(VK_RMENU)
               && HotkeyDispatch::IsModifierVirtualKey(VK_RSHIFT),
           "physical-side modifier events must not clear tracked input");
    Expect(!HotkeyDispatch::IsModifierVirtualKey('U'),
           "ordinary hotkey primaries must not be classified as modifiers");

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

    struct TestHookHotkey {
        bool armed;
        UINT modifiers;
        UINT virtualKey;
    };
    constexpr TestHookHotkey samePrimaryKey[] = {
        {false, MOD_CONTROL, 'K'},
        {true, MOD_ALT, 'K'},
    };
    bool heldRepeat = false;
    for (const TestHookHotkey& hotkey : samePrimaryKey) {
        heldRepeat = heldRepeat || HotkeyDispatch::ShouldConsumeHeldRepeat(
            hotkey.armed,
            'K',
            hotkey.virtualKey);
    }
    const bool competingArmedMatch = samePrimaryKey[1].armed
        && samePrimaryKey[1].modifiers == MOD_ALT
        && HotkeyDispatch::MatchesVirtualKey('K', samePrimaryKey[1].virtualKey);
    Expect(heldRepeat, "disarmed Ctrl+K must claim a held K repeat");
    Expect(HotkeyDispatch::DecideHeldRepeat(heldRepeat, competingArmedMatch, true)
               == HotkeyDispatch::Action::Consume,
           "held K repeat must consume before reservable Alt+K dispatch");

    armed = false;
    Expect(HotkeyDispatch::RearmOnReleasedKey(armed, VK_CONTROL, VK_LCONTROL),
           "generic modifier primary must rearm on physical-side key-up");
    return 0;
}
