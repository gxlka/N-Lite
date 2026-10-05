#pragma once

#include <windows.h>
#ifdef NLITE_STARTUP_TESTING
#include <commdlg.h>
#endif
#include <string>
#include <vector>

enum class StartupKind {
    UserRun, UserRunOnce, MachineRun, MachineRunOnce,
    UserFolder, CommonFolder, ScheduledTask, WindowsShell
};

struct StartupItem {
    std::wstring name, command, source, path;
    std::wstring taskPath, approvalName, approvalSubkey;
    StartupKind kind = StartupKind::UserRun;
    DWORD registryView = KEY_WOW64_64KEY, valueType = REG_SZ;
    std::vector<BYTE> rawData;
    bool enabled = true, canToggle = false, canDelete = false, disabledBackup = false;
    bool approvalManaged = false;
};

std::vector<StartupItem> EnumerateStartupItems();
bool SetStartupItemEnabled(StartupItem& item, bool enabled);
bool DeleteStartupItem(StartupItem& item);
bool AddStartupApplication(HWND owner, std::wstring& addedName);
#ifdef NLITE_STARTUP_TESTING
using StartupFilePickerForTesting = BOOL (WINAPI *)(LPOPENFILENAMEW);
void SetStartupFilePickerForTesting(StartupFilePickerForTesting picker);
#endif
