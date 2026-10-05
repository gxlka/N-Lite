#include "cleaner_policy.h"
#include "process_grouping.h"
#include "ui_layout.h"
#include "ui_theme.h"
#include "timer_slider.h"
#include "startup_policy.h"
#include "process_visibility.h"

#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void Check(bool condition, const char* name) {
    if (condition) {
        std::cout << "PASS " << name << '\n';
    } else {
        std::cout << "FAIL " << name << '\n';
        ++failures;
    }
}

bool Inside(UiRect rect, int width, int height) {
    return rect.left >= 0 && rect.top >= 0 && rect.right <= width && rect.bottom <= height &&
        rect.right > rect.left && rect.bottom > rect.top;
}

struct MemoryQueryFixture {
    int calls = 0;
    SystemMemoryListInfo response{};
    uint32_t requiredLength = sizeof(SystemMemoryListInfo) + 32;
};

int32_t FakeMemoryListQuery(uint32_t informationClass, void* buffer, uint32_t bufferLength,
                            uint32_t* returnLength, MemoryQueryFixture& fixture) {
    ++fixture.calls;
    if (informationClass != kSystemMemoryListInformationClass) return static_cast<int32_t>(0xC0000003u);
    if (returnLength) *returnLength = fixture.requiredLength;
    if (bufferLength < fixture.requiredLength) return static_cast<int32_t>(0xC0000004u);
    std::memcpy(buffer, &fixture.response, sizeof(fixture.response));
    return 0;
}
}

