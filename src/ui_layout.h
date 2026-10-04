#pragma once

#include <algorithm>

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

inline MemoryLayout ComputeMemoryLayout(int width, int height) {
    width = (std::max)(1, width);
    height = (std::max)(1, height);
    const int margin = (std::min)(30, (std::max)(20, width / 40));
    const int headerHeight = (std::min)(60, height);
    const int contentRight = (std::max)(margin + 1, width - margin);
    const int contentBottom = (std::max)(1, height - margin);
    const int contentWidth = contentRight - margin;
    const int gap = 12;
    const int cleanerWidth = (std::min)(480, (std::max)(300, contentWidth * 40 / 100));
    const int heroTop = (std::min)(144, (std::max)(82, height / 4));
    const int timerHeight = (std::min)(74, (std::max)(60, height / 9));
    const int timerGap = 12;
    const int availableHeroHeight = height - margin - timerHeight - timerGap - heroTop;
    const int heroHeight = (std::min)(326, (std::max)(180, availableHeroHeight));
    const int heroBottom = (std::min)(height - margin - timerHeight - timerGap, heroTop + heroHeight);
    const int timerTop = (std::min)(height - margin - timerHeight, heroBottom + timerGap);
    const int timerBottom = (std::max)(timerTop + 1, (std::min)(height, timerTop + timerHeight));
    const int memoryRight = (std::max)(margin + 1, contentRight - cleanerWidth - gap);

    MemoryLayout layout;
    layout.header = {0, 0, width, headerHeight};
    layout.content = {margin, headerHeight, contentRight, contentBottom};
    layout.title = {margin, (std::min)(height - 1, headerHeight + 18), contentRight,
        (std::min)(height, headerHeight + 58)};
    layout.memory = {margin, heroTop, memoryRight, heroBottom};
    layout.cleaner = {memoryRight + gap, heroTop, contentRight, heroBottom};
    layout.timer = {margin, timerTop, contentRight, timerBottom};
    return layout;
}
