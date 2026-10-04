#pragma once

#include <cstdint>

inline uint32_t TimerResolutionFromX(int x, int left, int width, uint32_t minimum,
    uint32_t maximum, uint32_t step = 1000) {
    if (maximum <= minimum || width <= 0) return minimum;
    if (x <= left) return minimum;
    if (x >= left + width) return maximum;

    const uint64_t range = static_cast<uint64_t>(maximum) - minimum;
    const uint64_t offset = static_cast<uint64_t>(x - left);
    const uint64_t scaled = (range * offset + static_cast<uint64_t>(width) / 2) /
        static_cast<uint64_t>(width);
    const uint64_t quantum = step ? step : 1;
    const uint64_t rounded = (scaled + quantum / 2) / quantum * quantum;
    const uint64_t value = static_cast<uint64_t>(minimum) + rounded;
    return static_cast<uint32_t>(value > maximum ? maximum : value);
}
