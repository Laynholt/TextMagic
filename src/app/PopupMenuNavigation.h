#pragma once

#include "UiRenderer.h"

#include <cstddef>
#include <vector>

namespace PopupMenuNavigation {
inline UINT MoveSelection(const std::vector<UiRenderer::PopupMenuItem>& items,
                          UINT currentItemId,
                          int direction) {
    if (items.empty() || direction == 0) {
        return currentItemId;
    }

    const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(items.size());
    std::ptrdiff_t index = direction > 0 ? -1 : count;
    for (std::ptrdiff_t candidate = 0; candidate < count; ++candidate) {
        const UiRenderer::PopupMenuItem& item = items[static_cast<size_t>(candidate)];
        if (!item.separator && item.id == currentItemId) {
            index = candidate;
            break;
        }
    }

    for (std::ptrdiff_t step = 0; step < count; ++step) {
        index = (index + (direction > 0 ? 1 : count - 1)) % count;
        const UiRenderer::PopupMenuItem& item = items[static_cast<size_t>(index)];
        if (!item.separator) {
            return item.id;
        }
    }
    return 0;
}
} // namespace PopupMenuNavigation
