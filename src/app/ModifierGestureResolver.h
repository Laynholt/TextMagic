#pragma once

#include "ScriptManifest.h"

#include <cstdint>
#include <utility>
#include <vector>

class ModifierGestureResolver {
public:
    static constexpr ULONGLONG MaxHoldMs = 300;
    static constexpr ULONGLONG ResolveMs = 350;

    static UINT ModifierMaskForHookVirtualKey(DWORD virtualKey) noexcept {
        return ModifierMaskForVirtualKey(virtualKey);
    }

    struct Binding {
        int hotkeyId = 0;
        ScriptManifest::HotkeyKind kind = ScriptManifest::HotkeyKind::ModifierGesture;
        UINT modifiers = 0;
        UINT virtualKey = 0;
    };

    struct Decision {
        int hotkeyId = 0;
        bool pending = false;
        ULONGLONG dueTick = 0;
        std::uintptr_t context = 0;
    };

    void SetBindings(std::vector<Binding> bindings) {
        m_bindings = std::move(bindings);
        m_pressedMask = 0;
        ClearCandidates();
    }

    Decision OnKeyDown(DWORD virtualKey, UINT currentModifiers,
                       ULONGLONG now, std::uintptr_t context) {
        const UINT modifierBit = ModifierMaskForVirtualKey(virtualKey);
        if (modifierBit == 0) {
            return Cancel(currentModifiers);
        }

        if ((m_pressedMask & modifierBit) != 0) {
            return PendingDecision();
        }

        if ((m_candidate.active || m_completed.active)
            && context != Context()) {
            return Cancel(currentModifiers);
        }

        const UINT nextMask = (m_pressedMask
            | (currentModifiers & ModifierMask)
            | modifierBit) & ModifierMask;

        if (m_completed.active) {
            const Binding* repeated = FindRepeatedBinding(nextMask, virtualKey);
            if (repeated && m_firstTap.active
                && m_firstTap.modifiers == repeated->modifiers
                && m_firstTap.context == context
                && now >= m_firstTap.startedTick
                && now - m_firstTap.startedTick <= ResolveMs) {
                m_pressedMask = nextMask;
                ClearCandidates();
                return { repeated->hotkeyId, false, 0, context };
            }
            ClearCandidates();
        }

        m_pressedMask = nextMask;
        const Binding* gesture = FindGestureBinding(nextMask);
        const bool hasRepeated = FindRepeatedBinding(nextMask, virtualKey) != nullptr;
        if (!gesture && !hasRepeated && !HasGesturePrefix(nextMask)) {
            ClearCandidates();
            return {};
        }

        if (!m_candidate.active) {
            m_candidate = {
                true,
                gesture ? gesture->modifiers : nextMask,
                now,
                context,
                virtualKey,
            };
        } else if (gesture) {
            m_candidate.modifiers = gesture->modifiers;
        } else {
            m_candidate.modifiers = nextMask;
        }
        return {};
    }

    Decision OnKeyUp(DWORD virtualKey, UINT currentModifiers,
                     ULONGLONG now, std::uintptr_t context) {
        const UINT modifierBit = ModifierMaskForVirtualKey(virtualKey);
        if (modifierBit == 0) {
            return {};
        }

        m_pressedMask = (m_pressedMask & ~modifierBit)
            | (currentModifiers & ModifierMask);

        if ((m_candidate.active || m_completed.active)
            && context != Context()) {
            return Cancel(currentModifiers);
        }

        if (!m_candidate.active || (m_pressedMask & m_candidate.modifiers) != 0) {
            return PendingDecision();
        }

        const Candidate completed = m_candidate;
        m_candidate = {};
        const Binding* gesture = FindGestureBinding(completed.modifiers);
        const Binding* repeated = FindRepeatedBinding(completed.modifiers, completed.virtualKey);
        if (!gesture && !repeated) {
            ClearCandidates();
            return {};
        }
        if (now < completed.startedTick || now - completed.startedTick > MaxHoldMs) {
            ClearCandidates();
            return {};
        }

        m_completed = {
            true,
            completed.modifiers,
            completed.startedTick,
            completed.startedTick + ResolveMs,
            completed.context,
        };
        m_firstTap = {
            repeated != nullptr,
            completed.modifiers,
            completed.virtualKey,
            completed.startedTick,
            completed.context,
        };
        return PendingDecision();
    }

