#pragma once

namespace ScriptInputSource {
enum class Type {
    None,
    Clipboard,
    Selection,
    TrackedInput
};

constexpr Type Choose(bool clipboardOnly, bool hasSelection, bool hasTrackedInput) noexcept {
    return clipboardOnly
        ? Type::Clipboard
        : (hasTrackedInput
            ? Type::TrackedInput
            : (hasSelection ? Type::Selection : Type::None));
}
}
