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
