#pragma once

#include <cstdint>
#include <cstring>
#include <cwctype>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

enum class CleanerSetupAction {
    Install,
    Update,
    Ready
};

// The protected helper/version marker prove setup completed. Per-user settings must exist,
// but mutable status output is recovered by the SYSTEM helper and does not gate setup.
inline bool HasProtectedCleanerRegistration(bool cleanerHelperPresent,
                                            bool helperVersionMarkerValid,
                                            bool perUserSettingsFilePresent) {
    return cleanerHelperPresent && helperVersionMarkerValid && perUserSettingsFilePresent;
}

inline CleanerSetupAction DecideCleanerSetup(bool registrationMarkerValid, bool helperVersionCurrent) {
    if (!registrationMarkerValid) return CleanerSetupAction::Install;
    return helperVersionCurrent ? CleanerSetupAction::Ready : CleanerSetupAction::Update;
}

inline bool CanUseRegisteredCleanerTask(bool taskRegistered, bool helperPresent,
                                        bool settingsReady) {
    return taskRegistered && helperPresent && settingsReady;
}

inline bool ShouldPromptCleanerSetup(bool taskUsable, bool setupBlocked, bool manualRequest) {
    return !taskUsable && (!setupBlocked || manualRequest);
}

inline bool ShouldSuppressCleanerUpdateRetry(bool helperInstalled, bool helperCurrent,
                                             uint32_t currentVersion, uint32_t failedVersion) {
    (void)helperInstalled;
    (void)helperCurrent;
    return currentVersion != 0 &&
        currentVersion == failedVersion;
}

inline bool ShouldBlockCleanerSetupRetry(bool setupBlocked, bool manualRequest) {
    return setupBlocked && !manualRequest;
}

struct CleanerSettings {
    bool enabled = false;
    uint32_t thresholdMb = 4096;
    uint32_t intervalSeconds = 60;
    uint64_t manualRequestId = 0;
};

struct CleanerStatus {
    uint32_t helperVersion = 1;
    uint64_t completedManualRequestId = 0;
    int32_t lastManualStatus = 0;
    uint64_t lastAutoTick = 0;
    int32_t lastAutoStatus = 0;
    bool autoArmed = true;
    bool standbyValid = false;
    uint64_t standbyBytes = 0;
    uint64_t standbyTick = 0;
    bool manualStandbyValid = false;
    uint64_t manualStandbyBefore = 0;
    uint64_t manualStandbyAfter = 0;
    bool autoStandbyCaptured = false;
    bool autoStandbyValid = false;
    uint64_t autoStandbyBefore = 0;
    uint64_t autoStandbyAfter = 0;
};

struct SystemMemoryListInfo {
    uintptr_t zeroPageCount = 0;
    uintptr_t freePageCount = 0;
    uintptr_t modifiedPageCount = 0;
    uintptr_t modifiedNoWritePageCount = 0;
    uintptr_t badPageCount = 0;
    uintptr_t standby[8]{};
    uintptr_t repurposed[8]{};
    uintptr_t modifiedPageCountPageFile = 0;
};

inline constexpr uint32_t kSystemMemoryListInformationClass = 80;
inline constexpr uint32_t kMemoryPurgeStandbyListCommand = 4;

template <typename Query>
inline bool QuerySystemMemoryListInfo(Query query, SystemMemoryListInfo& output,
                                      int32_t* statusOut = nullptr,
                                      uint32_t* returnLengthOut = nullptr) {
    constexpr int32_t kInfoLengthMismatch = static_cast<int32_t>(0xC0000004u);
    constexpr size_t kMaximumBufferSize = 65536;
    std::vector<uint8_t> buffer(sizeof(SystemMemoryListInfo));
    int32_t status = 0;
    uint32_t returnLength = 0;
    for (unsigned attempt = 0; attempt < 6; ++attempt) {
        returnLength = 0;
        status = query(kSystemMemoryListInformationClass, buffer.data(),
            static_cast<uint32_t>(buffer.size()), &returnLength);
        if (status >= 0) {
            if (buffer.size() < sizeof(output)) break;
            std::memcpy(&output, buffer.data(), sizeof(output));
            if (statusOut) *statusOut = status;
            if (returnLengthOut) *returnLengthOut = returnLength;
            return true;
        }
        if (status != kInfoLengthMismatch) break;
        const size_t nextSize = returnLength > buffer.size() ?
            static_cast<size_t>(returnLength) : buffer.size() * 2;
        if (nextSize <= buffer.size() || nextSize > kMaximumBufferSize) break;
        buffer.resize(nextSize);
    }
    if (statusOut) *statusOut = status;
    if (returnLengthOut) *returnLengthOut = returnLength;
    return false;
}

