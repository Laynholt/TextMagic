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
        : (hasSelection
            ? Type::Selection
            : (hasTrackedInput ? Type::TrackedInput : Type::None));
}
}
