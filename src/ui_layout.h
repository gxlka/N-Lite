#pragma once

#include <algorithm>
#include "ui_theme.h"

struct UiRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct MemoryLayout {
    UiRect header;
    UiRect content;
    UiRect title;
    UiRect memory;
    UiRect cleaner;
    UiRect timer;
};

inline int ComputeUiContentMargin(int width) {
    const int suggested = (std::max)(24, width / 40);
    const int snapped = ((suggested + kUiSpacingPx / 2) / kUiSpacingPx) * kUiSpacingPx;
    return (std::min)(32, snapped);
}

inline MemoryLayout ComputeMemoryLayout(int width, int height) {
    width = (std::max)(1, width);
    height = (std::max)(1, height);
    const int margin = ComputeUiContentMargin(width);
    const int headerHeight = (std::min)(64, height);
    const int contentRight = (std::max)(margin + 1, width - margin);
    const int contentBottom = (std::max)(1, height - margin);
    const int contentWidth = contentRight - margin;
    const int gap = kUiSpacingPx;
    const int cleanerWidth = (std::min)(480, (std::max)(300, contentWidth * 40 / 100));
    const int heroTop = ((std::min)(144, (std::max)(80, height / 4)) / kUiSpacingPx) * kUiSpacingPx;
    const int timerHeight = (std::min)(72, (std::max)(64, (height / 9 / kUiSpacingPx) * kUiSpacingPx));
    const int timerGap = 2 * kUiSpacingPx;
    const int availableHeroHeight = height - margin - timerHeight - timerGap - heroTop;
    const int heroHeight = (std::min)(328, (std::max)(180, availableHeroHeight));
    const int heroBottom = (std::min)(height - margin - timerHeight - timerGap, heroTop + heroHeight);
    const int timerTop = (std::min)(height - margin - timerHeight, heroBottom + timerGap);
    const int timerBottom = (std::max)(timerTop + 1, (std::min)(height, timerTop + timerHeight));
    const int memoryRight = (std::max)(margin + 1, contentRight - cleanerWidth - gap);

    MemoryLayout layout;
    layout.header = {0, 0, width, headerHeight};
    layout.content = {margin, headerHeight, contentRight, contentBottom};
    layout.title = {margin, (std::min)(height - 1, headerHeight + 16), contentRight,
        (std::min)(height, headerHeight + 56)};
    layout.memory = {margin, heroTop, memoryRight, heroBottom};
    layout.cleaner = {memoryRight + gap, heroTop, contentRight, heroBottom};
    layout.timer = {margin, timerTop, contentRight, timerBottom};
    return layout;
}
