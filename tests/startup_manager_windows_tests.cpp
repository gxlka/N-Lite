#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <iostream>
#include <vector>

#include "startup_manager.h"
#include "startup_policy.h"

namespace {
bool Check(bool condition, const char* name) {
    std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
    return condition;
}

bool ReadApproval(const wchar_t* keyPath, const wchar_t* valueName,
    StartupApprovalState& state, std::vector<BYTE>& bytes) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0, size = 0;
    LONG result = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size);
    if (result == ERROR_SUCCESS && type == REG_BINARY) {
        bytes.resize(size);
        result = RegQueryValueExW(key, valueName, nullptr, &type, bytes.data(), &size);
        if (result == ERROR_SUCCESS) {
            bytes.resize(size);
            std::vector<uint8_t> data(bytes.begin(), bytes.end());
            state = ParseStartupApprovalState(data);
        }
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_BINARY;
}
}

int main() {
    const wchar_t* keyPath = L"Software\\N-Lite\\StartupTests\\StartupApproved\\Run";
    const wchar_t* valueName = L"TestEntry";
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\N-Lite\\StartupTests");

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0,
        KEY_SET_VALUE | KEY_QUERY_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        std::cerr << "FAIL test registry key creation\n";
        return 1;
    }
    const BYTE initial[] = {2, 0, 0, 0, 10, 11, 12, 13, 14, 15, 16, 17};
    LONG result = RegSetValueExW(key, valueName, 0, REG_BINARY, initial, sizeof(initial));
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        std::cerr << "FAIL initial approval-state write\n";
        return 1;
    }

    StartupItem item;
    item.kind = StartupKind::UserRun;
    item.name = valueName;
    item.approvalName = valueName;
    item.approvalSubkey = keyPath;
    item.registryView = KEY_WOW64_64KEY;
    item.canToggle = true;
    item.approvalManaged = true;

    bool ok = true;
    StartupApprovalState state = StartupApprovalState::Unknown;
    std::vector<BYTE> bytes;
    ok &= Check(SetStartupItemEnabled(item, false) && !item.enabled,
        "startup_toggle_disables_native_approval_state");
    ok &= Check(ReadApproval(keyPath, valueName, state, bytes) &&
        state == StartupApprovalState::Disabled && bytes.size() == sizeof(initial) && bytes[4] == 10 && bytes[11] == 17,
        "startup_disable_preserves_approval_timestamp");
    ok &= Check(SetStartupItemEnabled(item, true) && item.enabled,
        "startup_toggle_enables_native_approval_state");
    ok &= Check(ReadApproval(keyPath, valueName, state, bytes) &&
        state == StartupApprovalState::Enabled && bytes.size() == sizeof(initial) && bytes[4] == 10 && bytes[11] == 17,
        "startup_enable_preserves_approval_timestamp");

    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\N-Lite\\StartupTests");
    return ok ? 0 : 1;
}
