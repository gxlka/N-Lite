#pragma once

#include <cstdint>

inline int UiLogicalToDevice(int value, unsigned dpi) {
    if (dpi == 0) dpi = 96;
    const int64_t numerator = static_cast<int64_t>(value) * dpi;
    const int64_t magnitude = numerator < 0 ? -numerator : numerator;
    const int64_t rounded = (magnitude + 48) / 96;
    return static_cast<int>(numerator < 0 ? -rounded : rounded);
}

inline int UiDeviceToLogical(int value, unsigned dpi) {
    if (dpi == 0) dpi = 96;
    const int64_t numerator = static_cast<int64_t>(value) * 96;
    const int64_t magnitude = numerator < 0 ? -numerator : numerator;
    const int64_t rounded = (magnitude + dpi / 2) / dpi;
    return static_cast<int>(numerator < 0 ? -rounded : rounded);
}
