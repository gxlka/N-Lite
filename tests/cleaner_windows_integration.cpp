#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <iostream>
static unsigned elevationAttempts = 0;
static BOOL WINAPI RejectElevation(SHELLEXECUTEINFOW*) {
    ++elevationAttempts; SetLastError(ERROR_CANCELLED); return FALSE;
}
#define NLITE_CLEANER_TEST_DIAGNOSTICS
#define ShellExecuteExW RejectElevation
#define wWinMain NliteGuiMain
#include "../src/main.cpp"
#undef wWinMain
#undef ShellExecuteExW

static bool Check(bool ok, const char* label) {
    std::cout << (ok ? "PASS " : "FAIL ") << label << " error=" << GetLastError() << std::endl;
    return ok;
}
static bool WaitStatus(bool manual, uint64_t previousTick) {
    const ULONGLONG deadline = GetTickCount64() + 30000;
    while (GetTickCount64() < deadline) {
        CleanerStatus status;
        if (ReadCleanerStatus(status) && (manual ?
            ManualRequestCompleted(gCleanerRequestId, status.completedManualRequestId) :
            status.standbyTick > previousTick)) {
            std::cout << "standby=" << status.standbyBytes << " before=" << status.manualStandbyBefore
                << " after=" << status.manualStandbyAfter << " ntstatus=" << status.lastManualStatus << std::endl;
            return status.standbyValid && (!manual || (status.lastManualStatus >= 0 && status.manualStandbyValid &&
                StandbyCleanSucceeded(status.lastManualStatus, status.manualStandbyBefore, status.manualStandbyAfter, gPageSize)));
        }
        Sleep(100);
    }
    return false;
}
static int NonAdminTest() {
    BYTE adminSid[SECURITY_MAX_SID_SIZE]; DWORD size = sizeof(adminSid); BOOL admin = FALSE;
    CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminSid, &size);
    CheckTokenMembership(nullptr, adminSid, &admin);
    if (!Check(!admin, "real non-admin token")) return 1;
    gUserSid = CurrentUserSid(); gCleanerRoot = ProgramDataNlite();
    wchar_t exe[32768]; GetModuleFileNameW(nullptr, exe, 32768); gExePath = exe;
    LoadNt(); LoadSettings();
    std::cout << "helper=" << CleanerHelperPresent() << " settings=" << CleanerSettingsReady() << std::endl;
    bool ok = Check(gCleanerTaskUsable, "existing SYSTEM task recognized by app");
    if (!ok) return 1;
    gAutoPurge = false; gThresholdMB = 64; SaveSettings();
    gCleanerTaskUsable = false; gCleanerSetupBlocked = true;
    gCleanerSetupProcess = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SaveToggleAuto();
    ok &= Check(gAutoPurge && elevationAttempts == 0, "toggle joins pending setup without a second elevation");
    CloseHandle(gCleanerSetupProcess); gCleanerSetupProcess = nullptr;
    gCleanerSetupForAuto = false; gCleanerSetupBlocked = false; gCleanerTaskUsable = true;
    gAutoPurge = false; SaveSettings();
    CleanerStatus previous; ReadCleanerStatus(previous);
    SaveToggleAuto();
    ok &= Check(gAutoPurge, "auto clean remains enabled");
    ok &= Check(WaitStatus(false, previous.standbyTick), "SYSTEM helper produces fresh standby sample");
    CleanerStatus automatic; ReadCleanerStatus(automatic);
    ok &= Check(automatic.lastAutoTick > 0 && automatic.lastAutoStatus >= 0, "automatic standby purge ran as SYSTEM");
    SaveToggleAuto(); SaveToggleAuto();
    ok &= Check(elevationAttempts == 0, "repeated toggles never invoke runas");
    LoadSettings();
    ok &= Check(gAutoPurge && gCleanerTaskUsable, "saved enabled state reloads without setup");
    ok &= Check(RequestCleanerTaskRun(true), "non-admin manual task dispatch");
    ok &= Check(WaitStatus(true, 0), "SYSTEM standby purge and before/after measurement");
    PollCleanerStatus();
    ok &= Check(!gManualCleanerPending, "app consumes completed request");
    ok &= Check(elevationAttempts == 0, "manual clean and state reload never invoke runas");
    return ok ? 0 : 1;
}
static DWORD Launch(std::wstring command, HANDLE token = nullptr) {
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE); startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE); PROCESS_INFORMATION process{};
    BOOL started = token ? CreateProcessAsUserW(token, nullptr, command.data(), nullptr, nullptr,
        TRUE, 0, nullptr, nullptr, &startup, &process) : CreateProcessW(nullptr, command.data(),
        nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process);
    if (!Check(started != FALSE, "launch integration process")) return 999;
    DWORD result = 998;
    if (WaitForSingleObject(process.hProcess, 90000) == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &result);
    else TerminateProcess(process.hProcess, result);
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return result;
}
int wmain(int argc, wchar_t** argv) {
    if (argc > 1) return NonAdminTest();
    gUserSid = CurrentUserSid(); gCleanerRoot = ProgramDataNlite();
    wchar_t exe[32768]; GetModuleFileNameW(nullptr, exe, 32768); gExePath = exe;
    const std::wstring helper = PackagedCleanerPath();
    if (!Check(Launch(L"\"" + helper + L"\" --install " + gUserSid) == 0, "install protected cleaner")) return 1;
    HANDLE original = nullptr, restricted = nullptr;
    BYTE adminSid[SECURITY_MAX_SID_SIZE]; DWORD size = sizeof(adminSid);
    CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminSid, &size);
    SID_AND_ATTRIBUTES deny{adminSid, 0};
    bool ok = OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &original) &&
        CreateRestrictedToken(original, DISABLE_MAX_PRIVILEGE, 1, &deny, 0, nullptr, 0, nullptr, &restricted);
    if (Check(ok, "create restricted non-admin token")) {
        ok &= Check(Launch(L"\"" + gExePath + L"\" --child", restricted) == 0, "first non-admin app session");
        ok &= Check(Launch(L"\"" + gExePath + L"\" --child", restricted) == 0, "fresh process with persisted task and settings");
        DeleteFileW((gCleanerRoot + L"\\helper-version.txt").c_str());
        ok &= Check(Launch(L"\"" + gExePath + L"\" --child", restricted) == 0, "missing version marker still reuses existing task without elevation");
    }
    CleanerStatus before; ReadCleanerStatus(before);
    const ULONGLONG deadline = GetTickCount64() + 75000; bool scheduled = false;
    while (ok && GetTickCount64() < deadline) {
        CleanerStatus next;
        if (ReadCleanerStatus(next) && next.standbyValid && next.standbyTick > before.standbyTick) {
            scheduled = true; break;
        }
        Sleep(100);
    }
    ok &= Check(scheduled, "minute trigger runs helper with all app sessions closed");
    if (restricted) CloseHandle(restricted); if (original) CloseHandle(original);
    Launch(L"\"" + helper + L"\" --uninstall " + gUserSid);
    return ok ? 0 : 1;
}
