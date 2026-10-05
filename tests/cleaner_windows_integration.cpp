#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <iostream>
#include <string>
static unsigned elevationAttempts = 0;
static BOOL WINAPI RejectElevation(SHELLEXECUTEINFOW*) {
    ++elevationAttempts; SetLastError(ERROR_CANCELLED); return FALSE;
}
static unsigned trayBalloonCount = 0;
static std::wstring trayBalloonTitle, trayBalloonText;
static BOOL WINAPI CaptureTrayNotification(DWORD, PNOTIFYICONDATAW);
static std::wstring startupDialogSelection;
static BOOL WINAPI CaptureStartupFile(LPOPENFILENAMEW);
#define NLITE_CLEANER_TEST_DIAGNOSTICS
#define ShellExecuteExW RejectElevation
#define Shell_NotifyIconW CaptureTrayNotification
#define wWinMain NliteGuiMain
#include "../src/main.cpp"
#undef wWinMain
#undef Shell_NotifyIconW
#undef ShellExecuteExW

static CleanerStatus syntheticCleanerStatus;
static bool ReadSyntheticCleanerStatus(CleanerStatus& status) {
    status = syntheticCleanerStatus;
    return true;
}

static BOOL WINAPI CaptureTrayNotification(DWORD action, PNOTIFYICONDATAW notification) {
    if (action == NIM_MODIFY && notification && (notification->uFlags & NIF_INFO)) {
        ++trayBalloonCount;
        trayBalloonTitle = notification->szInfoTitle;
        trayBalloonText = notification->szInfo;
    }
    return TRUE;
}
static BOOL WINAPI CaptureStartupFile(LPOPENFILENAMEW dialog) {
    if (!dialog || !dialog->lpstrFile || startupDialogSelection.empty()) {
        SetLastError(ERROR_CANCELLED);
        return FALSE;
    }
    return wcsncpy_s(dialog->lpstrFile, static_cast<size_t>(dialog->nMaxFile),
        startupDialogSelection.c_str(), _TRUNCATE) == 0 ? TRUE : FALSE;
}

