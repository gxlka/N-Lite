# Standby Cleaner Helper Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let N-Lite purge standby memory and retain automatic cleaning after one restricted helper setup, without elevating the UI or asking UAC for routine cleans.

**Architecture:** Build a small `N-Lite-Cleaner.exe`; an explicit elevated install copies it to protected ProgramData, registers a SYSTEM scheduled task, and grants Users task run/read but no modification. The task reads bounded per-user config and records results separately; the unelevated UI requests manual work by updating a request ID and starting that same task.

**Tech Stack:** C++17, Win32 APIs, Task Scheduler COM/XML, CMake/CTest, Inno Setup, GitHub Actions.

**Spec:** `docs/superpowers/specs/standby-cleaner.md`

## Global Constraints

- Preserve the per-user N-Lite install and existing update installer.
- Normal clean and automatic-clean actions must never use `runas`.
- Protected task action and helper path must not be writable by Users.
- The helper accepts only bounded cleaner settings and standby purge requests.
- Windows target remains x64 and supports Windows 10 build 17763 or later.

## Review Focus

- Malformed or out-of-range config — test parse rejects it and auto-clean stays off.
- Threshold equality, hysteresis, and cooldown — test exact byte/time boundaries.
- A stale or repeated manual request ID — test only a new ID starts one manual purge.
- Helper path/task DACL setup failure — verify install returns failure and UI does not claim readiness.
- UAC cancellation or task start failure — verify UI reports failure and the unelevated app stays open.

---

### Task 1: Cleaner Settings and Purge Policy

**Files:**
- Create: `src/cleaner_policy.h`
- Create: `tests/cleaner_policy_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `CleanerSettings`, `CleanerStatus`, settings/status parse+serialize functions, and `ShouldRunAutoClean`.
- Valid threshold range: 64–131072 MB. Valid interval range: 60–7200 seconds.

- [ ] **Step 1: Write policy tests** named `settings_round_trip_preserves_bounded_values`, `invalid_settings_fail_closed`, `auto_clean_requires_threshold_armed_and_elapsed_interval`, and `manual_request_is_reported_only_after_matching_completion`. Assert thresholds 64 and 131072 MB and intervals 60 and 7200 seconds are accepted; 63/131073 MB and 59/7201 seconds are rejected.
- [ ] **Step 2: Run tests and verify RED** because the cleaner policy header/implementation does not exist yet.
- [ ] **Step 3: Implement the pure policy/serialization functions** in `src/cleaner_policy.h`; invalid input returns false and leaves output settings unchanged.
- [ ] **Step 4: Run `cmake --build build --parallel` and `ctest --test-dir build --output-on-failure`; verify every boundary assertion passes.**

### Task 2: Protected Helper and Scheduled Task

**Files:**
- Create: `src/cleaner.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/cleaner_policy_tests.cpp` for generated/validated settings interfaces; Windows build for the helper target.

**Interfaces:**
- Helper accepts exactly `--install <sid>`, `--run <sid>`, or `--uninstall <sid>`.
- Helper task name and config/status paths derive only from a validated SID.
- `--run` refuses to purge unless the process token is LocalSystem; it handles one new manual request or an eligible automatic purge, then writes status.

- [ ] **Step 1: Extend tests** to reject invalid settings/status versions, reject malformed SIDs through a pure SID-shape validator, and verify a completed request ID is not replayed.
- [ ] **Step 2: Run tests and verify RED** for the missing helper validation/policy behavior.
- [ ] **Step 3: Implement helper setup**: secure `%ProgramData%\N-Lite`, install the helper, create per-user config/status files with separate ACLs, register the SYSTEM minute task, set its DACL to SYSTEM/Admin full plus Users read/execute, and read back the task descriptor before reporting success.
- [ ] **Step 4: Implement helper run/uninstall modes** with only the standby purge operation, LocalSystem token check, exclusive-run guard, validated policy, NTSTATUS reporting, per-user task/config removal, and shared-helper cleanup when no users remain.
- [ ] **Step 5: Add the `N-Lite-Cleaner` CMake target**, required Task Scheduler/OLE libraries, and run build plus CTest.

### Task 3: Unelevated UI and Packaging Integration

**Files:**
- Modify: `src/main.cpp`
- Modify: `installer/N-Lite.iss`
- Modify: `.github/workflows/windows-build.yml`
- Modify: `README.md`
- Create: `releases/v0.2.3.md`

**Interfaces:**
- The app writes its own bounded settings to its SID-named ProgramData config, requests a clean by incrementing `manual_request_id`, and invokes the protected task with `schtasks /Run`.
- Helper setup/update runs elevated only when absent or incompatible. Manual and automatic clean never relaunch the app or helper with `runas`.
- The UI displays only results read from the helper status file.

- [ ] **Step 1: Use policy tests** for config serialization and status/request ID matching; add a Windows helper smoke test that checks `--install` reports setup failure without elevation, and inspect that only helper setup/update/uninstall paths use `runas`.
- [ ] **Step 2: Run the checks and verify RED** for the missing helper integration.
- [ ] **Step 3: Integrate setup, status polling, manual requests, auto settings writes, and uninstall cleanup** in `src/main.cpp`; preserve existing memory metrics and auto-clean controls.
- [ ] **Step 4: Package the helper** in the per-user installer and Actions artifact, run CTest in CI, update README and v0.2.3 release notes.
- [ ] **Step 5: Run full local build and CTest; inspect task XML/SDDL and manually test setup success/cancel, one manual clean, auto threshold, and uninstall in Windows.**
