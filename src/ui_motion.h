#pragma once

#include <algorithm>
#include <cmath>

struct UiTween {
    double current = 0.0;
    double start = 0.0;
    double target = 0.0;
    double elapsedMs = 0.0;
    double durationMs = 0.0;
};

struct UiScrollMotion {
    double currentPx = 0.0;
    double startPx = 0.0;
    double targetPx = 0.0;
    double elapsedMs = 0.0;
    double durationMs = 0.0;
};

struct UiScrollFrame {
    int firstItem = 0;
    int firstRowTopPx = 0;
    int visibleCount = 0;
};

inline bool ShouldScheduleUiAnimationFrame(bool hasActiveMotion, bool windowVisible,
                                          bool minimized, bool animationsEnabled) {
    return hasActiveMotion && windowVisible && !minimized && animationsEnabled;
}

inline double ClampUiScroll(double requestedPx, double contentHeightPx, double viewportHeightPx) {
    const double maxOffset = (std::max)(0.0, contentHeightPx - viewportHeightPx);
    return (std::max)(0.0, (std::min)(requestedPx, maxOffset));
}

inline void ClampUiScrollMotion(UiScrollMotion& motion, double contentHeightPx,
                                double viewportHeightPx) {
    const double current = ClampUiScroll(motion.currentPx, contentHeightPx, viewportHeightPx);
    const double target = ClampUiScroll(motion.targetPx, contentHeightPx, viewportHeightPx);
    if (current == motion.currentPx && target == motion.targetPx) return;

    motion.currentPx = current;
    motion.targetPx = target;
    motion.startPx = current;
    motion.elapsedMs = 0.0;
    if (current == target) motion.durationMs = 0.0;
}

inline void SetUiScrollTarget(UiScrollMotion& motion, double requestedPx, double contentHeightPx,
                              double viewportHeightPx, double durationMs = 160.0) {
    motion.startPx = motion.currentPx;
    motion.targetPx = ClampUiScroll(requestedPx, contentHeightPx, viewportHeightPx);
    motion.elapsedMs = 0.0;
    motion.durationMs = (std::max)(0.0, durationMs);
}

inline void SetUiScrollWheelTarget(UiScrollMotion& motion, int wheelDelta,
                                   double contentHeightPx, double viewportHeightPx) {
    const double requested = motion.targetPx - static_cast<double>(wheelDelta) * (96.0 / 120.0);
    SetUiScrollTarget(motion, requested, contentHeightPx, viewportHeightPx);
}

inline void SetUiTweenTarget(UiTween& tween, double target, double durationMs) {
    tween.start = tween.current;
    tween.target = target;
    tween.elapsedMs = 0.0;
    tween.durationMs = (std::max)(0.0, durationMs);
}

inline double UiEaseOutCubic(double progress) {
    const double remaining = 1.0 - progress;
    return 1.0 - remaining * remaining * remaining;
}

inline bool AdvanceUiScroll(UiScrollMotion& motion, double elapsedMs, bool animationsEnabled) {
    if (!animationsEnabled || motion.durationMs <= 0.0 || motion.currentPx == motion.targetPx) {
        motion.currentPx = motion.targetPx;
        motion.startPx = motion.targetPx;
        motion.elapsedMs = motion.durationMs;
        return false;
    }

    motion.elapsedMs = (std::min)(motion.durationMs,
        motion.elapsedMs + (std::max)(0.0, elapsedMs));
    const double progress = motion.elapsedMs / motion.durationMs;
    motion.currentPx = motion.startPx + (motion.targetPx - motion.startPx) * UiEaseOutCubic(progress);
    if (progress >= 1.0) {
        motion.currentPx = motion.targetPx;
        motion.startPx = motion.targetPx;
        return false;
    }
    return true;
}

inline bool AdvanceUiTween(UiTween& tween, double elapsedMs, bool animationsEnabled) {
    if (!animationsEnabled || tween.durationMs <= 0.0 || tween.current == tween.target) {
        tween.current = tween.target;
        tween.start = tween.target;
        tween.elapsedMs = tween.durationMs;
        return false;
    }

    tween.elapsedMs = (std::min)(tween.durationMs,
        tween.elapsedMs + (std::max)(0.0, elapsedMs));
    const double progress = tween.elapsedMs / tween.durationMs;
    tween.current = tween.start + (tween.target - tween.start) * UiEaseOutCubic(progress);
    if (progress >= 1.0) {
        tween.current = tween.target;
        tween.start = tween.target;
        return false;
    }
    return true;
}

inline UiScrollFrame ComputeUiScrollFrame(double offsetPx, int rowHeightPx, int viewportTopPx,
                                          int viewportHeightPx, int itemCount) {
    UiScrollFrame frame;
    if (rowHeightPx <= 0 || viewportHeightPx <= 0 || itemCount <= 0) return frame;

    const double maxOffset = (std::max)(0.0,
        static_cast<double>(itemCount) * rowHeightPx - viewportHeightPx);
    const double offset = (std::max)(0.0, (std::min)(offsetPx, maxOffset));
    frame.firstItem = (std::min)(itemCount - 1, static_cast<int>(offset / rowHeightPx));
    const double inRowOffset = offset - static_cast<double>(frame.firstItem) * rowHeightPx;
    frame.firstRowTopPx = static_cast<int>(std::lround(viewportTopPx - inRowOffset));
    const double rowsNeeded = std::ceil((inRowOffset + viewportHeightPx) / rowHeightPx);
    frame.visibleCount = (std::max)(0, (std::min)(itemCount - frame.firstItem,
        static_cast<int>(rowsNeeded)));
    return frame;
}