static bool Check(bool ok, const char* label) {
    std::cout << (ok ? "PASS " : "FAIL ") << label << " error=" << GetLastError() << std::endl;
    return ok;
}
static bool WaitStatus(bool manual, uint64_t previousTick) {
    const ULONGLONG deadline = GetTickCount64() + 30000;
    ULONGLONG lastRefresh = GetTickCount64();
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
        if (manual && GetTickCount64() - lastRefresh >= 1000) {
            // Use the real GUI timer: a busy task may coalesce a Run request.
            WndProc(nullptr, WM_TIMER, TIMER_REFRESH, 0);
            lastRefresh = GetTickCount64();
        }
        Sleep(100);
    }
    return false;
}
static int StartupUiTest() {
    const std::wstring valueName = L"N-Lite Startup UI Test " +
        std::to_wstring(GetCurrentProcessId()) + L" " + std::to_wstring(GetTickCount64());
    const wchar_t* runPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* approvalPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
    HKEY run = nullptr;
    bool ok = true;
    elevationAttempts = 0;
    HWND controller = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
        0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ok &= Check(controller != nullptr, "hidden UI test window created");
    if (!controller) return 1;
    gWnd = controller;
    gTrayAdded = false;
    gPage = 2;
    gStartupScroll = 0;
    ApplyThemeColors();
    gFont = CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    gFontSmall = CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    gFontMed = CreateFontW(-16,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    gFontTitle = CreateFontW(-27,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");

    gStartupEntries = EnumerateStartupItems();
    SetStartupFilePickerForTesting(CaptureStartupFile);
    auto findTestEntry = [&]() {
        return std::find_if(gStartupEntries.begin(), gStartupEntries.end(), [&](const StartupItem& item) {
            return item.kind == StartupKind::UserRun && item.registryView == KEY_WOW64_64KEY &&
                item.name == valueName;
        });
    };
    HDC screen = GetDC(nullptr);
    HDC canvas = screen ? CreateCompatibleDC(screen) : nullptr;
    HBITMAP bitmap = screen ? CreateCompatibleBitmap(screen, 1240, 830) : nullptr;
    HGDIOBJ old = canvas && bitmap ? SelectObject(canvas, bitmap) : nullptr;
    ok &= Check(canvas && bitmap && old && old != HGDI_ERROR, "startup UI canvas created");
    if (canvas && bitmap && old && old != HGDI_ERROR) {
        const bool oldTaskUsable = gCleanerTaskUsable;
        gCleanerTaskUsable = true;
        gCleanerStatus = CleanerStatus{};
        gCleanerStatus.standbyValid = true;
        gCleanerStatus.standbyBytes = 128ull * 1024 * 1024;
        gCleanerStatus.lastAutoTick = 100;
        gLastSeenPurgeTick = 100;
        gTrayAdded = true;
        trayBalloonCount = 0;
        syntheticCleanerStatus = CleanerStatus{};
        syntheticCleanerStatus.standbyValid = true;
        syntheticCleanerStatus.standbyBytes = 96ull * 1024 * 1024;
        syntheticCleanerStatus.lastAutoTick = 200;
        syntheticCleanerStatus.lastAutoStatus = 0;
        syntheticCleanerStatus.autoStandbyCaptured = true;
        syntheticCleanerStatus.autoStandbyValid = true;
        syntheticCleanerStatus.autoStandbyBefore = 128ull * 1024 * 1024;
        syntheticCleanerStatus.autoStandbyAfter = 64ull * 1024 * 1024;
        gCleanerStatusReaderForTests = ReadSyntheticCleanerStatus;
        WndProc(controller, WM_TIMER, TIMER_REFRESH, 0);
        ok &= Check(trayBalloonCount == 1 && trayBalloonTitle == L"N-Lite" &&
            trayBalloonText.find(L"Auto clean succeeded:") == 0 &&
            trayBalloonText.find(L"64 MB") != std::wstring::npos,
            "hidden tray reports the helper's verified auto-clean before/after result");

        gCleanerStatus = CleanerStatus{};
        gCleanerStatus.standbyValid = true;
        gCleanerStatus.standbyBytes = 128ull * 1024 * 1024;
        gCleanerStatus.lastAutoTick = 200;
        gLastSeenPurgeTick = 200;
        syntheticCleanerStatus.standbyBytes = 0;
        syntheticCleanerStatus.lastAutoTick = 300;
        syntheticCleanerStatus.autoStandbyBefore = 64ull * 1024 * 1024;
        syntheticCleanerStatus.autoStandbyAfter = 64ull * 1024 * 1024;
        syntheticCleanerStatus.autoStandbyCaptured = true;
        trayBalloonCount = 0;
        WndProc(controller, WM_TIMER, TIMER_REFRESH, 0);
        ok &= Check(trayBalloonCount == 1 &&
            trayBalloonText.find(L"Auto clean failed: standby size unchanged") == 0,
            "hidden tray does not report unrelated standby changes as a clean success");

        gCleanerStatus.lastAutoTick = 300;
        gLastSeenPurgeTick = 300;
        syntheticCleanerStatus.lastAutoTick = 400;
        syntheticCleanerStatus.standbyValid = true;
        syntheticCleanerStatus.standbyBytes = 0;
        syntheticCleanerStatus.autoStandbyCaptured = true;
        syntheticCleanerStatus.autoStandbyValid = false;
        syntheticCleanerStatus.autoStandbyBefore = 64ull * 1024 * 1024;
        syntheticCleanerStatus.autoStandbyAfter = 0;
        trayBalloonCount = 0;
        WndProc(controller, WM_TIMER, TIMER_REFRESH, 0);
        ok &= Check(trayBalloonCount == 1 &&
            trayBalloonText.find(L"Auto clean failed: standby size unchanged or unavailable") == 0,
            "hidden tray treats an unavailable auto-clean measurement as unverifiable");
        gCleanerStatusReaderForTests = nullptr;
        gCleanerTaskUsable = oldTaskUsable;
        gTrayAdded = false;
    }
    if (canvas && bitmap && old && old != HGDI_ERROR) {
        auto paint = [&]() { Paint(canvas, 1240, 830); };
        auto clickHit = [&](int id) {
            const Hit* hit = nullptr;
            for (auto it = gHits.rbegin(); it != gHits.rend(); ++it) {
                if (it->id == id && it->data == 0) { hit = &*it; break; }
            }
            if (!hit) return false;
            const int x = hit->r.left + W(hit->r) / 2;
            const int y = hit->r.top + H(hit->r) / 2;
            WndProc(controller, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            return true;
        };
        paint();
        ok &= Check(std::any_of(gHits.begin(), gHits.end(), [](const Hit& hit) { return hit.id == ID_STARTUP_ADD; }) &&
            std::any_of(gHits.begin(), gHits.end(), [](const Hit& hit) { return hit.id == ID_REFRESH; }),
            "startup add and refresh buttons receive hit targets");

        startupDialogSelection = L"C:\\N-Lite-Test\\" + valueName + L".exe";
        ok &= Check(clickHit(ID_STARTUP_ADD), "startup add button opens the selected-app flow");
        auto testEntry = findTestEntry();
        ok &= Check(testEntry != gStartupEntries.end() && testEntry->enabled &&
            testEntry->canToggle && testEntry->canDelete &&
            testEntry->command == L"\"" + startupDialogSelection + L"\"",
            "startup UI adds a custom app with its quoted launch path");
        paint();
        ok &= Check(clickHit(ID_REFRESH), "startup refresh button responds to a rendered click");
        testEntry = findTestEntry();
        ok &= Check(testEntry != gStartupEntries.end(), "startup refresh keeps the custom app listed");

        if (testEntry != gStartupEntries.end()) {
            gStartupEntries = {*testEntry};
            paint();
            ok &= Check(clickHit(ID_STARTUP_TOGGLE), "startup toggle receives a real rendered hit target");
            testEntry = findTestEntry();
        }
        ok &= Check(testEntry != gStartupEntries.end() && !testEntry->enabled && testEntry->canToggle,
            "startup UI click disables current-user app and keeps its control");

        if (testEntry != gStartupEntries.end()) {
            gStartupEntries = {*testEntry};
            paint();
            ok &= Check(clickHit(ID_STARTUP_TOGGLE), "disabled startup switch can be clicked back on");
            testEntry = findTestEntry();
            ok &= Check(testEntry != gStartupEntries.end() && testEntry->enabled,
                "startup UI click enables current-user app");
        }
        if (testEntry != gStartupEntries.end()) {
            gStartupEntries = {*testEntry};
            paint();
            ok &= Check(clickHit(ID_STARTUP_DELETE), "startup delete receives a real rendered hit target");
        }
    }
    SetStartupFilePickerForTesting(nullptr);
    if (canvas && old && old != HGDI_ERROR) SelectObject(canvas, old);
    if (bitmap) DeleteObject(bitmap);
    if (canvas) DeleteDC(canvas);
    if (screen) ReleaseDC(nullptr, screen);
    if (gFont) { DeleteObject(gFont); gFont = nullptr; }
    if (gFontSmall) { DeleteObject(gFontSmall); gFontSmall = nullptr; }
    if (gFontMed) { DeleteObject(gFontMed); gFontMed = nullptr; }
    if (gFontTitle) { DeleteObject(gFontTitle); gFontTitle = nullptr; }
    DestroyWindow(controller);
    gWnd = nullptr;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, runPath, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &run) == ERROR_SUCCESS) {
        DWORD bytes = 0;
        const LONG remains = RegQueryValueExW(run, valueName.c_str(), nullptr, nullptr, nullptr, &bytes);
        RegCloseKey(run);
        ok &= Check(remains == ERROR_FILE_NOT_FOUND, "startup UI delete removes registry launch entry");
    } else ok &= Check(false, "startup UI delete removes registry launch entry");
    HKEY approval = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, approvalPath, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &approval) == ERROR_SUCCESS) {
        DWORD bytes = 0;
        const LONG remains = RegQueryValueExW(approval, valueName.c_str(), nullptr, nullptr, nullptr, &bytes);
        RegCloseKey(approval);
        ok &= Check(remains == ERROR_FILE_NOT_FOUND, "startup UI delete removes approval state");
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, runPath, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &run) == ERROR_SUCCESS) {
        RegDeleteValueW(run, valueName.c_str()); RegCloseKey(run);
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, approvalPath, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &approval) == ERROR_SUCCESS) {
        RegDeleteValueW(approval, valueName.c_str()); RegCloseKey(approval);
    }
    ok &= Check(elevationAttempts == 0, "startup app controls do not request administrator elevation");
    return ok ? 0 : 1;
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
    const uint64_t threshold = static_cast<uint64_t>(gThresholdMB) * 1024u * 1024u;
    ok &= Check(automatic.standbyValid, "SYSTEM helper returned a current standby measurement");
    if (automatic.standbyValid && automatic.standbyBytes < threshold)
        ok &= Check(automatic.autoArmed, "auto clean stays armed while standby is below threshold");
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
    if (argc > 1 && _wcsicmp(argv[1], L"--startup-ui-test") == 0) return StartupUiTest();
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
