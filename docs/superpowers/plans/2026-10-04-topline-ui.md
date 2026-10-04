# Topline UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the left-rail UI with the selected compact Topline layout, add a persistent light/dark toggle, and make process groups and expansion stable.

**Architecture:** Keep the current Win32/GDI app and feature behavior. Add small pure helpers for executable identity and memory-page geometry, then paint a horizontal top navigation, a memory/cleaner two-column view, and a timer row using theme tokens.

**Tech Stack:** C++17, Win32/GDI, CMake/CTest.

**Spec:** `docs/superpowers/specs/topline-ui.md`

## Global Constraints

- Use the approved Topline mockup as the layout reference.
- Default to dark mode, persist the selected appearance per user, and include no gradients.
- Preserve all existing pages, process actions, sorting, search, timer, startup, and update behavior.
- Keep the existing 960x620 minimum client size and Windows x64 support floor.
- Display no process-group total across different normalized executable paths.

## Review Focus

- Minimum-size window — geometry tests ensure all memory/cleaner/timer hit boxes stay inside 960x620.
- Long page/process names — verify ellipsis does not cover numeric columns or the theme button.
- Two same-name executables in different folders — test distinct group keys and independent totals.
- Unknown executable paths — test PID-specific keys so inaccessible paths never merge.
- Automatic refresh during expansion — verify the visible anchor PID/group stays at its current row.

---

### Task 1: Pure UI and Process Identity Tests

**Files:**
- Create: `src/process_grouping.h`
- Create: `src/ui_layout.h`
- Create: `tests/ui_logic_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `ProcessGroupKey(const std::wstring& path, DWORD pid)` lowercases and normalizes separators; unavailable paths become PID-specific keys.
- `ComputeMemoryLayout(int width, int height)` returns client-relative rectangles for the header, content, memory hero, cleaner, and timer; every rectangle is non-empty and inside the client.

- [ ] **Step 1: Write tests** named `same_path_groups_case_and_separator_variants`, `same_name_different_paths_stay_separate`, `unavailable_paths_are_pid_unique`, and `memory_layout_stays_inside_minimum_and_target_clients`. Assert all five returned rectangles have positive width/height and remain within client edges at 960x620 and 1240x830.
- [ ] **Step 2: Run tests and verify RED** because the pure helpers do not exist yet.
- [ ] **Step 3: Implement the two pure helpers** and run CTest until all named assertions pass.

### Task 2: Group Rows by Executable Path

**Files:**
- Modify: `src/main.cpp`
- Test: `tests/ui_logic_tests.cpp`

**Interfaces:**
- `ProcRow::groupKey` stores the normalized executable key; `gExpanded` is keyed by that string rather than PID.
- A group summary uses one representative PID for actions, sums exactly its same-key instances, and expands to individual instance rows.

- [ ] **Step 1: Extend tests** with `group_total_sums_only_identical_path_keys` and assert representative plus instance rows sum only bytes sharing their `ProcessGroupKey`.
- [ ] **Step 2: Run tests and verify RED** against the missing group-model behavior.
- [ ] **Step 3: Replace parent-PID grouping** in `RefreshProcesses()` with normalized executable-key groups, and preserve the top visible PID/group through refresh, sort, and expansion.
- [ ] **Step 4: Run CTest and inspect sort, filter, expand/collapse, and per-process action paths.**

### Task 3: Topline Navigation, Memory, and Theme

**Files:**
- Modify: `src/main.cpp`
- Test: `tests/ui_logic_tests.cpp`

**Interfaces:**
- Add an `ID_THEME` hit target and persisted `ThemeDark` setting; `ApplyTheme(bool dark)` selects all surface/text/border/state colors before painting.
- `DrawHeader()` paints the top tabs and theme toggle; `DrawMemory()` uses `ComputeMemoryLayout()` for the two-column memory/cleaner cards and full-width timer.

- [ ] **Step 1: Add theme/layout assertions** that light/dark `PaletteFor()` return distinct background/surface/text colors and every Topline hit rectangle is inside both tested client bounds.
- [ ] **Step 2: Run tests and verify RED** for missing theme/layout integration.
- [ ] **Step 3: Replace the sidebar with top navigation**, add the memory-use bar and side-by-side cleaner panel, preserve every cleaner/timer hit target, and add theme persistence.
- [ ] **Step 4: Run CTest and inspect dark/light screenshots at 960x620 and 1240x830.**

### Task 4: Remaining Pages and Final Verification

**Files:**
- Modify: `src/main.cpp`
- Modify: `.github/workflows/windows-build.yml`
- Test: `tests/ui_logic_tests.cpp`

- [ ] **Step 1: Reflow Processes, Startup, and Settings** under the top bar, preserving all existing actions and keeping controls within the content bounds.
- [ ] **Step 2: Build and test** with `cmake --build build --parallel` and `ctest --test-dir build --output-on-failure`.
- [ ] **Step 3: Run the app at both target client sizes, exercise navigation/theme/search/sort/group expansion, and check that periodic refresh does not move the anchor row.**
- [ ] **Step 4: Add CTest to GitHub Actions and review the complete diff against this spec.**