inline bool CleanerPolicyEquals(const std::wstring& left, const wchar_t* right) {
    if (left.size() != std::char_traits<wchar_t>::length(right)) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        if (std::towupper(left[i]) != std::towupper(right[i])) return false;
    }
    return true;
}

inline bool CleanerPolicyMatches(const std::wstring& value, const wchar_t* first,
                                 const wchar_t* second = L"", const wchar_t* third = L"") {
    return CleanerPolicyEquals(value, first) || CleanerPolicyEquals(value, second) ||
        CleanerPolicyEquals(value, third);
}

inline bool CleanerExecutablePathMatches(const std::wstring& actual,
                                         const std::wstring& expected) {
    std::wstring path = actual;
    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
        path = path.substr(1, path.size() - 2);
    return CleanerPolicyEquals(path, expected.c_str());
}

inline bool HasExpectedCleanerTaskSecurityDescriptor(const std::wstring& descriptor) {
    if (descriptor.compare(0, 2, L"O:") != 0) return false;
    size_t ownerEnd = descriptor.find(L"G:", 2);
    const size_t ownerDacl = descriptor.find(L"D:", 2);
    const size_t ownerSacl = descriptor.find(L"S:", 2);
    if (ownerEnd == std::wstring::npos || (ownerDacl != std::wstring::npos && ownerDacl < ownerEnd)) ownerEnd = ownerDacl;
    if (ownerEnd == std::wstring::npos || (ownerSacl != std::wstring::npos && ownerSacl < ownerEnd)) ownerEnd = ownerSacl;
    if (ownerEnd == std::wstring::npos || !CleanerPolicyMatches(
        descriptor.substr(2, ownerEnd - 2), L"BA", L"S-1-5-32-544")) return false;

    const size_t dacl = descriptor.find(L"D:");
    if (dacl == std::wstring::npos) return false;
    const size_t firstAce = descriptor.find(L'(', dacl + 2);
    if (firstAce == std::wstring::npos) return false;
    const std::wstring controls = descriptor.substr(dacl + 2, firstAce - dacl - 2);
    // Scheduler normalizes DACL control flags independently of the effective ACE grants.
    if (!CleanerPolicyMatches(controls, L"", L"P", L"PAI") &&
        !CleanerPolicyEquals(controls, L"AI")) return false;
    size_t daclEnd = descriptor.find(L"S:", dacl + 2);
    if (daclEnd == std::wstring::npos) daclEnd = descriptor.size();

    bool systemFull = false, adminsFull = false, usersRun = false;
    unsigned aceCount = 0;
    size_t open = descriptor.find(L'(', dacl + 2);
    while (open != std::wstring::npos && open < daclEnd) {
        const size_t close = descriptor.find(L')', open + 1);
        if (close == std::wstring::npos || close > daclEnd) return false;
        std::wstring fields[6];
        size_t fieldStart = open + 1;
        unsigned field = 0;
        for (size_t i = fieldStart; i <= close; ++i) {
            if (i == close || descriptor[i] == L';') {
                if (field >= 6) { field = 7; break; }
                fields[field++] = descriptor.substr(fieldStart, i - fieldStart);
                fieldStart = i + 1;
            }
        }
        if (field != 6 || !CleanerPolicyEquals(fields[0], L"A") || !fields[1].empty()) return false;
        ++aceCount;
        const bool full = CleanerPolicyMatches(fields[2], L"FA", L"0X1F01FF");
        const bool run = CleanerPolicyMatches(fields[2], L"GXGR", L"GRGX", L"0X1200A9");
        if (full && CleanerPolicyMatches(fields[5], L"SY", L"S-1-5-18") && !systemFull) systemFull = true;
        else if (full && CleanerPolicyMatches(fields[5], L"BA", L"S-1-5-32-544") && !adminsFull) adminsFull = true;
        else if (run && CleanerPolicyMatches(fields[5], L"BU", L"S-1-5-32-545") && !usersRun) usersRun = true;
        else if (CleanerPolicyMatches(fields[5], L"SY", L"S-1-5-18") &&
            CleanerPolicyMatches(fields[2], L"FR", L"0X120089")) {} // Scheduler-added SYSTEM read ACE.
        else return false;
        open = descriptor.find(L'(', close + 1);
    }
    return (aceCount == 3 || aceCount == 4) && systemFull && adminsFull && usersRun;
}

