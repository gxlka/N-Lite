#pragma once

#include <cstdint>
#include <cwctype>
#include <limits>
#include <string>
#include <unordered_map>

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
};

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
    static const wchar_t* const keys[] = {
        L"version", L"helper_version", L"completed_manual_request_id", L"last_manual_status",
        L"last_auto_tick", L"last_auto_status", L"auto_armed"
    };
    if (!cleaner_policy_detail::ParseFields(text, fields) ||
        !cleaner_policy_detail::HasExactFields(fields, keys, sizeof(keys) / sizeof(keys[0]))) return false;

    uint64_t version = 0, helperVersion = 0, requestId = 0, manualStatus = 0;
    uint64_t autoTick = 0, autoStatus = 0, armed = 0;
    if (!cleaner_policy_detail::ParseUnsigned(fields[L"version"], version) || version != 1 ||
        !cleaner_policy_detail::ParseUnsigned(fields[L"helper_version"], helperVersion) || helperVersion > UINT32_MAX ||
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
    output = parsed;
    return true;
}

inline std::wstring SerializeCleanerStatus(const CleanerStatus& status) {
    return L"version=1\nhelper_version=" + std::to_wstring(status.helperVersion) +
        L"\ncompleted_manual_request_id=" + std::to_wstring(status.completedManualRequestId) +
        L"\nlast_manual_status=" + std::to_wstring(static_cast<uint32_t>(status.lastManualStatus)) +
        L"\nlast_auto_tick=" + std::to_wstring(status.lastAutoTick) +
        L"\nlast_auto_status=" + std::to_wstring(static_cast<uint32_t>(status.lastAutoStatus)) +
        L"\nauto_armed=" + std::to_wstring(status.autoArmed ? 1 : 0) + L"\n";
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
