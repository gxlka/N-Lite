# N-Lite GUI Overhaul Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refresh the complete N-Lite GUI with fixed logical-pixel sizing, crisp emoji/vector icons, smooth pixel scrolling, and restrained animations while preserving all app behavior.

**Architecture:** Keep the existing single-process C++17 Win32 app and GDI page painter. Add small, platform-independent motion and DPI math helpers with unit tests; render the few color emoji glyphs through native Direct2D/DirectWrite and draw action icons with GDI; use the existing buffered paint path for the shared UI.

**Tech Stack:** C++17, Win32/GDI, Direct2D, DirectWrite, CMake, CTest, GitHub Actions on Windows.

**Spec:** `docs/superpowers/specs/2026-10-06-nlite-gui-design.md`

## Global Constraints

- Keep the app as the existing single-process C++17 Win32 application.
- Keep the current 1240×830 requested window size and four-page horizontal navigation.
- Use an 8-pixel spacing grid, whole-pixel control bounds, consistent text sizes, and larger click targets than decorative details.
- Retain the restrained dark and light palettes. Use flat surfaces and clear contrast; no gradients or extra status banners.
- Use fixed logical-pixel control dimensions at the 96-DPI baseline. When Windows display scaling changes, rerender text and vector geometry at the target device scale and snap edges to physical pixels.
- Add a consistent 20-logical-pixel emoji accent beside each page label and the Memory/Cleaner section headings.
- Keep the current GDI page renderer. Use Direct2D/DirectWrite only to render the small emoji glyphs into the existing paint surface; draw all other new app icons with GDI vector primitives.
- Keep the current page model and command IDs; do not change cleaner, startup, update, process, or timer semantics.
- Run animation frames only while a transition is active and the window is visible. Respect the Windows animation preference.

## Review Focus

- A wheel burst at either scroll boundary, including during a process-list refresh, must clamp cleanly and preserve the visible anchor. Task 2 tests retargeting and bounds in the helper and verifies refresh anchoring in the Windows UI test.
- At fractional scroll offsets, the row under the pointer must be the row that receives the click. Task 1 tests row-frame geometry; Task 2 exercises process and startup hit targets while scrolling.
- At 125% and 150% display scaling, fixed logical coordinates must map to crisp device coordinates without cumulative drift. Task 3 tests logical/device round trips and verifies the rendered emoji surface.
- When there is no list overflow, or animations are disabled/minimized, no UI timer should keep running and controls must still work. Task 1 tests immediate completion; Task 4 verifies timer lifetime and the Windows animation preference.
- If Direct2D emoji creation or back-buffer creation fails, painting must fall back without preventing app interaction. Task 3 tests emoji-renderer fallback; Task 4 verifies the GDI direct-paint fallback.

---

### Task 1: Testable scroll and animation math

**Files:**
- Create: `src/ui_motion.h`
- Modify: `tests/core_logic_tests.cpp`

**Interfaces:**
- Produces `UiTween { current, start, target, elapsedMs, durationMs }` and `UiScrollMotion { currentPx, startPx, targetPx, elapsedMs, durationMs }`.
- Produces `ClampUiScroll(double requestedPx, double contentHeightPx, double viewportHeightPx) -> double`.
- Produces `SetUiScrollTarget(UiScrollMotion&, double requestedPx, double contentHeightPx, double viewportHeightPx, double durationMs) -> void`.
- Produces `AdvanceUiScroll(UiScrollMotion&, double elapsedMs, bool animationsEnabled) -> bool`, where the return value means another animation frame is needed.
- Produces `UiScrollFrame { firstItem, firstRowTopPx, visibleCount }` and `ComputeUiScrollFrame(double offsetPx, int rowHeightPx, int viewportTopPx, int viewportHeightPx, int itemCount) -> UiScrollFrame`.
- Produces generic `SetUiTweenTarget(UiTween&, double target, double durationMs) -> void` and `AdvanceUiTween(UiTween&, double elapsedMs, bool animationsEnabled) -> bool` for page and switch transitions in Task 4.
- Consumed by Task 2 for process and startup list state and row placement.

- [ ] **Step 1: Add failing tests** with these assertions in `tests/core_logic_tests.cpp`:

```cpp
Check(ClampUiScroll(-4, 1000, 300) == 0 && ClampUiScroll(900, 1000, 300) == 700 &&
    ClampUiScroll(90, 100, 300) == 0, "ui_scroll_clamps_to_content_bounds");
UiScrollMotion scroll{};
SetUiScrollTarget(scroll, 500, 1000, 300, 160);
Check(AdvanceUiScroll(scroll, 80, true) && scroll.currentPx > 400 && scroll.currentPx < 500,
    "ui_scroll_uses_elapsed_time_easing");
const double beforeRetarget = scroll.currentPx;
SetUiScrollTarget(scroll, 600, 1000, 300, 160);
Check(scroll.currentPx == beforeRetarget, "ui_scroll_retarget_does_not_jump");
Check(!AdvanceUiScroll(scroll, 160, false) && scroll.currentPx == 600,
    "ui_scroll_disabled_motion_completes_immediately");
const UiScrollFrame frame = ComputeUiScrollFrame(45.5, 44, 120, 132, 10);
Check(frame.firstItem == 1 && frame.firstRowTopPx == 119 && frame.visibleCount == 4,
    "ui_scroll_frame_aligns_rows_at_fractional_offsets");
UiTween tween{};
SetUiTweenTarget(tween, 1.0, 120);
Check(AdvanceUiTween(tween, 60, true) && tween.current > 0 && tween.current < 1,
    "ui_tween_interpolates_transitions");
Check(!AdvanceUiTween(tween, 60, true) && tween.current == 1,
    "ui_tween_finishes_at_exact_target");
Check(ComputeUiScrollFrame(0, 0, 0, 0, 0).visibleCount == 0,
    "ui_scroll_empty_viewport_has_no_rows");
```

- [ ] **Step 2: Run the logic test executable** and confirm the new assertions fail because the helper does not exist.

Run: `g++ -std=c++17 -Wall -Wextra -pedantic -Isrc tests/core_logic_tests.cpp -o /tmp/nlite-core-tests && /tmp/nlite-core-tests`

Expected: compile fails with missing UI motion declarations.

- [ ] **Step 3: Implement the pure helper** in `src/ui_motion.h` using elapsed-time cubic easing and a 160 ms default scroll duration.
- [ ] **Step 4: Rerun the logic test executable** and confirm the new UI motion assertions pass with no warnings.
- [ ] **Step 5: Commit the helper and tests** on `codex/gui-overhaul-design-20261006`.

### Task 2: Smooth process and startup scrolling

**Files:**
- Modify: `src/ui_motion.h`
- Modify: `src/main.cpp`
- Modify: `tests/core_logic_tests.cpp` if an additional shared scroll-bound assertion is needed

**Interfaces:**
- Consumes `UiScrollMotion`, `SetUiScrollTarget`, `AdvanceUiScroll`, and `ComputeUiScrollFrame` from Task 1.
- Produces `SetUiScrollWheelTarget(UiScrollMotion&, int wheelDelta, double contentHeightPx, double viewportHeightPx) -> void`; one 120-unit notch moves the target by 96 logical pixels, with positive Windows wheel delta decreasing the content offset.
- Produces process-list and startup-list rendering/hit testing driven from the same pixel offset.
- Keeps existing process grouping, keyboard selection, startup entry actions, and refresh behavior.

- [ ] **Step 1: Add a failing test** with these assertions for `SetUiScrollWheelTarget`:

```cpp
UiScrollMotion wheel{};
SetUiScrollWheelTarget(wheel, -120, 1000, 300);
SetUiScrollWheelTarget(wheel, -120, 1000, 300);
Check(wheel.targetPx == 192 && wheel.currentPx == 0,
    "ui_wheel_bursts_accumulate_without_jumping");
SetUiScrollWheelTarget(wheel, 1200, 1000, 300);
Check(wheel.targetPx == 0, "ui_wheel_target_clamps_at_content_start");
SetUiScrollWheelTarget(wheel, -1200, 1000, 300);
Check(wheel.targetPx == 700, "ui_wheel_target_clamps_at_content_end");
```

- [ ] **Step 2: Run the logic test executable** and confirm it fails because `SetUiScrollWheelTarget` is missing.
- [ ] **Step 3: Implement `SetUiScrollWheelTarget`** in `src/ui_motion.h` and confirm the logic test passes.
- [ ] **Step 4: Replace row-index wheel jumps** with the tested helper and animate process/startup current offsets toward their targets on a 16 ms UI timer.
- [ ] **Step 5: Use `ComputeUiScrollFrame` for drawing, hit rectangles, and scrollbar positions**; keep process refresh/group expansion anchored to the same visible row and partial-row offset.
- [ ] **Step 6: Run logic tests and a Windows UI smoke test** covering scrolling both lists, click/toggle/delete after scrolling, wheel bursts, list refresh during motion, and top/bottom clamping.
- [ ] **Step 7: Commit the list-scrolling changes** on the feature branch.

