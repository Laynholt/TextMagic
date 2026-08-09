#include "ModifierGestureResolver.h"

#include <windows.h>

#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

ModifierGestureResolver MakeResolver() {
    ModifierGestureResolver resolver;
    resolver.SetBindings({
        {1, ScriptManifest::HotkeyKind::ModifierGesture, MOD_SHIFT, VK_SHIFT},
        {2, ScriptManifest::HotkeyKind::ModifierDoubleTap, MOD_SHIFT, VK_SHIFT},
        {3, ScriptManifest::HotkeyKind::ModifierGesture, MOD_CONTROL, VK_CONTROL},
        {4, ScriptManifest::HotkeyKind::ModifierGesture, MOD_CONTROL | MOD_SHIFT, 0},
    });
    return resolver;
}

void CheckNoDispatch(const ModifierGestureResolver::Decision& decision, const char* message) {
    Check(decision.hotkeyId == 0, message);
}
}

int main() {
    {
        auto resolver = MakeResolver();
        CheckNoDispatch(resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 0, 100),
                        "single Shift starts without dispatch");
        const auto completed = resolver.OnKeyUp(VK_SHIFT, 0, 50, 100);
        Check(completed.pending && completed.dueTick == 350,
              "single Shift waits for the resolution timeout");
        Check(resolver.HasPending() && resolver.DueTick() == 350,
              "single Shift exposes its due tick");
        CheckNoDispatch(resolver.OnTimeout(349, 100),
                        "single Shift does not dispatch before 350 ms");
        const auto dispatched = resolver.OnTimeout(350, 100);
        Check(dispatched.hotkeyId == 1 && !dispatched.pending,
              "single Shift dispatches at 350 ms");
        Check(!resolver.HasPending(), "single Shift clears after dispatch");
    }

    {
        auto resolver = MakeResolver();
        resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 1000, 100);
        CheckNoDispatch(resolver.OnKeyUp(VK_SHIFT, 0, 1301, 100),
                        "a long Shift hold is canceled");
        CheckNoDispatch(resolver.OnTimeout(2000, 100),
                        "a long Shift hold never dispatches");
    }

    {
        auto resolver = MakeResolver();
        resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 0, 100);
        CheckNoDispatch(resolver.OnKeyDown('A', MOD_SHIFT, 10, 100),
                        "Shift plus A cancels the modifier gesture");
        CheckNoDispatch(resolver.OnTimeout(350, 100),
                        "Shift plus A leaves no pending dispatch");
    }

    {
        auto resolver = MakeResolver();
        resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 0, 100);
        resolver.OnKeyUp(VK_SHIFT, 0, 50, 100);
        Check(resolver.Cancel().hotkeyId == 0 && !resolver.HasPending(),
              "mouse cancellation clears the pending gesture");
        CheckNoDispatch(resolver.OnTimeout(350, 100),
                        "mouse cancellation prevents dispatch");
    }

    {
        auto resolver = MakeResolver();
        resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 0, 100);
        resolver.OnKeyUp(VK_SHIFT, 0, 50, 100);
        CheckNoDispatch(resolver.OnTimeout(350, 200),
                        "a foreground change cancels the pending gesture");
        Check(!resolver.HasPending(), "foreground change clears pending state");
    }

    {
        auto resolver = MakeResolver();
        resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 0, 100);
        resolver.OnKeyUp(VK_SHIFT, 0, 50, 100);
        const auto doubleTap = resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 200, 100);
        Check(doubleTap.hotkeyId == 2 && !doubleTap.pending,
              "Shift double tap wins over the pending single gesture");
        CheckNoDispatch(resolver.OnTimeout(350, 100),
                        "a resolved Shift double tap suppresses the single action");
    }

    {
        auto resolver = MakeResolver();
        resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 0, 100);
        resolver.OnKeyUp(VK_SHIFT, 0, 50, 100);
        Check(!ModifierGestureResolver::IsTimeoutDue(resolver.DueTick(), 350),
              "the pre-event timeout stays pending at the exact double-tap boundary");
        const auto doubleTap = resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 350, 100);
        Check(doubleTap.hotkeyId == 2 && !doubleTap.pending,
              "the exact 350 ms second tap wins before pre-event timeout resolution");
    }

    {
        ModifierGestureResolver resolver;
        resolver.SetBindings({
            {7, ScriptManifest::HotkeyKind::ModifierDoubleTap, MOD_SHIFT, VK_SHIFT},
        });
        CheckNoDispatch(resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 0, 100),
                        "a double-tap-only binding starts its first tap");
        const auto firstTap = resolver.OnKeyUp(VK_SHIFT, 0, 50, 100);
        Check(firstTap.pending && firstTap.dueTick == 350,
              "a double-tap-only binding keeps the first tap until timeout");
        CheckNoDispatch(resolver.OnTimeout(350, 100),
                        "a double-tap-only binding emits no single action at timeout");

        resolver.SetBindings({
            {7, ScriptManifest::HotkeyKind::ModifierDoubleTap, MOD_SHIFT, VK_SHIFT},
        });
        resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 500, 100);
        resolver.OnKeyUp(VK_SHIFT, 0, 550, 100);
        Check(resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 700, 100).hotkeyId == 7,
              "a double-tap-only binding dispatches on its second tap");
    }

    {
        auto resolver = MakeResolver();
        resolver.OnKeyDown(VK_CONTROL, MOD_CONTROL, 0, 100);
        resolver.OnKeyDown(VK_SHIFT, MOD_CONTROL | MOD_SHIFT, 50, 100);
        resolver.OnKeyUp(VK_SHIFT, MOD_CONTROL, 100, 100);
        const auto completed = resolver.OnKeyUp(VK_CONTROL, 0, 120, 100);
        Check(completed.pending && completed.dueTick == 350,
              "Ctrl plus Shift keeps the original first-press tick");
        const auto dispatched = resolver.OnTimeout(350, 100);
        Check(dispatched.hotkeyId == 4,
              "the specific Ctrl plus Shift chord dispatches");
    }

    {
        auto resolver = MakeResolver();
        resolver.OnKeyDown(VK_CONTROL, MOD_CONTROL, 0, 100);
        CheckNoDispatch(resolver.OnKeyDown(VK_CONTROL, MOD_CONTROL, 10, 100),
                        "a held modifier repeat does not create a candidate");
        resolver.OnKeyUp(VK_CONTROL, 0, 50, 100);
        Check(resolver.OnTimeout(350, 100).hotkeyId == 3,
              "the original Ctrl candidate remains intact after a repeat");
    }

    {
        auto resolver = MakeResolver();
        resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 0, 100);
        CheckNoDispatch(resolver.OnKeyDown(VK_CAPITAL, MOD_SHIFT, 10, 100),
                        "Caps Lock is not a resolver modifier");
        CheckNoDispatch(resolver.OnTimeout(350, 100),
                        "Caps Lock cancels the pending modifier gesture");
    }

    {
        auto resolver = MakeResolver();
        resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 0, 100);
        CheckNoDispatch(resolver.OnKeyUp(VK_SHIFT, 0, 50, 200),
                        "a context-mismatched release cancels the old candidate");
        resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 100, 200);
        resolver.OnKeyUp(VK_SHIFT, 0, 150, 200);
        Check(resolver.OnTimeout(450, 200).hotkeyId == 1,
              "a fresh gesture resolves after a context-mismatched release");
    }

    {
        ModifierGestureResolver resolver;
        resolver.SetBindings({
            {4, ScriptManifest::HotkeyKind::ModifierGesture,
             MOD_CONTROL | MOD_SHIFT, 0},
        });
        resolver.OnKeyDown(VK_CONTROL, MOD_CONTROL, 0, 100);
        resolver.OnKeyDown(VK_SHIFT, MOD_CONTROL | MOD_SHIFT, 250, 100);
        resolver.OnKeyUp(VK_SHIFT, MOD_CONTROL, 260, 100);
        CheckNoDispatch(resolver.OnKeyUp(VK_CONTROL, 0, 301, 100),
                        "a chord-only gesture uses its first modifier press for hold timing");
        CheckNoDispatch(resolver.OnTimeout(600, 100),
                        "an overlong chord-only gesture never dispatches");

        resolver.SetBindings({
            {4, ScriptManifest::HotkeyKind::ModifierGesture,
             MOD_CONTROL | MOD_SHIFT, 0},
        });
        resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 1000, 100);
        resolver.OnKeyDown(VK_CONTROL, MOD_CONTROL | MOD_SHIFT, 1020, 100);
        resolver.OnKeyUp(VK_CONTROL, MOD_SHIFT, 1030, 100);
        const auto completed = resolver.OnKeyUp(VK_SHIFT, 0, 1040, 100);
        Check(completed.pending && completed.dueTick == 1350,
              "a reversed chord-only gesture keeps its first-press due tick");
        Check(resolver.OnTimeout(1350, 100).hotkeyId == 4,
              "a short reversed chord-only gesture dispatches");
    }

    {
        auto resolver = MakeResolver();
        resolver.OnKeyDown(VK_LSHIFT, MOD_SHIFT, 0, 100);
        resolver.OnKeyDown(VK_RSHIFT, MOD_SHIFT, 10, 100);
        CheckNoDispatch(resolver.OnKeyUp(VK_LSHIFT, MOD_SHIFT, 20, 100),
                        "releasing one Shift side keeps the generic modifier pressed");
        Check(!resolver.HasPending(), "one held Shift side keeps the candidate active");
        Check(resolver.OnKeyUp(VK_RSHIFT, 0, 30, 100).pending,
              "releasing the final Shift side completes the gesture");
        Check(resolver.OnTimeout(350, 100).hotkeyId == 1,
              "the side-insensitive Shift gesture resolves once both sides release");
    }

    {
        auto resolver = MakeResolver();
        resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 0, 100);
        resolver.Cancel(0);
        CheckNoDispatch(resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 100, 100),
                        "cancellation synchronizes a released modifier mask");
        Check(resolver.OnKeyUp(VK_SHIFT, 0, 120, 100).pending,
              "a modifier can start a fresh gesture after synchronized cancellation");
    }

    {
        auto resolver = MakeResolver();
        resolver.OnKeyDown(VK_SHIFT, MOD_SHIFT, 0, 100);
        resolver.OnKeyUp(VK_SHIFT, 0, 50, 100);
        const auto overdue = resolver.OnTimeout(400, 100);
        Check(overdue.hotkeyId == 1 && overdue.context == 100,
              "an overdue timeout resolves with its captured context");
    }

    return 0;
}