inline uint64_t StandbyBytesFromPageCounts(const SystemMemoryListInfo& info, uint32_t pageSize) {
    uint64_t pages = 0;
    for (uintptr_t count : info.standby) pages += count;
    return pages * pageSize;
}

inline uint64_t FreeBytesFromPageCount(const SystemMemoryListInfo& info, uint32_t pageSize) {
    return static_cast<uint64_t>(info.freePageCount) * pageSize;
}

inline bool IsValidCleanerSid(const std::wstring& sid) {
    if (sid.size() < 9 || sid.size() > 184 || sid.compare(0, 4, L"S-1-") != 0 || sid.back() == L'-') return false;
    size_t start = 4;
    unsigned parts = 0;
    while (start < sid.size()) {
        size_t end = sid.find(L'-', start);
        if (end == std::wstring::npos) end = sid.size();
        if (end == start || (end - start > 1 && sid[start] == L'0')) return false;
        for (size_t i = start; i < end; ++i) if (!std::iswdigit(sid[i])) return false;
        ++parts;
        start = end + 1;
    }
    return parts >= 3 && parts <= 15;
}

namespace cleaner_policy_detail {
inline bool ParseUnsigned(const std::wstring& value, uint64_t& result) {
    if (value.empty()) return false;
    uint64_t parsed = 0;
    for (wchar_t ch : value) {
        if (ch < L'0' || ch > L'9') return false;
        const uint64_t digit = static_cast<uint64_t>(ch - L'0');
        if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    result = parsed;
    return true;
}

inline bool ParseFields(const std::wstring& text,
                        std::unordered_map<std::wstring, std::wstring>& fields) {
    fields.clear();
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = text.find(L'\n', start);
        const size_t lineEnd = end == std::wstring::npos ? text.size() : end;
        std::wstring line = text.substr(start, lineEnd - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty()) return false;
        const size_t equals = line.find(L'=');
        if (equals == std::wstring::npos || equals == 0 || equals + 1 >= line.size()) return false;
        if (!fields.emplace(line.substr(0, equals), line.substr(equals + 1)).second) return false;
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return !fields.empty();
}

inline bool HasExactFields(const std::unordered_map<std::wstring, std::wstring>& fields,
                           const wchar_t* const* keys, size_t count) {
    if (fields.size() != count) return false;
    for (size_t i = 0; i < count; ++i) if (!fields.count(keys[i])) return false;
    return true;
}
}

inline bool ParseCleanerSettings(const std::wstring& text, CleanerSettings& output) {
    std::unordered_map<std::wstring, std::wstring> fields;
    static const wchar_t* const keys[] = {
        L"version", L"enabled", L"threshold_mb", L"interval_seconds", L"manual_request_id"
    };
    if (!cleaner_policy_detail::ParseFields(text, fields) ||
        !cleaner_policy_detail::HasExactFields(fields, keys, sizeof(keys) / sizeof(keys[0]))) return false;

    uint64_t version = 0, enabled = 0, threshold = 0, interval = 0, requestId = 0;
    if (!cleaner_policy_detail::ParseUnsigned(fields[L"version"], version) || version != 1 ||
        !cleaner_policy_detail::ParseUnsigned(fields[L"enabled"], enabled) || enabled > 1 ||
        !cleaner_policy_detail::ParseUnsigned(fields[L"threshold_mb"], threshold) || threshold < 64 || threshold > 131072 ||
        !cleaner_policy_detail::ParseUnsigned(fields[L"interval_seconds"], interval) || interval < 60 || interval > 7200 ||
        !cleaner_policy_detail::ParseUnsigned(fields[L"manual_request_id"], requestId)) return false;

    CleanerSettings parsed;
    parsed.enabled = enabled != 0;
    parsed.thresholdMb = static_cast<uint32_t>(threshold);
    parsed.intervalSeconds = static_cast<uint32_t>(interval);
    parsed.manualRequestId = requestId;
    output = parsed;
    return true;
}

inline std::wstring SerializeCleanerSettings(const CleanerSettings& settings) {
    return L"version=1\nenabled=" + std::to_wstring(settings.enabled ? 1 : 0) +
        L"\nthreshold_mb=" + std::to_wstring(settings.thresholdMb) +
        L"\ninterval_seconds=" + std::to_wstring(settings.intervalSeconds) +
        L"\nmanual_request_id=" + std::to_wstring(settings.manualRequestId) + L"\n";
}

inline bool ParseCleanerStatus(const std::wstring& text, CleanerStatus& output) {
    std::unordered_map<std::wstring, std::wstring> fields;
    static const wchar_t* const v1Keys[] = {
        L"version", L"helper_version", L"completed_manual_request_id", L"last_manual_status",
        L"last_auto_tick", L"last_auto_status", L"auto_armed"
    };
    static const wchar_t* const v2Keys[] = {
        L"version", L"helper_version", L"completed_manual_request_id", L"last_manual_status",
        L"last_auto_tick", L"last_auto_status", L"auto_armed", L"standby_valid",
        L"standby_bytes", L"standby_tick", L"manual_standby_valid",
        L"manual_standby_before", L"manual_standby_after"
    };
    static const wchar_t* const v3Keys[] = {
        L"version", L"helper_version", L"completed_manual_request_id", L"last_manual_status",
        L"last_auto_tick", L"last_auto_status", L"auto_armed", L"standby_valid",
        L"standby_bytes", L"standby_tick", L"manual_standby_valid",
        L"manual_standby_before", L"manual_standby_after", L"auto_standby_captured",
        L"auto_standby_valid", L"auto_standby_before", L"auto_standby_after"
    };
    if (!cleaner_policy_detail::ParseFields(text, fields)) return false;
    uint64_t version = 0;
    if (!cleaner_policy_detail::ParseUnsigned(fields[L"version"], version)) return false;
    if ((version == 1 && !cleaner_policy_detail::HasExactFields(fields, v1Keys, sizeof(v1Keys) / sizeof(v1Keys[0]))) ||
        (version == 2 && !cleaner_policy_detail::HasExactFields(fields, v2Keys, sizeof(v2Keys) / sizeof(v2Keys[0]))) ||
        (version == 3 && !cleaner_policy_detail::HasExactFields(fields, v3Keys, sizeof(v3Keys) / sizeof(v3Keys[0]))) ||
        (version != 1 && version != 2 && version != 3)) return false;

    uint64_t helperVersion = 0, requestId = 0, manualStatus = 0;
    uint64_t autoTick = 0, autoStatus = 0, armed = 0;
    if (!cleaner_policy_detail::ParseUnsigned(fields[L"helper_version"], helperVersion) || helperVersion > UINT32_MAX ||
        !cleaner_policy_detail::ParseUnsigned(fields[L"completed_manual_request_id"], requestId) ||
        !cleaner_policy_detail::ParseUnsigned(fields[L"last_manual_status"], manualStatus) || manualStatus > UINT32_MAX ||
        !cleaner_policy_detail::ParseUnsigned(fields[L"last_auto_tick"], autoTick) ||
        !cleaner_policy_detail::ParseUnsigned(fields[L"last_auto_status"], autoStatus) || autoStatus > UINT32_MAX ||
        !cleaner_policy_detail::ParseUnsigned(fields[L"auto_armed"], armed) || armed > 1) return false;

    CleanerStatus parsed;
    parsed.helperVersion = static_cast<uint32_t>(helperVersion);
    parsed.completedManualRequestId = requestId;
    parsed.lastManualStatus = static_cast<int32_t>(static_cast<uint32_t>(manualStatus));
    parsed.lastAutoTick = autoTick;
    parsed.lastAutoStatus = static_cast<int32_t>(static_cast<uint32_t>(autoStatus));
    parsed.autoArmed = armed != 0;
    if (version >= 2) {
        uint64_t standbyValid = 0, standbyBytes = 0, standbyTick = 0;
        uint64_t manualStandbyValid = 0, manualStandbyBefore = 0, manualStandbyAfter = 0;
        if (!cleaner_policy_detail::ParseUnsigned(fields[L"standby_valid"], standbyValid) || standbyValid > 1 ||
            !cleaner_policy_detail::ParseUnsigned(fields[L"standby_bytes"], standbyBytes) ||
            !cleaner_policy_detail::ParseUnsigned(fields[L"standby_tick"], standbyTick) ||
            !cleaner_policy_detail::ParseUnsigned(fields[L"manual_standby_valid"], manualStandbyValid) || manualStandbyValid > 1 ||
            !cleaner_policy_detail::ParseUnsigned(fields[L"manual_standby_before"], manualStandbyBefore) ||
            !cleaner_policy_detail::ParseUnsigned(fields[L"manual_standby_after"], manualStandbyAfter)) return false;
        parsed.standbyValid = standbyValid != 0;
        parsed.standbyBytes = standbyBytes;
        parsed.standbyTick = standbyTick;
        parsed.manualStandbyValid = manualStandbyValid != 0;
        parsed.manualStandbyBefore = manualStandbyBefore;
        parsed.manualStandbyAfter = manualStandbyAfter;
    }
    if (version == 3) {
        uint64_t autoStandbyCaptured = 0, autoStandbyValid = 0;
        uint64_t autoStandbyBefore = 0, autoStandbyAfter = 0;
        if (!cleaner_policy_detail::ParseUnsigned(fields[L"auto_standby_captured"], autoStandbyCaptured) ||
            autoStandbyCaptured > 1 ||
            !cleaner_policy_detail::ParseUnsigned(fields[L"auto_standby_valid"], autoStandbyValid) || autoStandbyValid > 1 ||
            !cleaner_policy_detail::ParseUnsigned(fields[L"auto_standby_before"], autoStandbyBefore) ||
            !cleaner_policy_detail::ParseUnsigned(fields[L"auto_standby_after"], autoStandbyAfter)) return false;
        parsed.autoStandbyCaptured = autoStandbyCaptured != 0;
        parsed.autoStandbyValid = autoStandbyValid != 0;
        parsed.autoStandbyBefore = autoStandbyBefore;
        parsed.autoStandbyAfter = autoStandbyAfter;
    }
    output = parsed;
    return true;
}

inline bool ParseCleanerStatusOrDefault(const std::wstring& text, CleanerStatus& output) {
    if (ParseCleanerStatus(text, output)) return true;
    output = CleanerStatus{};
    return false;
}

inline std::wstring SerializeCleanerStatus(const CleanerStatus& status) {
    return L"version=3\nhelper_version=" + std::to_wstring(status.helperVersion) +
        L"\ncompleted_manual_request_id=" + std::to_wstring(status.completedManualRequestId) +
        L"\nlast_manual_status=" + std::to_wstring(static_cast<uint32_t>(status.lastManualStatus)) +
        L"\nlast_auto_tick=" + std::to_wstring(status.lastAutoTick) +
        L"\nlast_auto_status=" + std::to_wstring(static_cast<uint32_t>(status.lastAutoStatus)) +
        L"\nauto_armed=" + std::to_wstring(status.autoArmed ? 1 : 0) +
        L"\nstandby_valid=" + std::to_wstring(status.standbyValid ? 1 : 0) +
        L"\nstandby_bytes=" + std::to_wstring(status.standbyBytes) +
        L"\nstandby_tick=" + std::to_wstring(status.standbyTick) +
        L"\nmanual_standby_valid=" + std::to_wstring(status.manualStandbyValid ? 1 : 0) +
        L"\nmanual_standby_before=" + std::to_wstring(status.manualStandbyBefore) +
        L"\nmanual_standby_after=" + std::to_wstring(status.manualStandbyAfter) +
        L"\nauto_standby_captured=" + std::to_wstring(status.autoStandbyCaptured ? 1 : 0) +
        L"\nauto_standby_valid=" + std::to_wstring(status.autoStandbyValid ? 1 : 0) +
        L"\nauto_standby_before=" + std::to_wstring(status.autoStandbyBefore) +
        L"\nauto_standby_after=" + std::to_wstring(status.autoStandbyAfter) + L"\n";
}

inline bool StandbyCleanSucceeded(int32_t status, uint64_t beforeBytes, uint64_t afterBytes,
                                  uint64_t pageSize) {
    return status >= 0 && beforeBytes > afterBytes &&
        (pageSize == 0 || beforeBytes - afterBytes >= pageSize);
}

inline bool AutoCleanNeedsRetry(int32_t status, bool afterValid, uint64_t beforeBytes,
                                uint64_t afterBytes, uint64_t pageSize) {
    return !afterValid || !StandbyCleanSucceeded(status, beforeBytes, afterBytes, pageSize);
}

inline bool ManualRequestCompleted(uint64_t requestId, uint64_t completedRequestId) {
    return requestId != 0 && completedRequestId >= requestId;
}

inline bool ShouldRunAutoClean(const CleanerSettings& settings, uint64_t standbyBytes,
                               uint64_t nowTick, uint64_t lastTick, bool armed) {
    if (!settings.enabled || !armed || settings.thresholdMb < 64 || settings.thresholdMb > 131072 ||
        settings.intervalSeconds < 60 || settings.intervalSeconds > 7200) return false;
    const uint64_t threshold = static_cast<uint64_t>(settings.thresholdMb) * 1024u * 1024u;
    if (standbyBytes < threshold) return false;
    return lastTick == 0 || nowTick < lastTick || nowTick - lastTick >=
        static_cast<uint64_t>(settings.intervalSeconds) * 1000u;
}

inline constexpr bool kAutoCleanDefaultEnabled = false;