int main() {
    Check(TimerResolutionFromX(0, 100, 100, 5100, 156250) == 5100,
        "timer_slider_left_edge_preserves_exact_windows_minimum");
    Check(TimerResolutionFromX(200, 100, 100, 5100, 156250) == 156250,
        "timer_slider_right_edge_preserves_exact_windows_maximum");
    const uint32_t sliderMiddle = TimerResolutionFromX(150, 100, 100, 5100, 156250);
    Check(sliderMiddle >= 5100 && sliderMiddle <= 156250,
        "timer_slider_intermediate_value_stays_in_windows_range");
    Check((sliderMiddle - 5100) % 1000 == 0,
        "timer_slider_uses_consistent_tenth_millisecond_steps_from_non_aligned_minimum");
    Check(TimerResolutionFromX(90, 100, 100, 5100, 156250) == 5100 &&
        TimerResolutionFromX(210, 100, 100, 5100, 156250) == 156250,
        "timer_slider_clamps_drag_outside_track");
    Check(TimerResolutionFromX(125, 100, 100, 5100, 156250) <=
        TimerResolutionFromX(175, 100, 100, 5100, 156250),
        "timer_slider_moves_monotonically");
    bool sliderIsMonotonic = true;
    uint32_t previousSliderValue = 0;
    for (int x = 100; x <= 200; ++x) {
        const uint32_t value = TimerResolutionFromX(x, 100, 100, 5100, 156250);
        if (x > 100 && value < previousSliderValue) sliderIsMonotonic = false;
        previousSliderValue = value;
    }
    Check(sliderIsMonotonic, "timer_slider_is_monotonic_across_entire_track");
    Check(StartupValueNameForPath(L"C:\\Tools\\Photo Editor.exe", {}) == L"Photo Editor",
        "startup_app_name_uses_executable_stem");
    Check(StartupValueNameForPath(L"C:\\Other\\photo editor.EXE", {L"photo editor"}) == L"photo editor (2)",
        "startup_app_name_avoids_case_insensitive_registry_collision");

    CleanerSettings malformedCleanerSettings;
    CleanerStatus malformedCleanerStatus;
    Check(!ParseCleanerSettings(L"", malformedCleanerSettings) &&
        !ParseCleanerStatus(L"", malformedCleanerStatus) &&
        HasProtectedCleanerRegistration(true, true, true) &&
        DecideCleanerSetup(HasProtectedCleanerRegistration(true, true, true), true) ==
            CleanerSetupAction::Ready,
        "malformed_runtime_file_contents_do_not_trigger_cleaner_setup_prompt");
    Check(!HasProtectedCleanerRegistration(true, false, true) &&
        !HasProtectedCleanerRegistration(true, true, false),
        "cleaner_setup_requires_settings_file_and_protected_completion_marker");
    Check(DecideCleanerSetup(true, true) == CleanerSetupAction::Ready,
        "persisted_cleaner_setup_with_current_helper_skips_admin_setup");
    Check(DecideCleanerSetup(true, false) == CleanerSetupAction::Update,
        "persisted_cleaner_setup_with_old_helper_requests_one_update");
    Check(DecideCleanerSetup(false, true) == CleanerSetupAction::Install,
        "missing_cleaner_setup_marker_requests_install");

    Check(CanUseRegisteredCleanerTask(true, true, true) &&
        !CanUseRegisteredCleanerTask(false, true, true) &&
        !CanUseRegisteredCleanerTask(true, false, true) &&
        !CanUseRegisteredCleanerTask(true, true, false),
        "existing_system_task_runs_without_reinstalling_for_a_helper_version_change");
    Check(!ShouldPromptCleanerSetup(true, false, false) &&
        !ShouldPromptCleanerSetup(true, true, false) &&
        !ShouldPromptCleanerSetup(false, true, false) &&
        ShouldPromptCleanerSetup(false, false, false) &&
        ShouldPromptCleanerSetup(false, true, true),
        "auto_toggle_never_prompts_when_system_task_is_ready_or_setup_was_cancelled");
    Check(ShouldSuppressCleanerUpdateRetry(true, false, 2, 2) &&
        !ShouldSuppressCleanerUpdateRetry(true, false, 2, 1) &&
        ShouldSuppressCleanerUpdateRetry(true, true, 2, 2) &&
        ShouldSuppressCleanerUpdateRetry(false, false, 2, 2),
        "failed_cleaner_setup_is_suppressed_for_same_version_until_manual_retry");
    Check(ShouldBlockCleanerSetupRetry(true, false) &&
        !ShouldBlockCleanerSetupRetry(true, true) &&
        !ShouldBlockCleanerSetupRetry(false, false),
        "manual_clean_retries_failed_setup_without_auto_retry_loop");
    Check(StandbyCleanSucceeded(0, 16 * 1024 * 1024, 2 * 1024 * 1024, 4096) &&
        !StandbyCleanSucceeded(0, 16 * 1024 * 1024, 16 * 1024 * 1024, 4096) &&
        !StandbyCleanSucceeded(static_cast<int32_t>(0xC0000061u), 16 * 1024 * 1024, 2 * 1024 * 1024, 4096),
        "standby_clean_succeeds_only_when_windows_succeeds_and_size_drops");
    const std::vector<uint8_t> approvalEnabled{2, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};
    const std::vector<uint8_t> approvalDisabled{3, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};
    Check(ParseStartupApprovalState(approvalEnabled) == StartupApprovalState::Enabled &&
        ParseStartupApprovalState(approvalDisabled) == StartupApprovalState::Disabled,
        "startup_approval_decodes_windows_enabled_and_disabled_states");
    const auto disabledApproval = SetStartupApprovalState(approvalEnabled, false);
    Check(ParseStartupApprovalState(disabledApproval) == StartupApprovalState::Disabled &&
        disabledApproval.size() == 12 && disabledApproval[4] == 1 && disabledApproval[11] == 8,
        "startup_approval_toggle_changes_state_and_preserves_timestamp");
    Check(StartupSourceEnabled(true, StartupApprovalState::Disabled) == false &&
        StartupSourceEnabled(true, StartupApprovalState::Enabled) &&
        StartupSourceEnabled(true, StartupApprovalState::Unknown) &&
        !StartupSourceEnabled(false, StartupApprovalState::Enabled),
        "startup_approval_overrides_active_source_state");
    Check(IsStartupTaskTriggerType(8) && IsStartupTaskTriggerType(9) &&
        !IsStartupTaskTriggerType(2),
        "only_boot_and_logon_tasks_count_as_startup");
    Check(IsProtectedStartupTaskPath(L"\\Microsoft\\Windows\\UpdateOrchestrator") &&
        !IsProtectedStartupTaskPath(L"\\Vendor\\Updater") &&
        StartupEntryCanBeDeleted(true, false) && !StartupEntryCanBeDeleted(false, false) &&
        !StartupEntryCanBeDeleted(true, true) && IsStartupFolderLaunchableFile(L"desktop.lnk") &&
        IsStartupFolderLaunchableFile(L"app.exe") && !IsStartupFolderLaunchableFile(L"notes.txt"),
        "startup_inventory_includes_system_tasks_but_only_deletes_owned_entries");
    Check(StartupEntryPriorityBefore(true, false) && !StartupEntryPriorityBefore(false, true) &&
        !StartupEntryPriorityBefore(true, true),
        "windows_desktop_and_sign_in_entries_are_prioritized_in_startup_list");
    Check(!kShowAllProcessesDefault &&
        ShouldShowProcess(false, true, true) &&
        !ShouldShowProcess(false, true, false) &&
        !ShouldShowProcess(false, false, false) &&
        ShouldShowProcess(true, false, false),
        "process_list_hides_non_user_processes_until_show_all");
    Check(!kAutoCleanDefaultEnabled, "auto_clean_is_off_by_default");
    Check(IsValidCleanerSid(L"S-1-5-21-100-200-300-1001"), "valid_user_sid_is_accepted");
    Check(!IsValidCleanerSid(L"S-1-5-21-100-200-300-1001\\.."), "sid_path_injection_is_rejected");
    Check(!IsValidCleanerSid(L"S-1-5-21-100-200-300-"), "sid_trailing_separator_is_rejected");

    Check(HasExpectedCleanerTaskSecurityDescriptor(
        L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GXGR;;;BU)"),
        "task_acl_accepts_windows_sddl_aliases_and_normalized_right_order");
    Check(HasExpectedCleanerTaskSecurityDescriptor(
        L"O:S-1-5-32-544G:S-1-5-32-544D:P(A;;FA;;;S-1-5-18)(A;;FA;;;S-1-5-32-544)(A;;GRGX;;;S-1-5-32-545)"),
        "task_acl_accepts_sid_strings_from_security_descriptor");
    Check(!HasExpectedCleanerTaskSecurityDescriptor(
        L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;BU)"),
        "task_acl_rejects_user_full_control");
    Check(!HasExpectedCleanerTaskSecurityDescriptor(
        L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;BU)(A;;FA;;;WD)"),
        "task_acl_rejects_unexpected_extra_access");
    Check(!HasExpectedCleanerTaskSecurityDescriptor(
        L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;CI;GRGX;;;BU)"),
        "task_acl_rejects_inheritable_task_access");
    Check(HasExpectedCleanerTaskSecurityDescriptor(
        L"O:BAG:BAD:PAI(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;BU)"),
        "task_acl_accepts_scheduler_auto_inherited_control_flag_with_safe_aces");

    Check(ShouldSuppressCleanerUpdateRetry(true, true, 4, 4),
        "failed_task_repair_stays_blocked_even_with_current_version_marker");
    Check(HasExpectedCleanerTaskSecurityDescriptor(
        L"O:BAG:BAD:(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x1200a9;;;BU)(A;;FR;;;SY)"),
        "task_acl_accepts_actual_windows_scheduler_descriptor");
    Check(CleanerExecutablePathMatches(L"\"C:\\ProgramData\\N-Lite\\N-Lite-Cleaner.exe\"",
        L"C:\\ProgramData\\N-Lite\\N-Lite-Cleaner.exe") &&
        !CleanerExecutablePathMatches(L"\"C:\\ProgramData\\N-Lite\\N-Lite-Cleaner.exe\" --other",
        L"C:\\ProgramData\\N-Lite\\N-Lite-Cleaner.exe"),
        "task_action_accepts_scheduler_quoted_path_without_allowing_extra_arguments");
    SystemMemoryListInfo memoryLists{};
    memoryLists.standby[0] = 2;
    memoryLists.standby[7] = 3;
    Check(sizeof(memoryLists) == 22 * sizeof(uintptr_t), "system_memory_list_information_uses_native_pointer_sized_counts");
    Check(StandbyBytesFromPageCounts(memoryLists, 4096) == 5u * 4096u,
        "standby_bytes_sum_all_eight_priority_buckets");
    memoryLists.freePageCount = 7;
    Check(offsetof(SystemMemoryListInfo, freePageCount) == sizeof(uintptr_t) &&
        offsetof(SystemMemoryListInfo, standby) == 5 * sizeof(uintptr_t) &&
        FreeBytesFromPageCount(memoryLists, 4096) == 7u * 4096u,
        "memory_panel_reads_native_free_and_standby_counters");
    MemoryQueryFixture queryFixture;
    queryFixture.response.standby[2] = 11;
    queryFixture.response.freePageCount = 17;
    SystemMemoryListInfo queriedMemory{};
    const bool queryRetried = QuerySystemMemoryListInfo(
        [&](uint32_t informationClass, void* buffer, uint32_t length, uint32_t* returned) {
            return FakeMemoryListQuery(informationClass, buffer, length, returned, queryFixture);
        }, queriedMemory);
    Check(queryRetried && queryFixture.calls == 2 && queriedMemory.standby[2] == 11 &&
        queriedMemory.freePageCount == 17,
        "standby_query_retries_when_windows_reports_a_larger_buffer_requirement");
    MemoryQueryFixture deniedQuery;
    SystemMemoryListInfo deniedMemory{};
    const bool deniedRead = QuerySystemMemoryListInfo(
        [&](uint32_t, void*, uint32_t, uint32_t*) { return static_cast<int32_t>(0xC0000022u); },
        deniedMemory);
    Check(!deniedRead, "standby_query_reports_access_denied_instead_of_a_zero_size");
    Check(kSystemMemoryListInformationClass == 80 && kMemoryPurgeStandbyListCommand == 4,
        "cleaner_targets_the_standby_list");

    CleanerSettings settings{true, 64, 7200, 7};
    const std::wstring settingsText = SerializeCleanerSettings(settings);
    CleanerSettings parsed{};
    Check(ParseCleanerSettings(settingsText, parsed) && parsed.enabled && parsed.thresholdMb == 64 &&
        parsed.intervalSeconds == 7200 && parsed.manualRequestId == 7,
        "settings_round_trip_preserves_bounded_values");

    CleanerStatus recoveredStatus;
    recoveredStatus.completedManualRequestId = 9;
    recoveredStatus.autoArmed = false;
    Check(!ParseCleanerStatusOrDefault(L"", recoveredStatus) &&
        recoveredStatus.completedManualRequestId == 0 && recoveredStatus.autoArmed,
        "corrupt_cleaner_status_recovers_to_safe_defaults");

    CleanerStatus standbyStatus;
    standbyStatus.completedManualRequestId = 8;
    standbyStatus.standbyValid = true;
    standbyStatus.standbyBytes = 64u * 1024u * 1024u;
    standbyStatus.standbyTick = 123456;
    standbyStatus.manualStandbyValid = true;
    standbyStatus.manualStandbyBefore = 80u * 1024u * 1024u;
    standbyStatus.manualStandbyAfter = 4u * 1024u * 1024u;
    CleanerStatus parsedStandbyStatus;
    Check(ParseCleanerStatus(SerializeCleanerStatus(standbyStatus), parsedStandbyStatus) &&
        parsedStandbyStatus.standbyValid && parsedStandbyStatus.standbyBytes == standbyStatus.standbyBytes &&
        parsedStandbyStatus.standbyTick == standbyStatus.standbyTick &&
        parsedStandbyStatus.manualStandbyValid &&
        parsedStandbyStatus.manualStandbyBefore == standbyStatus.manualStandbyBefore &&
        parsedStandbyStatus.manualStandbyAfter == standbyStatus.manualStandbyAfter,
        "cleaner_status_round_trip_preserves_system_standby_measurements");
    CleanerStatus legacyStatus;
    Check(ParseCleanerStatus(L"version=1\nhelper_version=1\ncompleted_manual_request_id=0\n"
            L"last_manual_status=0\nlast_auto_tick=0\nlast_auto_status=0\nauto_armed=1\n", legacyStatus) &&
        !legacyStatus.standbyValid && legacyStatus.autoArmed,
        "new_app_reads_existing_cleaner_status_without_standby_fields");

    const std::wstring invalidThreshold = L"version=1\nenabled=1\nthreshold_mb=63\ninterval_seconds=60\nmanual_request_id=0\n";
    Check(!ParseCleanerSettings(invalidThreshold, parsed), "invalid_settings_fail_closed");
    const std::wstring invalidInterval = L"version=1\nenabled=1\nthreshold_mb=131073\ninterval_seconds=7201\nmanual_request_id=0\n";
    Check(!ParseCleanerSettings(invalidInterval, parsed), "rejects_values_above_upper_bounds");

    settings.thresholdMb = 64;
    settings.intervalSeconds = 60;
    const uint64_t thresholdBytes = uint64_t{64} * 1024 * 1024;
    Check(ShouldRunAutoClean(settings, thresholdBytes, 120000, 60000, true),
        "auto_clean_requires_threshold_armed_and_elapsed_interval");
    Check(!ShouldRunAutoClean(settings, thresholdBytes - 1, 120000, 60000, true),
        "below_threshold_does_not_auto_clean");
    Check(!ShouldRunAutoClean(settings, thresholdBytes, 120000, 60000, false),
        "unarmed_auto_clean_does_not_repeat");
    Check(!ShouldRunAutoClean(settings, thresholdBytes, 119999, 60000, true),
        "auto_clean_waits_for_interval");
    Check(ManualRequestCompleted(7, 7) && !ManualRequestCompleted(8, 7) && !ManualRequestCompleted(0, 7),
        "manual_request_is_reported_only_after_matching_completion");

    Check(ProcessGroupKey(L"C:/Apps/Widget/widget.exe", 10) ==
        ProcessGroupKey(L"c:\\apps\\WIDGET\\WIDGET.EXE", 11),
        "same_path_groups_case_and_separator_variants");
    Check(ProcessGroupKey(L"C:\\Windows\\explorer.exe", 10) !=
        ProcessGroupKey(L"C:\\Apps\\explorer.exe", 11),
        "same_name_different_paths_stay_separate");
    Check(ProcessGroupKey(L"Path unavailable", 10) != ProcessGroupKey(L"Path unavailable", 11),
        "unavailable_paths_are_pid_unique");

    const std::vector<ProcessSample> samples{
        {10, L"C:\\Apps\\Widget\\widget.exe", 100, 60, 2.5},
        {11, L"c:/apps/widget/WIDGET.EXE", 200, 90, 1.5},
        {12, L"C:\\Other\\widget.exe", 400, 300, 8.0}
    };
    const auto groups = GroupProcessSamples(samples);
    bool sumsExactInstances = false;
    for (const auto& group : groups) {
        if (group.members.size() == 2) {
            sumsExactInstances = group.workingBytes == 300 && group.privateBytes == 150 && group.cpuPercent == 4.0;
        }
    }
    Check(sumsExactInstances && groups.size() == 2, "group_total_sums_only_identical_path_keys");

    for (const auto& dimensions : {std::pair<int, int>{960, 620}, {1240, 830}}) {
        const MemoryLayout layout = ComputeMemoryLayout(dimensions.first, dimensions.second);
        const bool allInside = Inside(layout.header, dimensions.first, dimensions.second) &&
            Inside(layout.content, dimensions.first, dimensions.second) &&
            Inside(layout.title, dimensions.first, dimensions.second) &&
            Inside(layout.memory, dimensions.first, dimensions.second) &&
            Inside(layout.cleaner, dimensions.first, dimensions.second) &&
            Inside(layout.timer, dimensions.first, dimensions.second);
        Check(allInside, dimensions.first == 960 ? "memory_layout_stays_inside_960x620" :
            "memory_layout_stays_inside_1240x830");
        Check(layout.memory.right <= layout.cleaner.left && layout.timer.top >= layout.memory.bottom,
            dimensions.first == 960 ? "memory_panels_and_timer_fit_960x620" :
            "memory_panels_and_timer_fit_1240x830");
    }

    const UiPalette dark = PaletteFor(true), light = PaletteFor(false);
    Check(dark.background != light.background && dark.surface != light.surface && dark.text != light.text,
        "dark_and_light_palettes_are_distinct");
    Check(dark.background != dark.text && light.background != light.text &&
        dark.surface != dark.border && light.surface != light.border,
        "theme_text_and_surface_colors_are_separate");

    return failures == 0 ? 0 : 1;
}
