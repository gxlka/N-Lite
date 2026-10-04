#pragma once

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>
#include <unordered_map>
#include <vector>

struct ProcessSample {
    unsigned long pid = 0;
    std::wstring path;
    uint64_t workingBytes = 0;
    uint64_t privateBytes = 0;
    double cpuPercent = 0.0;
};

struct ProcessGroup {
    std::wstring key;
    std::wstring path;
    std::vector<size_t> members;
    size_t representativeIndex = 0;
    unsigned long representativePid = 0;
    uint64_t workingBytes = 0;
    uint64_t privateBytes = 0;
    double cpuPercent = 0.0;
};

inline std::wstring ProcessGroupKey(const std::wstring& path, unsigned long pid) {
    if (path.empty()) return L"pid:" + std::to_wstring(pid);
    std::wstring normalized = path;
    for (wchar_t& ch : normalized) {
        if (ch == L'/') ch = L'\\';
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    if (normalized == L"path unavailable") return L"pid:" + std::to_wstring(pid);
    const std::wstring longPathPrefix = L"\\\\?\\";
    if (normalized.compare(0, longPathPrefix.size(), longPathPrefix) == 0)
        normalized.erase(0, longPathPrefix.size());
    return L"path:" + normalized;
}

inline std::vector<ProcessGroup> GroupProcessSamples(const std::vector<ProcessSample>& samples) {
    std::vector<ProcessGroup> groups;
    std::unordered_map<std::wstring, size_t> groupIndex;
    for (size_t i = 0; i < samples.size(); ++i) {
        const ProcessSample& sample = samples[i];
        std::wstring key = ProcessGroupKey(sample.path, sample.pid);
        auto found = groupIndex.find(key);
        if (found == groupIndex.end()) {
            ProcessGroup group;
            group.key = key;
            group.path = sample.path;
            group.representativeIndex = i;
            group.representativePid = sample.pid;
            groups.push_back(std::move(group));
            const size_t index = groups.size() - 1;
            groupIndex.emplace(key, index);
            found = groupIndex.find(key);
        }
        ProcessGroup& group = groups[found->second];
        group.members.push_back(i);
        if (sample.pid < group.representativePid) {
            group.representativeIndex = i;
            group.representativePid = sample.pid;
        }
        group.workingBytes += sample.workingBytes;
        group.privateBytes += sample.privateBytes;
        group.cpuPercent += sample.cpuPercent;
    }
    return groups;
}