### Task 3: Fixed-pixel DPI handling and crisp emoji rendering

**Files:**
- Create: `src/ui_metrics.h`
- Create: `src/ui_emoji.h`
- Create: `src/ui_emoji.cpp`
- Create: `tests/ui_render_tests.cpp`
- Modify: `tests/core_logic_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `src/main.cpp`

**Interfaces:**
- Produces `UiLogicalToDevice(int value, unsigned dpi) -> int` and `UiDeviceToLogical(int value, unsigned dpi) -> int` in `src/ui_metrics.h`.
- Produces `InitializeUiEmojiRenderer() -> bool`, `DrawUiEmoji(HDC dc, UiRect logicalBounds, unsigned dpi, const wchar_t* glyph, COLORREF fallback) -> bool`, and `ShutdownUiEmojiRenderer() -> void` in `src/ui_emoji.h`.
- Consumes `UiRect` from `src/ui_layout.h`; `ui_emoji.h` includes the Windows declarations required by its HDC/COLORREF interface.
- Uses GDI vector fallbacks if Direct2D/DirectWrite color emoji rendering fails.

- [ ] **Step 1: Add failing tests** with these DPI assertions in `tests/core_logic_tests.cpp`:

```cpp
Check(UiLogicalToDevice(8, 96) == 8 && UiLogicalToDevice(8, 120) == 10 &&
    UiLogicalToDevice(8, 144) == 12, "ui_dpi_maps_grid_edges_to_device_pixels");
Check(UiDeviceToLogical(10, 120) == 8 && UiDeviceToLogical(12, 144) == 8 &&
    std::abs(UiDeviceToLogical(UiLogicalToDevice(11, 120), 120) - 11) <= 1,
    "ui_dpi_round_trip_has_at_most_one_pixel_error");
```

Also add `ui_emoji_draws_glyph_or_vector_fallback` in `tests/ui_render_tests.cpp`; paint onto a DIB and assert that some pixels differ from the background.
- [ ] **Step 2: Run the relevant tests** and confirm missing DPI helpers or renderer symbols cause the expected failures.
- [ ] **Step 3: Implement DPI conversion helpers** and add assertions for rounding and round-trip error within one device pixel.
- [ ] **Step 4: Implement the small Direct2D/DirectWrite emoji renderer** with 20 logical-pixel glyphs, device-resource reset, and GDI vector fallback.
- [ ] **Step 5: Make the main window per-monitor-DPI-aware**; rerender at the current DPI and convert pointer coordinates to the same logical coordinate space used for hit tests.
- [ ] **Step 6: Add a CMake/CTest target for `tests/ui_render_tests.cpp`**, linked to `d2d1` and `dwrite`; build and run it on Windows and verify both renderer and fallback paths produce visible pixels.
- [ ] **Step 7: Commit DPI and emoji rendering changes** on the feature branch.

### Task 4: Shared visual system and restrained transitions

**Files:**
- Modify: `src/ui_theme.h`
- Modify: `src/ui_layout.h`
- Create: `src/ui_icons.h`
- Create: `src/ui_paint.h`
- Modify: `src/main.cpp`
- Modify: `tests/core_logic_tests.cpp`
- Modify: `tests/ui_render_tests.cpp`

**Interfaces:**
- Produces `kUiSpacingPx == 8`, `kUiEmojiSizePx == 20`, `kUiButtonHeightPx == 40`, `kUiProcessRowHeightPx == 44`, `kUiStartupRowHeightPx == 60`, and `kUiCornerRadiusPx == 12` for all pages.
- Produces `UiIcon` and `DrawUiIcon(HDC dc, UiIcon icon, UiRect logicalBounds, COLORREF color) -> void` for refresh, add, clean, delete, and navigation actions.
- Produces `ShouldScheduleUiAnimationFrame(bool hasActiveMotion, bool windowVisible, bool minimized, bool animationsEnabled) -> bool` for tested UI scheduling.
- Produces `using UiPaintCallback = void (*)(HDC, int, int, void*)`, `using UiBitmapFactory = HBITMAP (*)(HDC, int, int, void*)`, and `PaintUiBuffered(HDC target, int widthPx, int heightPx, UiPaintCallback paint, void* context, UiBitmapFactory factory) -> bool`; on allocation failure it paints directly to `target` and returns `false`.
- Consumes DPI mapping and emoji drawing from Task 3.
- Keeps every existing command ID, label meaning, and page action.

- [ ] **Step 1: Add a failing layout assertion** with this behavior assertion in `tests/core_logic_tests.cpp`:

```cpp
const MemoryLayout aligned = ComputeMemoryLayout(1240, 830);
Check(aligned.memory.left == 32 && aligned.memory.top % 8 == 0 &&
    aligned.cleaner.left - aligned.memory.right == 8,
    "memory_panels_follow_eight_pixel_grid_and_gap");
