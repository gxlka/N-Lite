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

    const wchar_t* runPath=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* deleteValue=L"N-Lite-Startup-DeleteTest";
    const wchar_t* approvalPath=L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
    HKEY runKey=nullptr;
    LONG runResult=RegCreateKeyExW(HKEY_CURRENT_USER,runPath,0,nullptr,0,
        KEY_SET_VALUE|KEY_QUERY_VALUE|KEY_WOW64_64KEY,nullptr,&runKey,nullptr);
    const wchar_t testCommand[]=L"test startup command";
    if(runResult==ERROR_SUCCESS){
        RegDeleteValueW(runKey,deleteValue);
        runResult=RegSetValueExW(runKey,deleteValue,0,REG_SZ,
            reinterpret_cast<const BYTE*>(testCommand),sizeof(testCommand));
        RegCloseKey(runKey);
    }
    HKEY approvalKey=nullptr;
    LONG approvalResult=RegCreateKeyExW(HKEY_CURRENT_USER,approvalPath,0,nullptr,0,
        KEY_SET_VALUE|KEY_QUERY_VALUE|KEY_WOW64_64KEY,nullptr,&approvalKey,nullptr);
    if(approvalResult==ERROR_SUCCESS){
        RegSetValueExW(approvalKey,deleteValue,0,REG_BINARY,initial,sizeof(initial));
        RegCloseKey(approvalKey);
    }
    StartupItem removable;
    removable.kind=StartupKind::UserRun;removable.name=deleteValue;
    removable.approvalName=deleteValue;removable.approvalSubkey=approvalPath;
    removable.registryView=KEY_WOW64_64KEY;removable.canDelete=true;
    ok &= Check(runResult==ERROR_SUCCESS&&approvalResult==ERROR_SUCCESS&&DeleteStartupItem(removable),
        "startup_bin_deletes_only_the_user_run_entry");
    runKey=nullptr;approvalKey=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,runPath,0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&runKey)==ERROR_SUCCESS){
        DWORD remainsSize=0;
        const LONG remains=RegQueryValueExW(runKey,deleteValue,nullptr,nullptr,nullptr,&remainsSize);
        RegCloseKey(runKey);
        ok &= Check(remains==ERROR_FILE_NOT_FOUND,"startup_bin_removes_the_run_value");
    }else ok &= Check(false,"startup_bin_removes_the_run_value");
    if(RegOpenKeyExW(HKEY_CURRENT_USER,approvalPath,0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&approvalKey)==ERROR_SUCCESS){
        DWORD remainsSize=0;
        const LONG remains=RegQueryValueExW(approvalKey,deleteValue,nullptr,nullptr,nullptr,&remainsSize);
        RegCloseKey(approvalKey);
        ok &= Check(remains==ERROR_FILE_NOT_FOUND,"startup_bin_cleans_matching_approval_state");
    }else ok &= Check(false,"startup_bin_cleans_matching_approval_state");
    if(RegOpenKeyExW(HKEY_CURRENT_USER,runPath,0,KEY_SET_VALUE|KEY_WOW64_64KEY,&runKey)==ERROR_SUCCESS){
        RegDeleteValueW(runKey,deleteValue);RegCloseKey(runKey);
    }
    if(RegOpenKeyExW(HKEY_CURRENT_USER,approvalPath,0,KEY_SET_VALUE|KEY_WOW64_64KEY,&approvalKey)==ERROR_SUCCESS){
        RegDeleteValueW(approvalKey,deleteValue);RegCloseKey(approvalKey);
    }

    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\N-Lite\\StartupTests");
    return ok ? 0 : 1;
}