    Decision OnTimeout(ULONGLONG now, std::uintptr_t context) {
        if (!m_completed.active) {
            return {};
        }
        if (context != m_completed.context) {
            return Cancel(m_pressedMask);
        }
        if (now < m_completed.dueTick) {
            return PendingDecision();
        }

        const Binding* gesture = FindGestureBinding(m_completed.modifiers);
        const int hotkeyId = gesture ? gesture->hotkeyId : 0;
        const std::uintptr_t completedContext = m_completed.context;
        ClearCandidates();
        return { hotkeyId, false, 0, completedContext };
    }

    Decision Cancel(UINT currentModifiers = 0) noexcept {
        m_pressedMask = currentModifiers & ModifierMask;
        ClearCandidates();
        return {};
    }

    bool HasPending() const noexcept { return m_completed.active; }

    ULONGLONG DueTick() const noexcept {
        return m_completed.active ? m_completed.dueTick : 0;
    }

private:
    static constexpr UINT ModifierMask = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN;

    struct Candidate {
        bool active = false;
        UINT modifiers = 0;
        ULONGLONG startedTick = 0;
        std::uintptr_t context = 0;
        DWORD virtualKey = 0;
    };

    struct Completed {
        bool active = false;
        UINT modifiers = 0;
        ULONGLONG startedTick = 0;
        ULONGLONG dueTick = 0;
        std::uintptr_t context = 0;
    };

    struct FirstTap {
        bool active = false;
        UINT modifiers = 0;
        DWORD virtualKey = 0;
        ULONGLONG startedTick = 0;
        std::uintptr_t context = 0;
    };

    static UINT ModifierMaskForVirtualKey(DWORD virtualKey) noexcept {
        switch (virtualKey) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return MOD_SHIFT;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return MOD_CONTROL;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return MOD_ALT;
        case VK_LWIN:
        case VK_RWIN:
            return MOD_WIN;
        default:
            return 0;
        }
    }

    static bool SameModifierKey(DWORD first, DWORD second) noexcept {
        const UINT firstMask = ModifierMaskForVirtualKey(first);
        return firstMask != 0 && firstMask == ModifierMaskForVirtualKey(second);
    }

    const Binding* FindGestureBinding(UINT modifiers) const noexcept {
        for (const auto& binding : m_bindings) {
            if (binding.kind == ScriptManifest::HotkeyKind::ModifierGesture
                && binding.modifiers == modifiers) {
                return &binding;
            }
        }
        return nullptr;
    }

    bool HasGesturePrefix(UINT modifiers) const noexcept {
        for (const auto& binding : m_bindings) {
            if (binding.kind == ScriptManifest::HotkeyKind::ModifierGesture
                && binding.modifiers != modifiers
                && (binding.modifiers & modifiers) == modifiers) {
                return true;
            }
        }
        return false;
    }

    const Binding* FindRepeatedBinding(UINT modifiers, DWORD virtualKey) const noexcept {
        for (const auto& binding : m_bindings) {
            if (binding.kind != ScriptManifest::HotkeyKind::ModifierDoubleTap
                || binding.modifiers != modifiers
                || (binding.virtualKey != 0 && !SameModifierKey(binding.virtualKey, virtualKey))) {
                continue;
            }
            return &binding;
        }
        return nullptr;
    }

    std::uintptr_t Context() const noexcept {
        if (m_candidate.active) {
            return m_candidate.context;
        }
        return m_completed.context;
    }

    Decision PendingDecision() const noexcept {
        return m_completed.active
            ? Decision{ 0, true, m_completed.dueTick, m_completed.context }
            : Decision{};
    }

    void ClearCandidates() noexcept {
        m_candidate = {};
        m_completed = {};
        m_firstTap = {};
    }

    std::vector<Binding> m_bindings;
    UINT m_pressedMask = 0;
    Candidate m_candidate;
    Completed m_completed;
    FirstTap m_firstTap;
};
