#pragma once

struct MainWindowHeaderLayout {
    int titleY;
    int titleHeight;
    int hintY;
    int hintHeight;
    int listTop;
};

inline MainWindowHeaderLayout CalculateMainWindowHeaderLayout(int innerY) {
    return {
        innerY + 6,
        40,
        innerY + 52,
        48,
        innerY + 108,
    };
}
