#pragma once

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

inline std::wstring StartupValueNameForPath(const std::wstring& path,
    const std::vector<std::wstring>& existingNames) {
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name.resize(dot);
    if (name.empty()) name = L"Application";

    auto equalsIgnoreCase = [](const std::wstring& a, const std::wstring& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
            [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
    };
    const std::wstring base = name;
    unsigned suffix = 2;
    while (std::any_of(existingNames.begin(), existingNames.end(),
        [&](const std::wstring& existing) { return equalsIgnoreCase(existing, name); })) {
        name = base + L" (" + std::to_wstring(suffix++) + L")";
    }
    return name;
}

enum class StartupApprovalState { Enabled, Disabled, Unknown };

inline StartupApprovalState ParseStartupApprovalState(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(uint32_t)) return StartupApprovalState::Unknown;
    const uint32_t state = static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8) |
        (static_cast<uint32_t>(data[2]) << 16) |
        (static_cast<uint32_t>(data[3]) << 24);
    if (state == 2) return StartupApprovalState::Enabled;
    if (state == 3) return StartupApprovalState::Disabled;
    return StartupApprovalState::Unknown;
}

inline std::vector<uint8_t> SetStartupApprovalState(const std::vector<uint8_t>& existing, bool enabled) {
    std::vector<uint8_t> data = existing;
    if (data.size() < 12) data.resize(12, 0);
    const uint32_t state = enabled ? 2u : 3u;
    data[0] = static_cast<uint8_t>(state & 0xff);
    data[1] = static_cast<uint8_t>((state >> 8) & 0xff);
    data[2] = static_cast<uint8_t>((state >> 16) & 0xff);
    data[3] = static_cast<uint8_t>((state >> 24) & 0xff);
    return data;
}

inline bool StartupSourceEnabled(bool sourcePresent, StartupApprovalState state) {
    if (!sourcePresent) return false;
    return state != StartupApprovalState::Disabled;
}

inline bool IsStartupTaskTriggerType(int type) {
    return type == 8 || type == 9;
}

inline bool IsProtectedStartupTaskPath(const std::wstring& path) {
    constexpr wchar_t prefix[] = L"\\Microsoft";
    constexpr size_t prefixLength = sizeof(prefix) / sizeof(prefix[0]) - 1;
    if (path.size() < prefixLength ||
        !std::equal(prefix, prefix + prefixLength, path.begin(),
            [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); })) return false;
    return path.size() == prefixLength || path[prefixLength] == L'\\';
}

inline bool StartupEntryCanBeDeleted(bool currentUserOwned, bool protectedSource) {
    return currentUserOwned && !protectedSource;
}

inline bool IsStartupFolderLaunchableFile(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring extension = name.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return extension == L".lnk" || extension == L".url" || extension == L".exe" ||
        extension == L".bat" || extension == L".cmd" || extension == L".vbs" || extension == L".js";
}
