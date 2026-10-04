#pragma once

#include <windows.h>
#include <string>
#include <vector>

enum class StartupKind { UserRun, UserRunOnce, MachineRun, MachineRunOnce, UserFolder, CommonFolder };

struct StartupItem {
    std::wstring name, command, source, path;
    StartupKind kind = StartupKind::UserRun;
    DWORD registryView = KEY_WOW64_64KEY, valueType = REG_SZ;
    std::vector<BYTE> rawData;
    bool enabled = true, canToggle = false, disabledBackup = false;
};

std::vector<StartupItem> EnumerateStartupItems();
bool SetStartupItemEnabled(StartupItem& item, bool enabled);
bool AddStartupApplication(HWND owner, std::wstring& addedName);