```
- [ ] **Step 2: Run the logic tests** and confirm the layout assertion fails against the current 30-pixel margin and 12-pixel panel gap.
- [ ] **Step 3: Implement shared visual tokens and vector icons**; apply them consistently across Memory, Processes, Startup, and Settings without adding controls or banners.
- [ ] **Step 4: Add failing assertions** in `tests/core_logic_tests.cpp` for the timer predicate and in `tests/ui_render_tests.cpp` for `ui_vector_icon_draws_sharp_pixels` and `ui_paint_falls_back_when_bitmap_allocation_fails`; use a real DIB target and a bitmap factory that returns null to exercise direct paint.

```cpp
Check(!ShouldScheduleUiAnimationFrame(false, true, false, true) &&
    !ShouldScheduleUiAnimationFrame(true, false, false, true) &&
    !ShouldScheduleUiAnimationFrame(true, true, true, true) &&
    !ShouldScheduleUiAnimationFrame(true, true, false, false) &&
    ShouldScheduleUiAnimationFrame(true, true, false, true),
    "ui_animation_timer_stops_when_idle_minimized_or_reduced_motion");
```

- [ ] **Step 5: Run the logic test executable** and confirm it fails because the timer predicate is missing.
- [ ] **Step 6: Implement and test the timer predicate**, then add time-based page-entry and switch transitions using `UiTween`.
- [ ] **Step 7: Retain a direct-paint fallback** if back-buffer allocation fails; stop the UI timer when no transitions remain, the app is minimized, or the Windows animation preference is off.
- [ ] **Step 8: Verify the GDI direct-paint fallback** with the injected bitmap-failure test.
- [ ] **Step 9: Verify the complete suite** and inspect the app at 100%, 125%, and 150% DPI plus a narrower window; exercise all click targets on all pages.
- [ ] **Step 10: Commit the visual and transition changes** on the feature branch.

### Task 5: Windows release verification

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/main.cpp`
- Modify: `installer/N-Lite.iss`
- Modify: `.github/workflows/windows-build.yml`
- Create: `releases/v0.2.17.md`

**Interfaces:**
- Uses the existing Windows build, CTest, portable executable, cleaner, and installer workflow.
- Publishes the completed UI overhaul as N-Lite v0.2.17 after the feature branch build is green.

- [ ] **Step 1: Run the full Windows build and CTest suite** on the feature branch; expected result is successful build and all tests passing.
- [ ] **Step 2: Add v0.2.17 release metadata and notes** only after the UI implementation has passed verification.
- [ ] **Step 3: Open the reviewed feature branch as a pull request** and verify the Windows workflow is green before merging/publishing.
- [ ] **Step 4: Publish v0.2.17** with the portable app, cleaner helper, and installer attached.

## Self-review

- Spec coverage: visual system and layout → Task 4; icons and emoji → Task 3 and Task 4; scrolling and animation → Tasks 1, 2, and 4; implementation boundaries/fallbacks → Tasks 3 and 4; verification → Tasks 2, 3, 4, and 5.
- Interface consistency: Task 2 consumes Task 1's `UiScrollMotion` and `UiScrollFrame`; Tasks 3–4 consume `UiRect` from the existing layout header and the DPI conversion/emoji APIs from Task 3. No task consumes an undefined API.
- Review-focus coverage: scroll bounds/retargeting and row geometry → Tasks 1–2 tests; DPI rounding and emoji fallback → Task 3 tests; reduced motion/idle timer and back-buffer failure → Task 4 tests/manual verification.
- Proportion: five tasks separate pure math, list integration, DPI/rendering, shared design/animation, and release packaging; each can be verified independently.
