#pragma once

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

enum class OutputLayout { Unchanged, English, Russian };

inline OutputLayout DetectOutputLayout(const std::wstring& output) {
    int latinLetters = 0;
    int cyrillicLetters = 0;
    for (const wchar_t character : output) {
        if ((character >= L'a' && character <= L'z')
            || (character >= L'A' && character <= L'Z')) {
            ++latinLetters;
        } else if (character >= 0x0400 && character <= 0x04FF) {
            ++cyrillicLetters;
        }
    }

    if (latinLetters == cyrillicLetters) {
        return OutputLayout::Unchanged;
    }
    return latinLetters > cyrillicLetters ? OutputLayout::English : OutputLayout::Russian;
}

inline OutputLayout ChooseOutputLayoutForAppliedScript(
    bool enabled,
    bool clipboardMode,
    bool replacementSucceeded,
    const std::wstring& output) {
    if (!enabled || clipboardMode || !replacementSucceeded) {
        return OutputLayout::Unchanged;
    }
    return DetectOutputLayout(output);
}

inline HKL FindInstalledOutputLayout(OutputLayout target, const std::vector<HKL>& layouts) {
    LANGID targetLanguage = 0;
    switch (target) {
    case OutputLayout::English:
        targetLanguage = LANG_ENGLISH;
        break;
    case OutputLayout::Russian:
        targetLanguage = LANG_RUSSIAN;
        break;
    case OutputLayout::Unchanged:
        return nullptr;
    }

    for (const HKL layout : layouts) {
        const LANGID layoutLanguage = LOWORD(reinterpret_cast<ULONG_PTR>(layout));
        if (PRIMARYLANGID(layoutLanguage) == targetLanguage) {
            return layout;
        }
    }
    return nullptr;
}

inline HKL FindNextInstalledLayout(HKL current, const std::vector<HKL>& layouts) {
    if (!current || layouts.size() < 2) {
        return nullptr;
    }
    const auto currentIt = std::find(layouts.begin(), layouts.end(), current);
    if (currentIt == layouts.end()) {
        return nullptr;
    }
    const size_t nextIndex = (static_cast<size_t>(currentIt - layouts.begin()) + 1) % layouts.size();
    return layouts[nextIndex];
}
