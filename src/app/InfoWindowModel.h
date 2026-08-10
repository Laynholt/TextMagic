#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

inline std::vector<std::wstring> SplitLogLines(std::wstring_view text) {
    std::vector<std::wstring> rows;
    size_t start = 0;
    while (start < text.size()) {
        const size_t newline = text.find(L'\n', start);
        const size_t end = newline == std::wstring_view::npos ? text.size() : newline;
        size_t contentEnd = end;
        if (contentEnd > start && text[contentEnd - 1] == L'\r') {
            --contentEnd;
        }
        rows.emplace_back(text.substr(start, contentEnd - start));
        if (newline == std::wstring_view::npos) {
            break;
        }
        start = newline + 1;
    }
    return rows;
}

inline std::wstring JoinLogLines(const std::vector<std::wstring>& lines,
                                 const std::vector<size_t>& indices) {
    std::vector<size_t> ordered(indices.begin(), indices.end());
    std::sort(ordered.begin(), ordered.end());
    ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());

    std::wstring result;
    bool first = true;
    for (const size_t index : ordered) {
        if (index >= lines.size()) {
            continue;
        }
        if (!first) {
            result += L"\r\n";
        }
        result += lines[index];
        first = false;
    }
    return result;
}

inline bool ShouldShowVerticalScrollbar(size_t itemCount,
                                        int itemHeight,
                                        int clientHeight) {
    if (itemHeight <= 0 || clientHeight <= 0) {
        return false;
    }
    const int visibleItems = std::max(1, clientHeight / itemHeight);
    return itemCount > static_cast<size_t>(visibleItems);
}
