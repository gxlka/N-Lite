#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <mmsystem.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <sddl.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <taskschd.h>
#include <thread>
#include <atomic>
#include <array>
#include <cwchar>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <functional>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <cwctype>
#include <iterator>
#include <utility>
#include <memory>
#include "cleaner_policy.h"
#include "process_grouping.h"
#include "ui_layout.h"
#include "ui_theme.h"
#include "timer_slider.h"
#include "startup_policy.h"
#include "process_visibility.h"
#include "startup_manager.h"

static constexpr WORD IDI_NLITE = 101;

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comdlg32.lib")

#ifndef NLITE_VERSION
#define NLITE_VERSION "0.2.14"
#endif
#ifndef NLITE_CLEANER_VERSION
#define NLITE_CLEANER_VERSION "4"
#endif
#define NLITE_WIDEN2(x) L##x
#define NLITE_WIDEN(x) NLITE_WIDEN2(x)
static const wchar_t* APP_VERSION = NLITE_WIDEN(NLITE_VERSION);
static const wchar_t* CLEANER_VERSION = NLITE_WIDEN(NLITE_CLEANER_VERSION);
static const wchar_t* APP_CLASS = L"NLiteWindow";
static const wchar_t* POPUP_CLASS = L"NLiteContextPopup";
static const UINT WM_UPDATE_READY = WM_APP + 12;
static const UINT WM_UPDATE_INSTALL_DONE = WM_APP + 13;
static const UINT WM_TRAY = WM_APP + 11;
static const UINT_PTR TIMER_REFRESH = 1, TIMER_UPDATE_CHECK = 2;
static const int ID_PROCESSES = 1, ID_MEMORY = 2, ID_STARTUP = 3, ID_SETTINGS = 4, ID_REFRESH = 10, ID_SEARCH = 11;
static const int ID_SORT_NAME = 20, ID_SORT_PID = 21, ID_SORT_CPU = 22, ID_SORT_MEMORY = 23, ID_SORT_PRIVATE = 24;
static const int ID_PURGE = 30, ID_AUTO = 31, ID_THRESHOLD_DOWN = 32, ID_THRESHOLD_UP = 33, ID_THRESHOLD_FIELD = 37;
static const int ID_INTERVAL_FIELD = 34, ID_ELEVATE = 36, ID_INTERVAL_OPTION = 38, ID_THEME = 46;
static const int ID_TIMER_TOGGLE = 40, ID_TIMER_MINUS = 41, ID_TIMER_PLUS = 42, ID_AUTOSTART = 43, ID_UPDATE_CHECK_NOW = 44, ID_OPEN_GITHUB = 45;
static const int ID_STARTUP_ADD = 47, ID_STARTUP_TOGGLE = 48, ID_PROCESS_FILTER = 49, ID_STARTUP_DELETE = 50;

static const int ID_EXIT = 9001, ID_SHOW = 9002, ID_UPDATE = 9003;
static COLORREF C_BG = RGB(17, 21, 29), C_PANEL = RGB(26, 32, 42), C_PANEL2 = RGB(21, 26, 35);
static COLORREF C_LINE = RGB(43, 52, 66), C_TEXT = RGB(233, 237, 245), C_MUTED = RGB(151, 162, 178);
static COLORREF C_ACCENT = RGB(130, 144, 255), C_ACCENT_SOFT = RGB(37, 43, 73);
static COLORREF C_GREEN = RGB(114, 200, 164), C_GREEN_SOFT = RGB(27, 53, 45);
static COLORREF C_AMBER = RGB(228, 182, 108), C_AMBER_SOFT = RGB(53, 43, 29);
static COLORREF C_RED = RGB(239, 135, 144), C_NAV_HOVER = RGB(31, 38, 52), C_SELECTED = RGB(38, 47, 73);
static COLORREF C_FIELD = RGB(16, 20, 29), C_ROW = RGB(23, 29, 42), C_TRACK = RGB(57, 64, 81);

using MemListInfo = SystemMemoryListInfo;
struct ProcRow {
    DWORD pid = 0, ppid = 0;
    std::wstring name, path;
    double cpu = 0.0;
    SIZE_T working = 0, privateBytes = 0, treeWorking = 0, treePrivateBytes = 0;
    bool hasChildren = false;
    bool ownerKnown = false, ownedByCurrentUser = false;
    bool groupHeader = false;
    std::wstring groupKey;
};
struct Metrics {
    double total = 0, available = 0, free = 0, standby = 0;
    double commit = 0, commitLimit = 0, pagefileUsed = 0, pagefileTotal = 0;
    bool standbyKnown = false;
};
struct Hit {
    RECT r;
    int id;
    DWORD data;
};
static HWND gWnd = nullptr;
static HICON gIcon = nullptr;
static HFONT gFont = nullptr, gFontSmall = nullptr, gFontMed = nullptr, gFontBold = nullptr, gFontTitle = nullptr;
static std::vector<Hit> gHits;
static std::vector<StartupItem> gStartupEntries;
static std::vector<ProcRow> gProcs, gVisible;
static std::unordered_map<DWORD, uint64_t> gCpuPrevious;
static std::unordered_map<std::wstring, bool> gExpanded;
static Metrics gMetrics;
static int gPage = 0, gScroll = 0, gNavHover = -1, gIntervalHover = -1;
static bool gIntervalOpen = false;
static bool gTimerDragging = false;
static ULONG gTimerDragOriginal = 0;
static RECT gTimerSliderHit{};
static int gStartupScroll = 0;
static DWORD gStartupLastRefresh = 0;
static int gSortColumn = 0;
static bool gSortDescending = false;
static DWORD gSelectedPid = 0;
static std::wstring gSearch, gStatus = L"Ready";
static bool gSearchFocus = false, gTrayAdded = false, gExiting = false, gAutoPurge = kAutoCleanDefaultEnabled, gAutoStart = false;
static bool gShowAllProcesses = kShowAllProcessesDefault;
static bool gThresholdFocus=false, gThresholdReplaceOnType=false, gTimerEnabled=false, gTimerActive=false, gAutoTaskReady=false, gHasProcessOverrides=false;
static bool gDarkTheme=true, gCleanerInstalled=false, gCleanerCurrentVersion=false, gCleanerTaskUsable=false;
static bool gCleanerSetupForAuto=false, gCleanerSetupForManual=false;
static bool gCleanerSetupBlocked=false;
static std::wstring gThresholdEdit;
static std::wstring gUserSid, gCleanerRoot;
static DWORD gHoveredPid=0;
static std::unordered_map<std::wstring,HICON> gProcessIcons;
static std::atomic<bool> gUpdateAvailable{false};
static std::atomic<bool> gUpdateCheckSucceeded{false};
static std::atomic<bool> gUpdateCheckNoRelease{false};
static std::atomic<bool> gUpdateCheckInProgress{false};
static std::wstring gLatestVersion, gInstallerUrl, gInstallerDigest, gUpdateInstallMessage;
static std::atomic<bool> gUpdateInstallerMissing{false};
static std::atomic<bool> gUpdateInstallInProgress{false};
static bool gTimerNeed = false;
static HANDLE gCleanerSetupProcess = nullptr;
static uint64_t gCleanerRequestId = 0;
static uint64_t gLastDisplayedManualRequestId = 0;
static CleanerStatus gCleanerStatus;
static bool gManualCleanerPending = false;
static uint64_t gManualStandbyBefore = 0;
static bool gManualStandbyBeforeKnown = false;
enum class CleanerNoticeKind : uint8_t { Info, Success, Failure };
static std::wstring gCleanerNotification;
static ULONGLONG gCleanerNotificationUntil = 0;
static CleanerNoticeKind gCleanerNotificationKind = CleanerNoticeKind::Info;
static unsigned gThresholdMB = 4096, gIntervalSec = 60;
static ULONG gTimerResolution=5000, gTimerApplied=5000, gTimerMinResolution=5000, gTimerMaxResolution=156250;
static DWORD gLastRefresh = 0;
static ULONGLONG gLastPurgeCheck = 0, gLastPurge = 0, gLastSeenPurgeTick = 0;
static bool gPurgeLatched = false;
static std::wstring gExePath;
static DWORD gPageSize = 4096;
static HANDLE gMutex = nullptr;

static void ShowCleanerNotification(const std::wstring& message, CleanerNoticeKind kind) {
    gCleanerNotification = message;
    gCleanerNotificationKind = kind;
    gCleanerNotificationUntil = GetTickCount64() + 6000;
    if (gWnd) InvalidateRect(gWnd, nullptr, FALSE);
}

static void ApplyThemeColors() {
    const UiPalette p = PaletteFor(gDarkTheme);
    C_BG = p.background; C_PANEL = p.surface; C_PANEL2 = p.softSurface; C_LINE = p.border;
    C_TEXT = p.text; C_MUTED = p.muted; C_ACCENT = p.accent; C_ACCENT_SOFT = p.accentSoft;
    C_GREEN = p.green; C_GREEN_SOFT = p.greenSoft; C_AMBER = p.amber; C_AMBER_SOFT = p.amberSoft;
    C_RED = p.red; C_NAV_HOVER = p.navHover; C_SELECTED = p.selected; C_FIELD = p.field;
    C_ROW = p.row; C_TRACK = p.track;
}
static void ApplyWindowChromeTheme(HWND hwnd) {
    BOOL dark = gDarkTheme ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    COLORREF caption = C_BG, titleText = C_TEXT, border = C_BG;
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36, &titleText, sizeof(titleText));
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
}

static bool ReadProcessDword(const wchar_t* kind, const std::wstring& path, DWORD& value);
static bool ReadProcessQword(const wchar_t* kind, const std::wstring& path, uint64_t& value);
static bool ProcessOverridesExist();
static void ApplyStoredProcessSettings(const ProcRow& p);

using NtQuerySysFn = LONG (NTAPI*)(ULONG, PVOID, ULONG, PULONG);
using NtQueryTimerFn = LONG (NTAPI*)(PULONG, PULONG, PULONG);
using NtSetTimerFn = LONG (NTAPI*)(ULONG, BOOLEAN, PULONG);
static NtQuerySysFn gNtQuerySys = nullptr;
static NtQueryTimerFn gNtQueryTimer = nullptr;
static NtSetTimerFn gNtSetTimer = nullptr;

static COLORREF RGBc(int r, int g, int b) { return RGB(r, g, b); }
static RECT R(int x, int y, int w, int h) { RECT a{ x, y, x + w, y + h }; return a; }
static int W(const RECT& r) { return r.right - r.left; }
static int H(const RECT& r) { return r.bottom - r.top; }
static void Fill(HDC dc, RECT r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b);
}
static void Round(HDC dc, RECT r, COLORREF c, COLORREF edge = C_LINE, int radius = 12) {
    HBRUSH b = CreateSolidBrush(c); HPEN p = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b); DeleteObject(p);
}
static void Line(HDC dc, int x1, int y1, int x2, int y2, COLORREF c, int width = 1) {
    HPEN p = CreatePen(PS_SOLID, width, c); HGDIOBJ old = SelectObject(dc, p);
    MoveToEx(dc, x1, y1, nullptr); LineTo(dc, x2, y2); SelectObject(dc, old); DeleteObject(p);
}
static void Txt(HDC dc, const std::wstring& s, int x, int y, int w, int h, COLORREF color = C_TEXT,
                HFONT font = nullptr, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    RECT r = R(x, y, w, h); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    HGDIOBJ old = SelectObject(dc, font ? font : gFont); DrawTextW(dc, s.c_str(), -1, &r, flags);
    SelectObject(dc, old);
}
static void AddHit(RECT r, int id, DWORD data = 0) { gHits.push_back({ r, id, data }); }
static bool Inside(RECT r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
static void Card(HDC dc, RECT r) { Round(dc, r, C_PANEL, C_LINE, 14); }
static std::wstring Commas(uint64_t n) {
    std::wstring s = std::to_wstring(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(i, L",");
    return s;
}
static std::wstring Bytes(double b) {
    if (b < 0) b = 0;
    const wchar_t* u[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    int i = 0; while (b >= 1024.0 && i < 4) { b /= 1024.0; ++i; }
    std::wostringstream o; o << std::fixed << std::setprecision(i ? 1 : 0) << b << L" " << u[i]; return o.str();
}
static std::wstring Percent(double v) {
    std::wostringstream o; o << std::fixed << std::setprecision(1) << v << L"%"; return o.str();
}
static uint64_t FtValue(FILETIME f) {
    ULARGE_INTEGER u{}; u.LowPart = f.dwLowDateTime; u.HighPart = f.dwHighDateTime; return u.QuadPart;
}
static void LoadNt() {
    HMODULE n = GetModuleHandleW(L"ntdll.dll");
    if (!n) return;
    gNtQuerySys = reinterpret_cast<NtQuerySysFn>(GetProcAddress(n, "NtQuerySystemInformation"));
    gNtQueryTimer = reinterpret_cast<NtQueryTimerFn>(GetProcAddress(n, "NtQueryTimerResolution"));
    gNtSetTimer = reinterpret_cast<NtSetTimerFn>(GetProcAddress(n, "NtSetTimerResolution"));
}
static bool ReadStandby(double& out, double& freeOut) {
    MemListInfo info{};
    const bool queried = gNtQuerySys && QuerySystemMemoryListInfo(
        [&](uint32_t informationClass, void* buffer, uint32_t bufferLength, uint32_t* returnLength) {
            ULONG ret = 0;
            LONG status = gNtQuerySys(informationClass, buffer, bufferLength, &ret);
            if (returnLength) *returnLength = ret;
            return static_cast<int32_t>(status);
        }, info);
    if (queried) {
        out = static_cast<double>(StandbyBytesFromPageCounts(info, gPageSize));
        freeOut = static_cast<double>(FreeBytesFromPageCount(info, gPageSize));
        return true;
    }
    const ULONGLONG now = GetTickCount64();
    if (gCleanerStatus.standbyValid && gCleanerStatus.standbyTick <= now &&
        now - gCleanerStatus.standbyTick <= 120000) {
        out = static_cast<double>(gCleanerStatus.standbyBytes);
        return true;
    }
    return false;
}
static bool IsNtOk(LONG s) { return s >= 0; }
static BOOL CALLBACK PageFileUsageCallback(PVOID context, PENUM_PAGE_FILE_INFORMATION info, LPCWSTR) {
    double* pages=static_cast<double*>(context);
    pages[0]+=static_cast<double>(info->TotalInUse);
    pages[1]+=static_cast<double>(info->TotalSize);
    return TRUE;
}
static void UpdateMetrics() {
    MEMORYSTATUSEX m{}; m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m)) {
        gMetrics.total = static_cast<double>(m.ullTotalPhys);
        gMetrics.available = static_cast<double>(m.ullAvailPhys);
        gMetrics.free = static_cast<double>(m.ullAvailPhys);
    }
    PERFORMANCE_INFORMATION p{}; p.cb = sizeof(p);
    if (GetPerformanceInfo(&p, sizeof(p))) {
        gPageSize = static_cast<DWORD>(p.PageSize ? p.PageSize : 4096);
        gMetrics.free = static_cast<double>(p.PhysicalAvailable) * gPageSize;
        gMetrics.commit = static_cast<double>(p.CommitTotal) * gPageSize;
        gMetrics.commitLimit = static_cast<double>(p.CommitLimit) * gPageSize;
    }
    double sb = 0, freePages = gMetrics.free;
    gMetrics.standbyKnown = ReadStandby(sb, freePages);
    gMetrics.standby = gMetrics.standbyKnown ? sb : 0;
    if (gMetrics.standbyKnown) gMetrics.free = freePages;
    double pagefilePages[2]={0,0};
    if(EnumPageFilesW(PageFileUsageCallback,pagefilePages)){
        gMetrics.pagefileUsed=pagefilePages[0]*gPageSize;
        gMetrics.pagefileTotal=pagefilePages[1]*gPageSize;
    }
}
static std::wstring ImagePath(HANDLE h) {
    wchar_t b[32768]{}; DWORD n = static_cast<DWORD>(std::size(b));
    if (QueryFullProcessImageNameW(h, 0, b, &n)) return std::wstring(b, n);
    return L"Path unavailable";
}
static bool ProcessOwnedByCurrentUser(HANDLE process, bool& ownerKnown) {
    ownerKnown=false;
    if(gUserSid.empty())return false;
    HANDLE token=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&token))return false;
    DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
    std::vector<BYTE> buffer(bytes);bool owned=false;
    if(bytes&&GetTokenInformation(token,TokenUser,buffer.data(),bytes,&bytes)){
        LPWSTR sidText=nullptr;
        const auto user=reinterpret_cast<TOKEN_USER*>(buffer.data());
        if(ConvertSidToStringSidW(user->User.Sid,&sidText)){
            ownerKnown=true;owned=_wcsicmp(sidText,gUserSid.c_str())==0;LocalFree(sidText);
        }
    }
    CloseHandle(token);return owned;
}
static void RefreshProcesses() {
    DWORD nowMs = GetTickCount();
    DWORD elapsed = gLastRefresh ? nowMs - gLastRefresh : 0;
    DWORD anchorPid = 0;
    std::wstring anchorGroup;
    const int previousScroll = gScroll;
    if (gScroll >= 0 && gScroll < static_cast<int>(gVisible.size())) {
        anchorPid = gVisible[gScroll].pid;
        anchorGroup = gVisible[gScroll].groupKey;
    }
    SYSTEM_INFO si{}; GetSystemInfo(&si); unsigned cpus = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
    std::vector<ProcRow> fresh;
    std::unordered_map<DWORD,double> previousCpu;
    for(const auto& old:gProcs)previousCpu[old.pid]=old.cpu;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W e{}; e.dwSize = sizeof(e);
        if (Process32FirstW(snap, &e)) do {
            ProcRow p; p.pid = e.th32ProcessID; p.ppid = e.th32ParentProcessID; p.name = e.szExeFile;
            DWORD rights = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ;
            HANDLE ph = OpenProcess(rights, FALSE, p.pid);
            if (ph) {
                p.path = ImagePath(ph);
                p.ownedByCurrentUser = ProcessOwnedByCurrentUser(ph,p.ownerKnown);
                PROCESS_MEMORY_COUNTERS_EX pm{}; pm.cb = sizeof(pm);
                if (GetProcessMemoryInfo(ph, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof(pm))) {
                    p.working = pm.WorkingSetSize; p.privateBytes = pm.PrivateUsage;
                }
                FILETIME c{}, x{}, k{}, u{};
                if (GetProcessTimes(ph, &c, &x, &k, &u)) {
                    uint64_t t = FtValue(k) + FtValue(u);
                    auto it = gCpuPrevious.find(p.pid);
                    if (it != gCpuPrevious.end() && elapsed >= 1000) {
                        uint64_t diff = t >= it->second ? t - it->second : 0;
                        p.cpu = 100.0 * static_cast<double>(diff) / (static_cast<double>(elapsed) * 10000.0 * cpus);
                        if (p.cpu > 100.0) p.cpu = 100.0;
                    } else {
                        auto old=previousCpu.find(p.pid);if(old!=previousCpu.end())p.cpu=old->second;
                    }
                    gCpuPrevious[p.pid] = t;
                }
                CloseHandle(ph);
            }
            if(!p.path.empty())ApplyStoredProcessSettings(p);
            fresh.push_back(std::move(p));
        } while (Process32NextW(snap, &e));
        CloseHandle(snap);
    }
    gLastRefresh = nowMs;
    gProcs.swap(fresh);

    std::unordered_set<DWORD> ids; for (auto& p : gProcs) ids.insert(p.pid);
    for(auto it=gCpuPrevious.begin();it!=gCpuPrevious.end();)if(!ids.count(it->first))it=gCpuPrevious.erase(it);else ++it;
    std::vector<ProcRow> displayProcs;
    displayProcs.reserve(gProcs.size());
    for(const auto& p:gProcs)if(ShouldShowProcess(gShowAllProcesses||gUserSid.empty(),p.ownerKnown,p.ownedByCurrentUser))displayProcs.push_back(p);
    auto compareRows = [&](const ProcRow& a,const ProcRow& b) {
        int cmp=0;
        if(gSortColumn==0)cmp=_wcsicmp(a.name.c_str(),b.name.c_str());
        else if(gSortColumn==1)cmp=a.pid<b.pid?-1:(a.pid>b.pid?1:0);
        else if(gSortColumn==2)cmp=a.cpu<b.cpu?-1:(a.cpu>b.cpu?1:0);
        else if(gSortColumn==3){SIZE_T av=a.groupHeader?a.treeWorking:a.working,bv=b.groupHeader?b.treeWorking:b.working;cmp=av<bv?-1:(av>bv?1:0);}
        else {SIZE_T av=a.groupHeader?a.treePrivateBytes:a.privateBytes,bv=b.groupHeader?b.treePrivateBytes:b.privateBytes;cmp=av<bv?-1:(av>bv?1:0);}
        if(cmp==0)cmp=_wcsicmp(a.name.c_str(),b.name.c_str());
        return gSortDescending?cmp>0:cmp<0;
    };
    std::vector<ProcessSample> samples;
    samples.reserve(displayProcs.size());
    for (const auto& p : displayProcs) samples.push_back({p.pid, p.path,
        static_cast<uint64_t>(p.working), static_cast<uint64_t>(p.privateBytes), p.cpu});
    std::vector<ProcessGroup> groups = GroupProcessSamples(samples);
    std::sort(groups.begin(), groups.end(), [&](const ProcessGroup& a, const ProcessGroup& b) {
        ProcRow left = displayProcs[a.representativeIndex], right = displayProcs[b.representativeIndex];
        left.name = a.path.empty() || a.path == L"Path unavailable" ? left.name : PathFindFileNameW(a.path.c_str());
        right.name = b.path.empty() || b.path == L"Path unavailable" ? right.name : PathFindFileNameW(b.path.c_str());
        left.groupHeader = a.members.size() > 1; right.groupHeader = b.members.size() > 1;
        left.treeWorking = static_cast<SIZE_T>(a.workingBytes); right.treeWorking = static_cast<SIZE_T>(b.workingBytes);
        left.treePrivateBytes = static_cast<SIZE_T>(a.privateBytes); right.treePrivateBytes = static_cast<SIZE_T>(b.privateBytes);
        if (a.members.size() > 1) left.cpu = a.cpuPercent;
        if (b.members.size() > 1) right.cpu = b.cpuPercent;
        return compareRows(left, right);
    });
    std::unordered_set<std::wstring> liveGroups;
    std::vector<ProcRow> flat;
    for (const auto& group : groups) {
        liveGroups.insert(group.key);
        const bool grouped = group.members.size() > 1;
        ProcRow summary = displayProcs[group.representativeIndex];
        if (!group.path.empty() && group.path != L"Path unavailable") summary.name = PathFindFileNameW(group.path.c_str());
        summary.groupKey = group.key;
        summary.groupHeader = grouped;
        summary.hasChildren = grouped;
        summary.ppid = 0;
        summary.treeWorking = static_cast<SIZE_T>(group.workingBytes);
        summary.treePrivateBytes = static_cast<SIZE_T>(group.privateBytes);
        if (grouped) summary.cpu = group.cpuPercent;
        flat.push_back(summary);
        if (!grouped || !gExpanded[group.key]) continue;
        std::vector<size_t> members = group.members;
        std::sort(members.begin(), members.end(), [&](size_t a, size_t b) { return compareRows(displayProcs[a], displayProcs[b]); });
        for (size_t member : members) {
            ProcRow child = displayProcs[member];
            child.groupKey = group.key;
            child.groupHeader = false;
            child.hasChildren = false;
            child.ppid = 1;
            child.treeWorking = child.working;
            child.treePrivateBytes = child.privateBytes;
            flat.push_back(std::move(child));
        }
    }
    for (auto it = gExpanded.begin(); it != gExpanded.end();) {
        if (!liveGroups.count(it->first)) it = gExpanded.erase(it); else ++it;
    }
    gVisible.clear();
    for (auto& p : flat) if (gSearch.empty() || StrStrIW(p.name.c_str(), gSearch.c_str()) || StrStrIW(p.path.c_str(), gSearch.c_str()))
        gVisible.push_back(p);
    auto anchor = std::find_if(gVisible.begin(), gVisible.end(), [&](const ProcRow& p) {
        return anchorPid && p.pid == anchorPid && p.groupKey == anchorGroup;
    });
    if (anchor == gVisible.end() && !anchorGroup.empty()) anchor = std::find_if(gVisible.begin(), gVisible.end(), [&](const ProcRow& p) {
        return p.groupHeader && p.groupKey == anchorGroup;
    });
    gScroll = anchor != gVisible.end() ? static_cast<int>(anchor - gVisible.begin()) : previousScroll;
    gScroll = (std::max)(0, (std::min)(gScroll, static_cast<int>(gVisible.size())));
    if (!gSelectedPid || std::none_of(gVisible.begin(), gVisible.end(), [](const ProcRow& p){ return p.pid == gSelectedPid; })) {
        gSelectedPid = gVisible.empty() ? 0 : gVisible.front().pid;
    }
}
static ProcRow* Selected() {
    auto it = std::find_if(gProcs.begin(), gProcs.end(), [](const ProcRow& p) { return p.pid == gSelectedPid; });
    return it == gProcs.end() ? nullptr : &*it;
}
static void RegReadDword(const wchar_t* name, DWORD& v) {
    DWORD cb = sizeof(v); RegGetValueW(HKEY_CURRENT_USER, L"Software\\N-Lite", name, RRF_RT_REG_DWORD, nullptr, &v, &cb);
}
static void RegWriteDword(const wchar_t* name, DWORD v) {
    HKEY k; if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\N-Lite", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v)); RegCloseKey(k);
    }
}
static uint64_t RegReadQword(const wchar_t* name,uint64_t fallback=0) {
    HKEY k; if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\N-Lite",0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return fallback;
    uint64_t value=fallback; DWORD type=0,cb=sizeof(value);
    if(RegQueryValueExW(k,name,nullptr,&type,reinterpret_cast<BYTE*>(&value),&cb)!=ERROR_SUCCESS||type!=REG_QWORD)value=fallback;
    RegCloseKey(k); return value;
}
static void RegWriteQword(const wchar_t* name,uint64_t value) {
    HKEY k; if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\N-Lite",0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)==ERROR_SUCCESS){
        RegSetValueExW(k,name,0,REG_QWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value));RegCloseKey(k);
    }
}
static void RegWriteString(const wchar_t* name, const std::wstring& value) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\N-Lite", 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
}
static std::wstring ProgramDataNlite() {
    DWORD size = GetEnvironmentVariableW(L"ProgramData", nullptr, 0);
    if (!size || size > 32767) return L"";
    std::vector<wchar_t> value(size);
    DWORD copied = GetEnvironmentVariableW(L"ProgramData", value.data(), size);
    if (!copied || copied >= size) return L"";
    return std::wstring(value.data()) + L"\\N-Lite";
}
static std::wstring CleanerSettingsFile() {
    return gCleanerRoot.empty() || gUserSid.empty() ? L"" : gCleanerRoot + L"\\settings-" + gUserSid + L".txt";
}
static std::wstring CleanerStatusFile() {
    return gCleanerRoot.empty() || gUserSid.empty() ? L"" : gCleanerRoot + L"\\status-" + gUserSid + L".txt";
}
static bool ReadCleanerText(const std::wstring& path, std::wstring& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    bool ok = GetFileInformationByHandle(file, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart <= 32768 &&
        size.QuadPart % sizeof(wchar_t) == 0;
    std::vector<wchar_t> buffer(ok ? static_cast<size_t>(size.QuadPart / sizeof(wchar_t)) + 1 : 1, L'\0');
    DWORD read = 0;
    if (ok && size.QuadPart) ok = ReadFile(file, buffer.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr) &&
        read == static_cast<DWORD>(size.QuadPart);
    if (ok) text.assign(buffer.data(), read / sizeof(wchar_t));
    CloseHandle(file);
    return ok;
}
static bool WriteCleanerText(const std::wstring& path, const std::wstring& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    bool ok = GetFileInformationByHandle(file, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
    LARGE_INTEGER zero{};
    if (ok) ok = SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) && SetEndOfFile(file);
    const DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    DWORD written = 0;
    if (ok) ok = WriteFile(file, text.data(), bytes, &written, nullptr) && written == bytes && FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}
static std::wstring CurrentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return L"";
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> buffer(bytes);
    std::wstring result;
    if (bytes && GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes)) {
        LPWSTR sidText = nullptr;
        auto user = reinterpret_cast<TOKEN_USER*>(buffer.data());
        if (ConvertSidToStringSidW(user->User.Sid, &sidText)) {
            result = sidText;
            LocalFree(sidText);
        }
    }
    CloseHandle(token);
    return IsValidCleanerSid(result) ? result : L"";
}
static bool RunRegisteredCleanerTask();
static bool WriteCleanerSettings() {
    if (!gCleanerTaskUsable) return true;
    CleanerSettings settings;
    settings.enabled = gAutoPurge;
    settings.thresholdMb = gThresholdMB;
    settings.intervalSeconds = gIntervalSec;
    settings.manualRequestId = gCleanerRequestId;
    if (!WriteCleanerText(CleanerSettingsFile(), SerializeCleanerSettings(settings))) {
        gStatus = L"Could not save settings for the standby cleaner.";
        return false;
    }
    return true;
}
static bool ReadCleanerStatus(CleanerStatus& status) {
    std::wstring text;
    return ReadCleanerText(CleanerStatusFile(), text) && ParseCleanerStatus(text, status);
}
static bool ReadCleanerHelperVersion(std::wstring& version) {
    if (gCleanerRoot.empty() || !ReadCleanerText(gCleanerRoot + L"\\helper-version.txt", version)) return false;
    while (!version.empty() && (version.back() == L'\r' || version.back() == L'\n' ||
            version.back() == L' ' || version.back() == L'\t')) version.pop_back();
    size_t first = 0;
    while (first < version.size() && (version[first] == L' ' || version[first] == L'\t' ||
            version[first] == L'\r' || version[first] == L'\n')) ++first;
    if (first) version.erase(0, first);
    if (version.empty()) return false;
    for (wchar_t ch : version) if (ch < L'0' || ch > L'9') return false;
    return true;
}
template <typename T>
static void ReleaseCleanerCom(T*& value) {
    if(value){value->Release();value=nullptr;}
}
static bool IsCleanerSystemPrincipal(const std::wstring& principal) {
    if(_wcsicmp(principal.c_str(),L"SYSTEM")==0||
       _wcsicmp(principal.c_str(),L"NT AUTHORITY\\SYSTEM")==0||
       _wcsicmp(principal.c_str(),L"S-1-5-18")==0)return true;
    PSID sid=nullptr;
    if(ConvertStringSidToSidW(principal.c_str(),&sid)){
        const bool isSystem=IsWellKnownSid(sid,WinLocalSystemSid)!=FALSE;
        LocalFree(sid);return isSystem;
    }
    DWORD sidBytes=0,domainChars=0;SID_NAME_USE use{};
    LookupAccountNameW(nullptr,principal.c_str(),nullptr,&sidBytes,nullptr,&domainChars,&use);
    if(!sidBytes)return false;
    std::vector<BYTE> sidBuffer(sidBytes);std::vector<wchar_t> domain((std::max)(domainChars,static_cast<DWORD>(1)));
    return LookupAccountNameW(nullptr,principal.c_str(),sidBuffer.data(),&sidBytes,
        domain.data(),&domainChars,&use)&&IsWellKnownSid(sidBuffer.data(),WinLocalSystemSid);
}
static bool CleanerTaskRegistered() {
    if(gUserSid.empty()||gCleanerRoot.empty())return false;
    const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const bool uninitialize=SUCCEEDED(init);
    if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return false;

    bool usable=false;
    ITaskService* service=nullptr;ITaskFolder* folder=nullptr;IRegisteredTask* task=nullptr;
    ITaskDefinition* definition=nullptr;IPrincipal* principal=nullptr;
    IActionCollection* actions=nullptr;IAction* action=nullptr;IExecAction* exec=nullptr;
    BSTR folderPath=SysAllocString(L"\\"),taskName=SysAllocString((L"N-Lite Cleaner "+gUserSid).c_str());
    VARIANT empty;VariantInit(&empty);
    HRESULT hr=CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,
        IID_ITaskService,reinterpret_cast<void**>(&service));
    if(SUCCEEDED(hr))hr=service->Connect(empty,empty,empty,empty);
    if(SUCCEEDED(hr)&&folderPath)hr=service->GetFolder(folderPath,&folder);
    else if(SUCCEEDED(hr))hr=E_OUTOFMEMORY;
    if(SUCCEEDED(hr)&&taskName)hr=folder->GetTask(taskName,&task);
    else if(SUCCEEDED(hr))hr=E_OUTOFMEMORY;

    VARIANT_BOOL enabled=VARIANT_FALSE;BSTR security=nullptr;
    if(SUCCEEDED(hr))hr=task->get_Enabled(&enabled);
    if(SUCCEEDED(hr)&&enabled!=VARIANT_TRUE)hr=E_ACCESSDENIED;
    if(SUCCEEDED(hr))hr=task->get_Definition(&definition);
    if(SUCCEEDED(hr))hr=definition->get_Principal(&principal);
    BSTR principalName=nullptr;TASK_LOGON_TYPE logonType=TASK_LOGON_NONE;
    if(SUCCEEDED(hr))hr=principal->get_UserId(&principalName);
    if(SUCCEEDED(hr))hr=principal->get_LogonType(&logonType);
    // LocalSystem is already privileged; Task Scheduler ignores RunLevel for this account.
    if(SUCCEEDED(hr)&&(!principalName||!IsCleanerSystemPrincipal(principalName)||
        logonType!=TASK_LOGON_SERVICE_ACCOUNT))hr=E_ACCESSDENIED;
    if(SUCCEEDED(hr))hr=definition->get_Actions(&actions);
    LONG actionCount=0;
    if(SUCCEEDED(hr))hr=actions->get_Count(&actionCount);
    if(SUCCEEDED(hr)&&actionCount!=1)hr=E_ACCESSDENIED;
    if(SUCCEEDED(hr))hr=actions->get_Item(1,&action);
    if(SUCCEEDED(hr))hr=action->QueryInterface(IID_IExecAction,reinterpret_cast<void**>(&exec));
    BSTR actionPath=nullptr,arguments=nullptr;
    if(SUCCEEDED(hr))hr=exec->get_Path(&actionPath);
    if(SUCCEEDED(hr))hr=exec->get_Arguments(&arguments);
    const std::wstring expectedPath=gCleanerRoot+L"\\N-Lite-Cleaner.exe";
    const std::wstring expectedArguments=L"--run "+gUserSid;
    if(SUCCEEDED(hr)&&(!actionPath||!CleanerExecutablePathMatches(actionPath,expectedPath)||
        !arguments||expectedArguments!=arguments))hr=E_ACCESSDENIED;
    if(SUCCEEDED(hr))hr=task->GetSecurityDescriptor(
        OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&security);
    if(SUCCEEDED(hr)&&(!security||!HasExpectedCleanerTaskSecurityDescriptor(security)))hr=E_ACCESSDENIED;
    usable=SUCCEEDED(hr);
#ifdef NLITE_CLEANER_TEST_DIAGNOSTICS
    std::wcerr << L"Task query hr=" << static_cast<unsigned long>(hr)
        << L" principal=" << (principalName ? principalName : L"<none>") << L" logon=" << logonType
        << L" path=" << (actionPath ? actionPath : L"<none>")
        << L" args=" << (arguments ? arguments : L"<none>")
        << L" acl=" << (security ? security : L"<none>") << std::endl;
#endif

    if(actionPath)SysFreeString(actionPath);if(arguments)SysFreeString(arguments);
    if(principalName)SysFreeString(principalName);if(security)SysFreeString(security);
    if(folderPath)SysFreeString(folderPath);if(taskName)SysFreeString(taskName);
    ReleaseCleanerCom(exec);ReleaseCleanerCom(action);ReleaseCleanerCom(actions);
    ReleaseCleanerCom(principal);ReleaseCleanerCom(definition);ReleaseCleanerCom(task);
    ReleaseCleanerCom(folder);ReleaseCleanerCom(service);
    if(uninitialize)CoUninitialize();
    return usable;
}
static bool CleanerHelperPresent() {
    if(gCleanerRoot.empty())return false;
    const DWORD attrs=GetFileAttributesW((gCleanerRoot+L"\\N-Lite-Cleaner.exe").c_str());
    return attrs!=INVALID_FILE_ATTRIBUTES&&!(attrs&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY));
}
static bool CleanerSettingsReady() {
    if(gCleanerRoot.empty()||gUserSid.empty())return false;
    std::wstring settingsText;CleanerSettings settings;
    return ReadCleanerText(CleanerSettingsFile(),settingsText)&&ParseCleanerSettings(settingsText,settings);
}
static bool CleanerRegistrationValid() {
    if (gCleanerRoot.empty() || gUserSid.empty()) return false;
    const auto isRegularFile = [](const std::wstring& path) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
            !(attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
    };
    std::wstring helperVersion;
    return HasProtectedCleanerRegistration(
        isRegularFile(gCleanerRoot + L"\\N-Lite-Cleaner.exe"),
        ReadCleanerHelperVersion(helperVersion),
        isRegularFile(CleanerSettingsFile()));
}
static bool CleanerHelperCurrent() {
    std::wstring version;
    return ReadCleanerHelperVersion(version) && version == CLEANER_VERSION;
}
static void RefreshCleanerSetupState() {
    const bool registrationValid = CleanerRegistrationValid();
    const CleanerSetupAction action = DecideCleanerSetup(
        registrationValid, registrationValid && CleanerHelperCurrent());
    gCleanerInstalled = action != CleanerSetupAction::Install;
    gCleanerCurrentVersion = action == CleanerSetupAction::Ready;
    gCleanerTaskUsable = CanUseRegisteredCleanerTask(CleanerTaskRegistered(),
        CleanerHelperPresent(),CleanerSettingsReady());
    gAutoTaskReady = gCleanerTaskUsable;
}
static DWORD CurrentCleanerVersionNumber() {
    wchar_t* end = nullptr;
    const unsigned long version = std::wcstoul(CLEANER_VERSION, &end, 10);
    if (end == CLEANER_VERSION || !end || *end != 0 || version > MAXDWORD) return 0;
    return static_cast<DWORD>(version);
}
static void BlockCleanerSetup() {
    gCleanerSetupBlocked = true;
    const DWORD version = CurrentCleanerVersionNumber();
    if (version) RegWriteDword(L"CleanerSetupBlockedVersion", version);
}
static void SaveSettings() {
    RegWriteDword(L"AutoPurge",gAutoPurge?1:0); RegWriteDword(L"ThresholdMB",gThresholdMB); RegWriteDword(L"IntervalSec",gIntervalSec);
    RegWriteDword(L"TimerEnabled",gTimerEnabled?1:0); RegWriteDword(L"TimerResolution100ns",gTimerResolution);
    RegWriteDword(L"ThemeDark",gDarkTheme?1:0);
    RegWriteDword(L"ShowAllProcesses",gShowAllProcesses?1:0);
    if (!gUserSid.empty()) RegWriteString(L"CleanerSid", gUserSid);
    WriteCleanerSettings();
}
static void DisableAutoCleanAfterSetupFailure(bool showNotice) {
    if (!gAutoPurge) return;
    gAutoPurge = false;
    SaveSettings();
    if (showNotice) ShowCleanerNotification(
        L"Auto clean is off. Press Clean now to retry cleaner setup.", CleanerNoticeKind::Failure);
}
static void LoadSettings() {
    DWORD v=0; RegReadDword(L"AutoPurge",v); gAutoPurge=v!=0;
    v=4096; RegReadDword(L"ThresholdMB",v); gThresholdMB=static_cast<unsigned>((std::max)(64u,(std::min)(131072u,static_cast<unsigned>(v))));
    v=60; RegReadDword(L"IntervalSec",v); gIntervalSec=static_cast<unsigned>((std::max)(60u,(std::min)(7200u,static_cast<unsigned>(v))));
    v=0; RegReadDword(L"TimerEnabled",v); gTimerEnabled=v!=0;
    v=5000; RegReadDword(L"TimerResolution100ns",v); gTimerResolution=v;
    v=0; RegReadDword(L"AutoTaskReady",v); gAutoTaskReady=v!=0;
    v=1; RegReadDword(L"ThemeDark",v); gDarkTheme=v!=0; ApplyThemeColors();
    v=0; RegReadDword(L"ShowAllProcesses",v); gShowAllProcesses=v!=0;
    RefreshCleanerSetupState();
    DWORD failedCleanerVersion = 0;
    RegReadDword(L"CleanerSetupBlockedVersion", failedCleanerVersion);
    gCleanerSetupBlocked = ShouldSuppressCleanerUpdateRetry(gCleanerInstalled,
        gCleanerCurrentVersion, CurrentCleanerVersionNumber(), failedCleanerVersion);
    if (gCleanerTaskUsable) {
        std::wstring text; CleanerSettings settings;
        if (ReadCleanerText(CleanerSettingsFile(), text) && ParseCleanerSettings(text, settings)) {
            gCleanerRequestId = settings.manualRequestId;
        }
        CleanerStatus status;
        if (ReadCleanerStatus(status)) {
            gCleanerRequestId=(std::max)(gCleanerRequestId,status.completedManualRequestId);
            gCleanerStatus = status;
            gLastSeenPurgeTick = status.lastAutoTick;
            gLastDisplayedManualRequestId = status.completedManualRequestId;
            gManualCleanerPending = gCleanerRequestId > status.completedManualRequestId;
        }
        WriteCleanerSettings();
    }
    RegWriteString(L"CleanerSid", gUserSid);
}
static void LoadTimerRange() {
    ULONG maxRes=0,minRes=0,cur=0;
    if(gNtQueryTimer&&IsNtOk(gNtQueryTimer(&maxRes,&minRes,&cur))){
        gTimerMinResolution=(std::min)(maxRes,minRes); gTimerMaxResolution=(std::max)(maxRes,minRes);
        if(gTimerResolution<gTimerMinResolution||gTimerResolution>gTimerMaxResolution)gTimerResolution=gTimerMinResolution;
    }
}
static std::wstring TimerText(ULONG units) {
    std::wostringstream q; q<<std::fixed<<std::setprecision(1)<<(units/10000.0)<<L" ms"; return q.str();
}
static bool SetAutoStart(bool on) {
    HKEY k; const wchar_t* sub = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub, 0, nullptr, 0, KEY_SET_VALUE|KEY_WOW64_64KEY, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    bool ok = false;
    if (on) {
        std::wstring cmd = L"\"" + gExePath + L"\" --startup";
        ok = RegSetValueExW(k, L"N-Lite", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else { LONG dr = RegDeleteValueW(k, L"N-Lite"); ok = dr == ERROR_SUCCESS || dr == ERROR_FILE_NOT_FOUND; }
    RegCloseKey(k);
    if(ok&&on){
        StartupItem approval;approval.kind=StartupKind::UserRun;approval.enabled=false;approval.canToggle=true;
        approval.approvalManaged=true;approval.registryView=KEY_WOW64_64KEY;
        approval.approvalName=L"N-Lite";
        approval.approvalSubkey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
        ok=SetStartupItemEnabled(approval,true);
    }
    return ok;
}
static bool ReadAutoStart() {
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE|KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, cb = 0; LONG r = RegQueryValueExW(k, L"N-Lite", nullptr, &type, nullptr, &cb);
    const bool sourcePresent=r==ERROR_SUCCESS&&type==REG_SZ;RegCloseKey(k);
    StartupApprovalState state=StartupApprovalState::Unknown;
    if(sourcePresent&&RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
        0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&k)==ERROR_SUCCESS){
        DWORD approvalType=0,size=0;
        if(RegQueryValueExW(k,L"N-Lite",nullptr,&approvalType,nullptr,&size)==ERROR_SUCCESS&&approvalType==REG_BINARY){
            std::vector<BYTE> bytes(size);
            if(size&&RegQueryValueExW(k,L"N-Lite",nullptr,&approvalType,bytes.data(),&size)==ERROR_SUCCESS){
                bytes.resize(size);std::vector<uint8_t> data(bytes.begin(),bytes.end());
                state=ParseStartupApprovalState(data);
            }
        }
        RegCloseKey(k);
    }
    return StartupSourceEnabled(sourcePresent,state);
}
static std::wstring ProcessSettingName(const wchar_t* kind,const std::wstring& path) {
    return std::wstring(kind)+L":"+path;
}
static bool ReadProcessDword(const wchar_t* kind,const std::wstring& path,DWORD& value) {
    if(path.empty()||path==L"Path unavailable")return false;
    HKEY k;if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\N-Lite\\ProcessSettings",0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return false;
    DWORD type=0,cb=sizeof(value);LONG r=RegQueryValueExW(k,ProcessSettingName(kind,path).c_str(),nullptr,&type,reinterpret_cast<BYTE*>(&value),&cb);
    RegCloseKey(k);return r==ERROR_SUCCESS&&type==REG_DWORD&&cb==sizeof(value);
}
static bool ReadProcessQword(const wchar_t* kind,const std::wstring& path,uint64_t& value) {
    if(path.empty()||path==L"Path unavailable")return false;
    HKEY k;if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\N-Lite\\ProcessSettings",0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return false;
    DWORD type=0,cb=sizeof(value);LONG r=RegQueryValueExW(k,ProcessSettingName(kind,path).c_str(),nullptr,&type,reinterpret_cast<BYTE*>(&value),&cb);
    RegCloseKey(k);return r==ERROR_SUCCESS&&type==REG_QWORD&&cb==sizeof(value);
}
static bool ProcessOverridesExist() {
    HKEY k;if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\N-Lite\\ProcessSettings",0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return false;
    DWORD count=0;RegQueryInfoKeyW(k,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&count,nullptr,nullptr,nullptr,nullptr);RegCloseKey(k);
    return count>0;
}
static void ApplyStoredProcessSettings(const ProcRow& p) {
    DWORD priority=0;uint64_t affinity=0;
    const bool hasPriority=ReadProcessDword(L"Priority",p.path,priority);
    const bool hasAffinity=ReadProcessQword(L"Affinity",p.path,affinity);
    if(!hasPriority&&!hasAffinity)return;
    HANDLE h=OpenProcess(PROCESS_SET_INFORMATION|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p.pid);if(!h)return;
    if(hasPriority&&(priority==IDLE_PRIORITY_CLASS||priority==BELOW_NORMAL_PRIORITY_CLASS||priority==NORMAL_PRIORITY_CLASS||priority==ABOVE_NORMAL_PRIORITY_CLASS||priority==HIGH_PRIORITY_CLASS))
        SetPriorityClass(h,priority);
    if(hasAffinity){
        DWORD_PTR current=0,system=0;
        if(GetProcessAffinityMask(h,&current,&system)){
            DWORD_PTR desired=static_cast<DWORD_PTR>(affinity)&system;
            if(desired)SetProcessAffinityMask(h,desired);
        }
    }
    CloseHandle(h);
}
static void SetTimerRequest(bool on) {
    if(on && !gTimerActive){
        if(!gNtSetTimer){gTimerNeed=true;gStatus=L"Windows timer-resolution API is unavailable.";return;}
        ULONG current=0; LONG st=gNtSetTimer(gTimerResolution,TRUE,&current);
        if(IsNtOk(st)){gTimerActive=true;gTimerApplied=gTimerResolution;gTimerNeed=false;}
        else {gTimerNeed=true;gStatus=L"Windows rejected the timer-resolution request.";}
    } else if(!on && gTimerActive){
        ULONG current=0; if(gNtSetTimer)gNtSetTimer(gTimerApplied,FALSE,&current); gTimerActive=false;gTimerNeed=false;
    }
}
static void UpdateTimerSlider(int x) {
    if(W(gTimerSliderHit)<=0)return;
    gTimerResolution=TimerResolutionFromX(x,gTimerSliderHit.left,W(gTimerSliderHit),
        gTimerMinResolution,gTimerMaxResolution,1000);
}
static void CommitTimerSlider() {
    if(gTimerEnabled){SetTimerRequest(false);SetTimerRequest(true);}
    SaveSettings();
    if(gTimerEnabled&&!gTimerActive)gStatus=L"Windows rejected the selected timer resolution.";
    else gStatus=L"Timer resolution saved.";
}
static std::wstring QuoteWindowsArg(const std::wstring& arg) {
    std::wstring out=L"\"";size_t slashes=0;
    for(wchar_t c:arg){
        if(c==L'\\'){slashes++;continue;}
        if(c==L'\"'){out.append(slashes*2+1,L'\\');out.push_back(L'\"');slashes=0;continue;}
        out.append(slashes,L'\\');slashes=0;out.push_back(c);
    }
    out.append(slashes*2,L'\\');out.push_back(L'\"');return out;
}
static bool RunRegisteredCleanerTask() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;
    ITaskService* service = nullptr; ITaskFolder* folder = nullptr;
    IRegisteredTask* task = nullptr; IRunningTask* running = nullptr;
    VARIANT empty; VariantInit(&empty);
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITaskService, reinterpret_cast<void**>(&service));
    if (SUCCEEDED(hr)) hr = service->Connect(empty, empty, empty, empty);
    BSTR root = SysAllocString(L"\\");
    BSTR name = SysAllocString((L"N-Lite Cleaner " + gUserSid).c_str());
    if (SUCCEEDED(hr) && root) hr = service->GetFolder(root, &folder);
    else if (SUCCEEDED(hr)) hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr) && name) hr = folder->GetTask(name, &task);
    else if (SUCCEEDED(hr)) hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) hr = task->Run(empty, &running);
#ifdef NLITE_CLEANER_TEST_DIAGNOSTICS
    std::wcerr << L"Task dispatch hr=" << static_cast<unsigned long>(hr) << std::endl;
#endif
    if (root) SysFreeString(root); if (name) SysFreeString(name);
    ReleaseCleanerCom(running); ReleaseCleanerCom(task); ReleaseCleanerCom(folder); ReleaseCleanerCom(service);
    if (uninitialize) CoUninitialize();
    return SUCCEEDED(hr);
}

static HICON MakeIcon() {
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, 32, 32), mask = CreateBitmap(32, 32, 1, 1, nullptr);
    HGDIOBJ old = SelectObject(dc, color); RECT r = R(0,0,32,32); Fill(dc, r, C_BG);
    HPEN p = CreatePen(PS_SOLID, 4, C_ACCENT); HGDIOBJ op = SelectObject(dc, p);
    MoveToEx(dc, 8, 24, nullptr); LineTo(dc, 8, 8); LineTo(dc, 23, 24); LineTo(dc, 23, 8);
    SelectObject(dc, op); DeleteObject(p); SelectObject(dc, old);
    ICONINFO ii{}; ii.fIcon = TRUE; ii.hbmColor = color; ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color); DeleteObject(mask); DeleteDC(dc); ReleaseDC(nullptr, screen); return icon;
}
static void AddTray() {
    if (gTrayAdded) return;
    if (!gIcon) gIcon = MakeIcon();
    NOTIFYICONDATAW n{}; n.cbSize = sizeof(n); n.hWnd = gWnd; n.uID = 1; n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    n.uCallbackMessage = WM_TRAY; n.hIcon = gIcon; wcscpy_s(n.szTip, L"N-Lite is running");
    gTrayAdded = Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
}
static void RemoveTray() {
    if (!gTrayAdded) return;
    NOTIFYICONDATAW n{}; n.cbSize = sizeof(n); n.hWnd = gWnd; n.uID = 1; Shell_NotifyIconW(NIM_DELETE, &n); gTrayAdded = false;
}
static void ShowWindowFromTray() {
    ShowWindow(gWnd, SW_SHOW); ShowWindow(gWnd, SW_RESTORE); SetForegroundWindow(gWnd);
}
static void HideToTray() { AddTray(); ShowWindow(gWnd, SW_HIDE); }
static std::wstring PackagedCleanerPath() {
    size_t slash = gExePath.find_last_of(L"\\/");
    return (slash == std::wstring::npos ? L"" : gExePath.substr(0, slash + 1)) + L"N-Lite-Cleaner.exe";
}
static bool RequestCleanerTaskRun(bool manual);
static bool StartCleanerSetup(bool forAuto, bool forManual) {
    if (gCleanerSetupProcess && WaitForSingleObject(gCleanerSetupProcess, 0) == WAIT_TIMEOUT) {
        gCleanerSetupForAuto = gCleanerSetupForAuto || forAuto;
        gCleanerSetupForManual = gCleanerSetupForManual || forManual;
        gStatus = L"Ready";
        return true;
    }
    if (ShouldBlockCleanerSetupRetry(gCleanerSetupBlocked, forManual)) return false;
    gCleanerSetupForAuto = gCleanerSetupForAuto || forAuto;
    gCleanerSetupForManual = gCleanerSetupForManual || forManual;
    if (gCleanerSetupProcess) { CloseHandle(gCleanerSetupProcess); gCleanerSetupProcess = nullptr; }
    if (gUserSid.empty()) {
        BlockCleanerSetup();
        gCleanerSetupForAuto=false;gCleanerSetupForManual=false;
        gStatus = L"Ready";
        if (forAuto && !gCleanerTaskUsable) DisableAutoCleanAfterSetupFailure(!forManual);
        if (forManual) ShowCleanerNotification(L"Clean failed: Windows account unavailable", CleanerNoticeKind::Failure);
        return false;
    }
    const std::wstring helper = PackagedCleanerPath();
    DWORD attributes = GetFileAttributesW(helper.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        BlockCleanerSetup();
        gCleanerSetupForAuto=false;gCleanerSetupForManual=false;
        gStatus = L"Ready";
        if (gCleanerTaskUsable && (forManual || gAutoPurge)) {
            const bool started = RequestCleanerTaskRun(forManual);
            if (started) gStatus = forManual ?
                L"Using the existing cleaner; the update file is missing." :
                L"Auto Clean is using the existing cleaner; the update file is missing.";
            return started;
        }
        if (forAuto && !gCleanerTaskUsable) DisableAutoCleanAfterSetupFailure(!forManual);
        if (forManual && !gCleanerTaskUsable) ShowCleanerNotification(L"Clean failed: cleaner could not start", CleanerNoticeKind::Failure);
        return false;
    }
    std::wstring parameters = L"--install " + gUserSid;
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.hwnd = gWnd;
    execute.lpVerb = L"runas";
    execute.lpFile = helper.c_str();
    execute.lpParameters = parameters.c_str();
    execute.nShow = SW_HIDE;
    RegWriteDword(L"AutoTaskReady", 0);
    BlockCleanerSetup(); // Persist the attempt before launching, including app exits during setup.
    if (!ShellExecuteExW(&execute)) {
        DWORD error = GetLastError();
        BlockCleanerSetup();
        gCleanerSetupForAuto = false;
        gCleanerSetupForManual = false;
        gStatus = L"Ready";
        if (gCleanerTaskUsable && (forManual || gAutoPurge)) {
            const bool started = RequestCleanerTaskRun(forManual);
            if (started) gStatus = forManual ?
                L"Using the existing cleaner; the update was skipped." :
                L"Auto Clean is using the existing cleaner; the update was skipped.";
            return started;
        }
        if (forAuto && !gCleanerTaskUsable) DisableAutoCleanAfterSetupFailure(!forManual);
        if (forManual && !gCleanerTaskUsable) ShowCleanerNotification(
            error == ERROR_CANCELLED ? L"Clean failed: request was cancelled" : L"Clean failed: cleaner could not start",
            CleanerNoticeKind::Failure);
        return false;
    }
    gCleanerSetupProcess = execute.hProcess;
    gStatus = L"Ready";
    return true;
}
static bool RequestCleanerTaskRun(bool manual) {
    if (!gCleanerTaskUsable) {
        if(!ShouldPromptCleanerSetup(false,gCleanerSetupBlocked,manual))return false;
        return StartCleanerSetup(gAutoPurge, manual);
    }
    if (manual && !gManualCleanerPending) {
        if (gCleanerRequestId == (std::numeric_limits<uint64_t>::max)()) {
            gStatus = L"Ready";
            ShowCleanerNotification(L"Clean failed: could not create a request", CleanerNoticeKind::Failure);
            return false;
        }
        ++gCleanerRequestId;
        gManualCleanerPending = true;
        gLastDisplayedManualRequestId = gCleanerRequestId - 1;
    }
    if (!WriteCleanerSettings()) {
        if (manual) {
            gManualCleanerPending = false;
            ShowCleanerNotification(L"Clean failed: could not save the request", CleanerNoticeKind::Failure);
        }
        return false;
    }
    if (!RunRegisteredCleanerTask()) {
        gStatus = L"Ready";
        if (manual) {
            gManualCleanerPending = false;
            ShowCleanerNotification(L"Clean failed: Windows could not start it", CleanerNoticeKind::Failure);
        }
        return false;
    }
    if (manual) gStatus = L"Ready";
    return true;
}
static void PollCleanerSetup() {
    if (!gCleanerSetupProcess || WaitForSingleObject(gCleanerSetupProcess, 0) != WAIT_OBJECT_0) return;
    DWORD setupExit = 1;
    GetExitCodeProcess(gCleanerSetupProcess, &setupExit);
    CloseHandle(gCleanerSetupProcess);
    gCleanerSetupProcess = nullptr;
    const bool forAuto = gCleanerSetupForAuto, forManual = gCleanerSetupForManual;
    gCleanerSetupForAuto = false; gCleanerSetupForManual = false;
    RefreshCleanerSetupState();
    RegWriteDword(L"AutoTaskReady", gCleanerTaskUsable ? 1 : 0);
    if (setupExit != 0 || !gCleanerTaskUsable || !gCleanerInstalled || !gCleanerCurrentVersion) {
        BlockCleanerSetup();
        bool fallbackStarted = false;
        if (gCleanerTaskUsable) {
            SaveSettings();
            if (forManual) fallbackStarted = RequestCleanerTaskRun(true);
            else if (gAutoPurge) fallbackStarted = RequestCleanerTaskRun(false);
        }
        else if (forAuto) DisableAutoCleanAfterSetupFailure(!forManual);
        gStatus = L"Ready";
        if (forManual && !fallbackStarted)
            ShowCleanerNotification(L"Clean failed: Windows could not start the cleaner", CleanerNoticeKind::Failure);
        return;
    }
    gCleanerSetupBlocked = false;
    RegWriteDword(L"CleanerSetupBlockedVersion", 0);
    SaveSettings();
    if (forManual) RequestCleanerTaskRun(true);
    else gStatus = L"Ready";
}
static void PollCleanerStatus() {
    if (!gCleanerTaskUsable) return;
    CleanerStatus latest;
    if (!ReadCleanerStatus(latest)) return;
    gCleanerStatus = latest;
    if (latest.completedManualRequestId > gLastDisplayedManualRequestId &&
        latest.completedManualRequestId >= gCleanerRequestId) {
        gLastDisplayedManualRequestId = latest.completedManualRequestId;
        gManualCleanerPending = false;
        if (IsNtOk(static_cast<LONG>(latest.lastManualStatus))) {
            UpdateMetrics();
            gPurgeLatched = true;
            gLastPurge = GetTickCount64();
            const int32_t status = static_cast<int32_t>(latest.lastManualStatus);
            const bool sizesKnown = latest.manualStandbyValid ||
                (gManualStandbyBeforeKnown && gMetrics.standbyKnown);
            const uint64_t before = latest.manualStandbyValid ? latest.manualStandbyBefore : gManualStandbyBefore;
            const uint64_t after = latest.manualStandbyValid ? latest.manualStandbyAfter :
                static_cast<uint64_t>(gMetrics.standby);
            if (sizesKnown && StandbyCleanSucceeded(status, before, after, gPageSize)) {
                const uint64_t released = before - after;
                ShowCleanerNotification(L"Clean succeeded: " + Bytes(static_cast<double>(released)) + L" freed",
                    CleanerNoticeKind::Success);
            } else if (status >= 0) {
                ShowCleanerNotification(sizesKnown ? L"Clean failed: standby size unchanged" :
                    L"Clean could not be verified: standby size unavailable", CleanerNoticeKind::Failure);
            } else {
                ShowCleanerNotification(L"Clean failed: Windows rejected the request", CleanerNoticeKind::Failure);
            }
            gManualStandbyBefore = 0;
            gManualStandbyBeforeKnown = false;
            gPurgeLatched = true;
            gLastPurge = GetTickCount64();
            gStatus = L"Ready";
        } else {
            gManualStandbyBefore = 0;
            gManualStandbyBeforeKnown = false;
            ShowCleanerNotification(L"Clean failed: Windows rejected the request", CleanerNoticeKind::Failure);
            gStatus = L"Ready";
        }
    }
    if (latest.lastAutoTick && latest.lastAutoTick != gLastSeenPurgeTick) {
        gLastSeenPurgeTick = latest.lastAutoTick;
        gStatus = IsNtOk(static_cast<LONG>(latest.lastAutoStatus)) ?
            L"Automatic standby cleaning finished." : L"Automatic standby cleaning was denied by Windows.";
    }
}
static void DoPurge() {
    if (gManualCleanerPending) {
        ShowCleanerNotification(L"Clean is already running", CleanerNoticeKind::Info);
        return;
    }
    UpdateMetrics();
    if (gMetrics.standbyKnown && gMetrics.standby < static_cast<double>(gPageSize)) {
        ShowCleanerNotification(L"Standby list is already empty", CleanerNoticeKind::Info);
        return;
    }
    gManualStandbyBefore = static_cast<uint64_t>(gMetrics.standby);
    gManualStandbyBeforeKnown = gMetrics.standbyKnown;
    if (!gCleanerTaskUsable) {
        StartCleanerSetup(false, true);
        return;
    }
    RequestCleanerTaskRun(true);
}
static std::string JsonString(const std::string& json,const std::string& key,size_t from=0) {
    std::string marker="\""+key+"\"";size_t p=json.find(marker,from);if(p==std::string::npos)return {};
    p=json.find(':',p+marker.size());if(p==std::string::npos)return {};p++;
    while(p<json.size()&&(json[p]==' '||json[p]=='\t'||json[p]=='\r'||json[p]=='\n'))p++;
    if(p>=json.size()||json[p]!='"')return {};p++;
    std::string out;bool escaped=false;
    for(;p<json.size();p++){char c=json[p];if(escaped){out.push_back(c);escaped=false;}else if(c=='\\')escaped=true;else if(c=='"')break;else out.push_back(c);}
    return out;
}
static bool VersionNewer(const std::wstring& latest,const std::wstring& current) {
    auto parse=[](std::wstring v){
        if(!v.empty()&&(v[0]==L'v'||v[0]==L'V'))v.erase(v.begin());
        long a[3]={0,0,0};size_t start=0;
        for(int i=0;i<3&&start<v.size();i++){
            wchar_t* end=nullptr;a[i]=wcstol(v.c_str()+start,&end,10);
            if(end==v.c_str()+start)break;
            start=static_cast<size_t>(end-v.c_str());if(start<v.size()&&v[start]==L'.')start++;else break;
        }
        return std::array<long,3>{a[0],a[1],a[2]};
    };
    auto a=parse(latest),b=parse(current);return a>b;
}
static bool VerifySha256File(const std::wstring& path,const std::string& digest) {
    const std::string prefix="sha256:";
    if(digest.compare(0,prefix.size(),prefix)!=0||digest.size()!=prefix.size()+64)return false;
    std::string expected=digest.substr(prefix.size());
    for(char& c:expected)if(c>='A'&&c<='F')c=static_cast<char>(c-'A'+'a');
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))return false;
    bool ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)!=FALSE;
    HANDLE file=INVALID_HANDLE_VALUE;
    if(ok)file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)ok=false;
    BYTE buffer[65536];DWORD got=0;
    while(ok){
        if(!ReadFile(file,buffer,sizeof(buffer),&got,nullptr)){ok=false;break;}
        if(!got)break;
        if(!CryptHashData(hash,buffer,got,0)){ok=false;break;}
    }
    BYTE value[32]{};DWORD valueSize=sizeof(value);
    if(ok&&!CryptGetHashParam(hash,HP_HASHVAL,value,&valueSize,0))ok=false;
    std::string actual;
    if(ok){static const char hex[]="0123456789abcdef";actual.reserve(64);for(BYTE b:value){actual.push_back(hex[b>>4]);actual.push_back(hex[b&15]);}}
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(hash)CryptDestroyHash(hash);
    CryptReleaseContext(provider,0);
    return ok&&actual==expected;
}
static bool DownloadVerifiedSetup(const std::wstring& url,const std::string& digest,std::wstring& output,std::wstring& error) {
    URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);
    parts.dwSchemeLength=static_cast<DWORD>(-1);parts.dwHostNameLength=static_cast<DWORD>(-1);
    parts.dwUrlPathLength=static_cast<DWORD>(-1);parts.dwExtraInfoLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(url.c_str(),0,0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS){error=L"The setup download URL is not a valid HTTPS address.";return false;}
    std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength);
    if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    wchar_t tempDir[MAX_PATH+1]{},tempFile[MAX_PATH+1]{};
    DWORD tempLength=GetTempPathW(MAX_PATH,tempDir);
    if(!tempLength||tempLength>MAX_PATH||!GetTempFileNameW(tempDir,L"NLI",0,tempFile)){error=L"Could not create a temporary setup file.";return false;}
    DeleteFileW(tempFile);
    if(!PathRenameExtensionW(tempFile,L".exe")){error=L"Could not prepare the temporary setup file.";return false;}
    output=tempFile;
    HANDLE file=CreateFileW(output.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr);
    if(file==INVALID_HANDLE_VALUE){output.clear();error=L"Could not open the temporary setup file.";return false;}
    bool ok=false;uint64_t total=0;
    HINTERNET session=WinHttpOpen(L"N-Lite updater",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(session){
        WinHttpSetTimeouts(session,10000,10000,15000,30000);
        HINTERNET conn=WinHttpConnect(session,host.c_str(),parts.nPort,0);
        if(conn){
            HINTERNET request=WinHttpOpenRequest(conn,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
            if(request){
                WinHttpAddRequestHeaders(request,L"User-Agent: N-Lite updater\r\n",static_cast<DWORD>(-1),WINHTTP_ADDREQ_FLAG_ADD);
                if(WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,nullptr)){
                    DWORD code=0,cb=sizeof(code);
                    if(WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&code,&cb,nullptr)&&code==200){
                        ok=true;DWORD available=0;BYTE buffer[65536];
                        while(ok&&WinHttpQueryDataAvailable(request,&available)&&available){
                            DWORD remaining=available;
                            while(ok&&remaining){
                                DWORD amount=(std::min)(remaining,static_cast<DWORD>(sizeof(buffer))),got=0,written=0;
                                if(!WinHttpReadData(request,buffer,amount,&got)||!got){ok=false;break;}
                                total+=got;if(total>64ull*1024ull*1024ull){ok=false;break;}
                                if(!WriteFile(file,buffer,got,&written,nullptr)||written!=got){ok=false;break;}
                                remaining-=got;
                            }
                        }
                        if(!total)ok=false;
                    }
                }
                WinHttpCloseHandle(request);
            }
            WinHttpCloseHandle(conn);
        }
        WinHttpCloseHandle(session);
    }
    if(!CloseHandle(file))ok=false;
    if(ok&&!VerifySha256File(output,digest)){ok=false;error=L"The downloaded setup did not match GitHub's SHA-256 checksum.";}
    if(!ok){
        DeleteFileW(output.c_str());output.clear();
        if(error.empty())error=L"Could not download the N-Lite setup installer.";
        return false;
    }
    return true;
}
struct UpdateInstallResult { bool ok=false;std::wstring path,message; };
static void InstallLatestUpdate() {
    bool expected=false;
    if(!gUpdateAvailable.load(std::memory_order_acquire)||!gUpdateInstallInProgress.compare_exchange_strong(expected,true))return;
    gUpdateInstallMessage.clear();
    std::wstring url=gInstallerUrl;
    std::string digest;
    for(wchar_t c:gInstallerDigest)digest.push_back(static_cast<char>(c));
    std::thread([url,digest](){
        auto result=new UpdateInstallResult{};
        result->ok=DownloadVerifiedSetup(url,digest,result->path,result->message);
        if(!result->ok)gUpdateInstallInProgress.store(false,std::memory_order_release);
        HWND target=gWnd;
        if(!target||!PostMessageW(target,WM_UPDATE_INSTALL_DONE,0,reinterpret_cast<LPARAM>(result))){
            if(result->ok&&!result->path.empty())DeleteFileW(result->path.c_str());
            delete result;
        }
    }).detach();
}
static void CheckForUpdatesAsync() {
    if(gUpdateCheckInProgress.exchange(true))return;
    gUpdateCheckNoRelease.store(false,std::memory_order_release);
    gUpdateInstallerMissing.store(false,std::memory_order_release);
    std::thread([](){
        std::wstring latest,installerUrl,installerDigest;bool noRelease=false;
        HINTERNET session=WinHttpOpen(L"N-Lite",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
        if(session){
            WinHttpSetTimeouts(session,4000,4000,4000,10000);
            HINTERNET conn=WinHttpConnect(session,L"api.github.com",INTERNET_DEFAULT_HTTPS_PORT,0);
            if(conn){
                HINTERNET req=WinHttpOpenRequest(conn,L"GET",L"/repos/gxlka/N-Lite/releases/latest",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
                if(req){
                    WinHttpAddRequestHeaders(req,L"Accept: application/vnd.github+json\r\nUser-Agent: N-Lite\r\n",static_cast<DWORD>(-1),WINHTTP_ADDREQ_FLAG_ADD);
                    if(WinHttpSendRequest(req,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(req,nullptr)){
                        DWORD code=0,cb=sizeof(code);
                        if(WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&code,&cb,nullptr)){
                            if(code==404)noRelease=true;
                            else if(code==200){
                                std::string body;DWORD available=0;
                                while(WinHttpQueryDataAvailable(req,&available)&&available&&body.size()<2*1024*1024){
                                    size_t old=body.size(),amount=(std::min)(static_cast<size_t>(available),2*1024*1024-body.size());body.resize(old+amount);DWORD got=0;
                                    if(!WinHttpReadData(req,&body[old],static_cast<DWORD>(amount),&got)){body.resize(old);break;}
                                    body.resize(old+got);
                                }
                                std::string tag=JsonString(body,"tag_name");
                                latest.assign(tag.begin(),tag.end());
                                size_t asset=body.find("\"name\":\"N-Lite-Setup-x64.exe\"");
                                if(asset!=std::string::npos){
                                    std::string download=JsonString(body,"browser_download_url",asset);
                                    std::string digest=JsonString(body,"digest",asset);
                                    const std::string allowed="https://github.com/gxlka/N-Lite/releases/download/";
                                    if(download.compare(0,allowed.size(),allowed)==0&&digest.compare(0,7,"sha256:")==0){
                                        installerUrl.assign(download.begin(),download.end());installerDigest.assign(digest.begin(),digest.end());
                                    }
                                }
                            }
                        }
                    }
                    WinHttpCloseHandle(req);
                }
                WinHttpCloseHandle(conn);
            }
            WinHttpCloseHandle(session);
        }
        if(!latest.empty()){
            bool newer=VersionNewer(latest,APP_VERSION);
            gUpdateCheckSucceeded.store(true,std::memory_order_release);
            gUpdateCheckNoRelease.store(false,std::memory_order_release);
            if(newer){
                gLatestVersion=latest;gInstallerUrl=installerUrl;gInstallerDigest=installerDigest;
                bool ready=!installerUrl.empty()&&!installerDigest.empty();
                gUpdateInstallerMissing.store(!ready,std::memory_order_release);
                gUpdateAvailable.store(ready,std::memory_order_release);
            }else{
                gUpdateAvailable.store(false,std::memory_order_release);
                gUpdateInstallerMissing.store(false,std::memory_order_release);
            }
        }else if(noRelease){
            gUpdateCheckSucceeded.store(true,std::memory_order_release);
            gUpdateCheckNoRelease.store(true,std::memory_order_release);
            gUpdateAvailable.store(false,std::memory_order_release);
        }else{
            gUpdateCheckSucceeded.store(false,std::memory_order_release);
            gUpdateCheckNoRelease.store(false,std::memory_order_release);
        }
        gUpdateCheckInProgress.store(false,std::memory_order_release);
        if(gWnd)PostMessageW(gWnd,WM_UPDATE_READY,0,0);
    }).detach();
}
static void DrawButton(HDC dc, RECT r, const std::wstring& s, int id, COLORREF bg = C_PANEL2, COLORREF fg = C_TEXT, bool accent = false) {
    Round(dc, r, bg, accent ? C_ACCENT : C_LINE, 9);
    Txt(dc, s, r.left + 8, r.top, W(r) - 16, H(r), fg, gFontMed, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    AddHit(r, id);
}
static RECT MainContent(int width) {
    int margin=(std::min)(30,(std::max)(20,width/40));
    return R(margin,0,(std::max)(1,width-2*margin),0);
}
static void DrawPageTitle(HDC dc,RECT content,const wchar_t* title,const wchar_t* subtitle) {
    Txt(dc,title,content.left,82,W(content),31,C_TEXT,gFontTitle);
    Txt(dc,subtitle,content.left,113,W(content),20,C_MUTED,gFont);
}
static void DrawHeader(HDC dc, int width,int height) {
    const int headerHeight=60;
    RECT top=R(0,0,width,headerHeight);Fill(dc,top,C_PANEL);
    Line(dc,0,headerHeight-1,width,headerHeight-1,C_LINE);
    Round(dc,R(20,15,32,32),C_ACCENT,C_ACCENT,8);
    Txt(dc,L"N",20,15,32,32,RGB(255,255,255),gFontMed,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    Txt(dc,L"N-Lite",61,0,126,headerHeight,C_TEXT,gFontMed,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    const int navWidth=103, navGap=4, navTotal=4*navWidth+3*navGap;
    const int navStart=(width-navTotal)/2;
    auto nav=[&](int index,int id,const wchar_t* label,int page){
        RECT r=R(navStart+index*(navWidth+navGap),12,navWidth,40);
        bool active=gPage==page,hover=gNavHover==id;
        if(active)Round(dc,r,C_ACCENT_SOFT,C_ACCENT_SOFT,8);
        else if(hover)Round(dc,r,C_NAV_HOVER,C_NAV_HOVER,8);
        COLORREF fg=active?C_TEXT:C_MUTED;
        Txt(dc,label,r.left+8,r.top,W(r)-16,H(r),fg,gFontMed,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(active)Fill(dc,R(r.left+24,r.bottom-3,W(r)-48,2),C_ACCENT);
        AddHit(r,id);
    };
    nav(0,ID_MEMORY,L"Memory",0);
    nav(1,ID_PROCESSES,L"Processes",1);
    nav(2,ID_STARTUP,L"Startup",2);
    nav(3,ID_SETTINGS,L"Settings",3);
    RECT theme=R(width-112,13,92,38);
    Round(dc,theme,C_PANEL2,gNavHover==ID_THEME?C_ACCENT:C_LINE,8);
    Txt(dc,gDarkTheme?L"Dark mode":L"Light mode",theme.left+5,theme.top,W(theme)-10,H(theme),C_TEXT,gFontSmall,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    AddHit(theme,ID_THEME);
    (void)height;
}
static HICON GetProcessIcon(const ProcRow& p) {
    if(p.path.empty()||p.path==L"Path unavailable")return nullptr;
    auto found=gProcessIcons.find(p.path);if(found!=gProcessIcons.end())return found->second;
    SHFILEINFOW info{};HICON icon=nullptr;
    if(SHGetFileInfoW(p.path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_SMALLICON))icon=info.hIcon;
    gProcessIcons.emplace(p.path,icon);return icon;
}
static void DrawProcesses(HDC dc, int cw, int ch) {
    RECT content=MainContent(cw);int tableX=content.left,tableW=W(content),tableY=193;
    DrawPageTitle(dc,content,L"Processes",L"Show current-user processes or the full process list.");
    RECT search=R(content.left,142,W(content)-228,38);
    Round(dc,search,C_FIELD,gSearchFocus?C_ACCENT:C_LINE,9);
    Txt(dc,gSearch.empty()?L"Search processes by name or path":L"Search  ·  "+gSearch,search.left+14,search.top, W(search)-28,H(search),gSearch.empty()?C_MUTED:C_TEXT,gFont);
    AddHit(search,ID_SEARCH);
    DrawButton(dc,R(content.right-212,142,96,38),gShowAllProcesses?L"My apps":L"Show all",ID_PROCESS_FILTER);
    DrawButton(dc,R(content.right-108,142,108,38),L"Refresh",ID_REFRESH);
    RECT table=R(tableX,tableY,tableW,ch-tableY-25);Card(dc,table);
    int nameX=tableX+56;
    const int columnGap=12,pidWidth=78,cpuWidth=84,memoryWidth=116,privateWidth=116;
    int privateX=table.right-20-privateWidth;
    int ramX=privateX-columnGap-memoryWidth;
    int cpuX=ramX-columnGap-cpuWidth;
    int pidX=cpuX-columnGap-pidWidth;
    auto sortLabel=[&](int col,const wchar_t* name){return std::wstring(name)+(gSortColumn==col?(gSortDescending?L"  ↓":L"  ↑"):L"");};
    COLORREF nameColor=gSortColumn==0?C_ACCENT:C_MUTED,pidColor=gSortColumn==1?C_ACCENT:C_MUTED;
    COLORREF cpuColor=gSortColumn==2?C_ACCENT:C_MUTED,memColor=gSortColumn==3?C_ACCENT:C_MUTED,privateColor=gSortColumn==4?C_ACCENT:C_MUTED;
    Txt(dc,sortLabel(0,L"NAME"),nameX,tableY+8,pidX-nameX-12,24,nameColor,gFontSmall);
    Txt(dc,sortLabel(1,L"PID"),pidX,tableY+8,cpuX-pidX-12,24,pidColor,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    Txt(dc,sortLabel(2,L"CPU"),cpuX,tableY+8,ramX-cpuX-12,24,cpuColor,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    Txt(dc,sortLabel(3,L"MEMORY"),ramX,tableY+8,privateX-ramX-12,24,memColor,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    Txt(dc,sortLabel(4,L"PRIVATE"),privateX,tableY+8,table.right-privateX-20,24,privateColor,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    int headerY=tableY+4;
    AddHit(R(tableX+8,headerY,pidX-tableX-8,30),ID_SORT_NAME);
    AddHit(R(pidX,headerY,cpuX-pidX-4,30),ID_SORT_PID);
    AddHit(R(cpuX,headerY,ramX-cpuX-4,30),ID_SORT_CPU);
    AddHit(R(ramX,headerY,privateX-ramX-4,30),ID_SORT_MEMORY);
    AddHit(R(privateX,headerY,table.right-privateX-8,30),ID_SORT_PRIVATE);
    Line(dc,tableX+12,tableY+35,table.right-12,tableY+35,C_LINE);
    const int rowH=43,firstY=tableY+40;
    int rows=(std::max)(0,static_cast<int>(table.bottom-firstY-31)/rowH);
    int maxScroll=(std::max)(0,static_cast<int>(gVisible.size())-rows);
    gScroll=(std::max)(0,(std::min)(gScroll,maxScroll));
    for(int i=0;i<rows&&gScroll+i<static_cast<int>(gVisible.size());i++){
        const ProcRow& p=gVisible[gScroll+i];int y=firstY+i*rowH;
        RECT rr=R(tableX+7,y,tableW-14,rowH-2);
        bool selected=p.pid==gSelectedPid,hover=p.pid==gHoveredPid;
        if(selected)Round(dc,rr,C_SELECTED,C_ACCENT_SOFT,8);
        else if(hover)Round(dc,rr,C_NAV_HOVER,C_NAV_HOVER,8);
        else if(i%2)Round(dc,rr,C_ROW,C_ROW,8);
        AddHit(rr,100,p.pid);
        int depth=static_cast<int>(p.ppid), base=tableX+17+(std::min)(depth,8)*17;
        if(p.hasChildren){
            POINT tri[3];int ty=y+15;
            if(gExpanded[p.groupKey]){tri[0]={base,ty};tri[1]={base+9,ty};tri[2]={base+4,ty+6};}
            else{tri[0]={base,ty};tri[1]={base,ty+9};tri[2]={base+6,ty+4};}
            HBRUSH b=CreateSolidBrush(selected?C_ACCENT:C_MUTED);HGDIOBJ old=SelectObject(dc,b);Polygon(dc,tri,3);SelectObject(dc,old);DeleteObject(b);
            AddHit(R(base-4,y+5,21,rowH-12),101,p.pid);
        }
        int ix=base+13;HICON icon=GetProcessIcon(p);
        if(icon)DrawIconEx(dc,ix,y+12,icon,18,18,0,nullptr,DI_NORMAL);
        else {Round(dc,R(ix,y+13,16,16),C_TRACK,C_TRACK,5);Txt(dc,L"N",ix,y+12,16,18,C_ACCENT,gFontSmall,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
        Txt(dc,p.name,ix+24,y,pidX-(ix+29),rowH-2,selected?C_TEXT:C_TEXT,gFont);
        Txt(dc,std::to_wstring(p.pid),pidX,y,cpuX-pidX-12,rowH-2,C_MUTED,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        Txt(dc,Percent(p.cpu),cpuX,y,ramX-cpuX-12,rowH-2,C_TEXT,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        SIZE_T shownWorking=p.groupHeader?p.treeWorking:p.working;
        SIZE_T shownPrivate=p.groupHeader?p.treePrivateBytes:p.privateBytes;
        Txt(dc,Bytes(shownWorking),ramX,y,privateX-ramX-12,rowH-2,C_TEXT,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        Txt(dc,Bytes(shownPrivate),privateX,y,table.right-privateX-20,rowH-2,C_MUTED,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    }
    if(gVisible.empty())Txt(dc,L"No processes match that search.",tableX+22,firstY+20,tableW-44,36,C_MUTED,gFont);
    if(maxScroll>0){
        RECT track=R(table.right-7,firstY,3,rows*rowH);Fill(dc,track,C_TRACK);
        int thumbH=(std::max)(24,H(track)*rows/static_cast<int>(gVisible.size()));
        int thumbY=track.top+(H(track)-thumbH)*gScroll/maxScroll;
        Round(dc,R(track.left-2,thumbY,7,thumbH),C_ACCENT,C_ACCENT,5);
    }
    Line(dc,tableX+14,table.bottom-28,table.right-14,table.bottom-28,C_LINE);
    Txt(dc,std::to_wstring(gVisible.size())+L" processes",tableX+18,table.bottom-25,150,19,C_MUTED,gFontSmall);
    Txt(dc,L"Right-click for actions  ·  Expand groups with the chevron or double-click",tableX+170,table.bottom-25,tableW-190,19,C_MUTED,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
}
static void DrawSwitch(HDC dc,RECT r,bool enabled,int id,DWORD data=0) {
    Round(dc,r,enabled?C_ACCENT:C_TRACK,enabled?C_ACCENT:C_LINE,H(r)/2);
    int d=H(r)-6,x=enabled?r.right-d-3:r.left+3;
    Round(dc,R(x,r.top+3,d,d),RGB(250,251,255),RGB(250,251,255),d/2);
    AddHit(r,id,data);
}
static void DrawStartupDelete(HDC dc,RECT r,bool enabled,DWORD data) {
    Round(dc,r,enabled?C_PANEL2:C_ROW,enabled?C_LINE:C_ROW,8);
    const COLORREF ink=enabled?C_RED:C_MUTED;
    const int cx=(r.left+r.right)/2,top=r.top+8;
    Line(dc,cx-7,top+3,cx+7,top+3,ink,2);
    Line(dc,cx-4,top,cx+4,top,ink,2);
    Line(dc,cx-5,top+5,cx-4,top+17,ink,2);
    Line(dc,cx+5,top+5,cx+4,top+17,ink,2);
    Line(dc,cx-4,top+17,cx+4,top+17,ink,2);
    Line(dc,cx-1,top+7,cx-1,top+14,ink,1);
    Line(dc,cx+2,top+7,cx+2,top+14,ink,1);
    if(enabled)AddHit(r,ID_STARTUP_DELETE,data);
}
static std::wstring IntervalLabel(unsigned seconds) {
    if(seconds<60)return std::to_wstring(seconds)+L" seconds";
    unsigned minutes=seconds/60;
    return std::to_wstring(minutes)+(minutes==1?L" minute":L" minutes");
}
static void DrawMetricCard(HDC dc, RECT r, const wchar_t* label, const std::wstring& value, const std::wstring& sub, COLORREF accent) {
    Card(dc,r);
    Round(dc,R(r.left+15,r.top+16,5,5),accent,accent,3);
    Txt(dc,label,r.left+28,r.top+11,W(r)-42,18,C_MUTED,gFontSmall);
    Txt(dc,value,r.left+16,r.top+31,W(r)-30,27,C_TEXT,gFontMed,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    Txt(dc,sub,r.left+16,r.top+57,W(r)-30,15,C_MUTED,gFontSmall,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
static void DrawMemory(HDC dc, int cw, int ch) {
    RECT content=MainContent(cw);
    DrawPageTitle(dc,content,L"Memory",L"");
    const MemoryLayout layout=ComputeMemoryLayout(cw,ch);
    RECT memory{layout.memory.left,layout.memory.top,layout.memory.right,layout.memory.bottom};
    RECT clean{layout.cleaner.left,layout.cleaner.top,layout.cleaner.right,layout.cleaner.bottom};
    RECT timer{layout.timer.left,layout.timer.top,layout.timer.right,layout.timer.bottom};
    Card(dc,memory);
    Card(dc,clean);

    const double used=(std::max)(0.0,gMetrics.total-gMetrics.available);
    const double total=(std::max)(1.0,gMetrics.total);
    const double usedRatio=(std::max)(0.0,(std::min)(1.0,used/total));
    const int pad=18;
    Txt(dc,L"Memory usage",memory.left+pad,memory.top+16,W(memory)-2*pad,24,C_TEXT,gFontMed);
    Txt(dc,L"IN USE",memory.left+pad,memory.top+57,110,16,C_MUTED,gFontSmall);
    Txt(dc,Bytes(used)+L" / "+Bytes(gMetrics.total),memory.left+pad,memory.top+75,W(memory)-2*pad-92,34,C_TEXT,gFontBold);
    RECT percent=R(memory.right-91,memory.top+77,73,27);
    Round(dc,percent,C_ACCENT_SOFT,C_ACCENT_SOFT,13);
    Txt(dc,Percent(usedRatio*100)+L" used",percent.left+4,percent.top,W(percent)-8,H(percent),C_ACCENT,gFontSmall,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    const int barX=memory.left+pad,barY=memory.top+123,barW=W(memory)-2*pad;
    const int usedW=static_cast<int>(barW*usedRatio);
    const int standbyW=static_cast<int>(barW*(std::max)(0.0,(std::min)(1.0,gMetrics.standby/total)));
    const int freeW=(std::max)(0,barW-usedW-standbyW);
    Round(dc,R(barX,barY,barW,9),C_TRACK,C_TRACK,5);
    if(usedW>0)Round(dc,R(barX,barY,usedW,9),C_ACCENT,C_ACCENT,5);
    if(standbyW>0)Fill(dc,R(barX+usedW,barY,standbyW,9),C_AMBER);
    if(freeW>0)Round(dc,R(barX+usedW+standbyW,barY,freeW,9),C_GREEN,C_GREEN,5);
    Round(dc,R(barX,memory.top+145,7,7),C_ACCENT,C_ACCENT,4);
    Txt(dc,L"In use  "+Bytes(used),barX+13,memory.top+139,140,20,C_MUTED,gFontSmall);
    Round(dc,R(barX+151,memory.top+145,7,7),C_AMBER,C_AMBER,4);
    Txt(dc,L"Standby  "+(gMetrics.standbyKnown?Bytes(gMetrics.standby):L"Unavailable"),
        barX+164,memory.top+139,160,20,C_MUTED,gFontSmall);
    Round(dc,R(barX+322,memory.top+145,7,7),C_GREEN,C_GREEN,4);
    Txt(dc,L"Free  "+Bytes(gMetrics.free),barX+335,memory.top+139,W(memory)-2*pad-335,20,C_MUTED,gFontSmall);
    Line(dc,memory.left+pad,memory.top+174,memory.right-pad,memory.top+174,C_LINE);

    const int rowX=memory.left+pad,rowW=W(memory)-2*pad;
    Txt(dc,L"Available",rowX,memory.top+187,150,24,C_MUTED,gFont);
    Txt(dc,Bytes(gMetrics.available),rowX+150,memory.top+185,rowW-150,28,C_TEXT,gFontMed,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    Txt(dc,L"Page file",rowX,memory.top+222,150,24,C_MUTED,gFont);
    Txt(dc,Bytes(gMetrics.pagefileUsed)+L" / "+Bytes(gMetrics.pagefileTotal),rowX+150,memory.top+220,rowW-150,28,C_TEXT,gFontMed,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    Txt(dc,L"Commit",rowX,memory.top+257,150,24,C_MUTED,gFont);
    Txt(dc,Bytes(gMetrics.commit)+L" / "+Bytes(gMetrics.commitLimit),rowX+150,memory.top+255,rowW-150,28,C_TEXT,gFontMed,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

    const int cleanPad=16;
    Txt(dc,L"Standby cleaner",clean.left+cleanPad,clean.top+16,W(clean)-2*cleanPad,24,C_TEXT,gFontMed);

    const int innerW=W(clean)-2*cleanPad;
    Txt(dc,L"Threshold",clean.left+cleanPad,clean.top+54,innerW,17,C_MUTED,gFontSmall);
    RECT threshold=R(clean.left+cleanPad,clean.top+74,innerW,35);
    Round(dc,threshold,C_FIELD,gThresholdFocus?C_ACCENT:C_LINE,8);
    Txt(dc,gThresholdFocus?gThresholdEdit:std::to_wstring(gThresholdMB),threshold.left+12,threshold.top, W(threshold)-20,H(threshold),C_TEXT,gFontMed,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    AddHit(threshold,ID_THRESHOLD_FIELD);

    Txt(dc,L"Automatic check",clean.left+cleanPad,clean.top+119,innerW,17,C_MUTED,gFontSmall);
    RECT interval=R(clean.left+cleanPad,clean.top+139,innerW,35);
    Round(dc,interval,C_FIELD,gIntervalOpen?C_ACCENT:C_LINE,8);
    Txt(dc,L"Every "+IntervalLabel(gIntervalSec),interval.left+12,interval.top,W(interval)-42,H(interval),C_TEXT,gFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    Txt(dc,gIntervalOpen?L"^":L"v",interval.right-30,interval.top,22,H(interval),C_MUTED,gFontMed,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    AddHit(interval,ID_INTERVAL_FIELD);

    Txt(dc,L"Auto clean",clean.left+cleanPad,clean.top+194,innerW-63,20,C_TEXT,gFontMed);
    DrawSwitch(dc,R(clean.right-cleanPad-46,clean.top+191,46,26),gAutoPurge,ID_AUTO);

    const ULONGLONG now = GetTickCount64();
    if (!gCleanerNotification.empty() && now < gCleanerNotificationUntil) {
        const COLORREF noticeColor = gCleanerNotificationKind == CleanerNoticeKind::Success ? C_GREEN :
            (gCleanerNotificationKind == CleanerNoticeKind::Failure ? C_RED : C_MUTED);
        RECT notice = R(clean.left+cleanPad,clean.top+222,innerW,22);
        Round(dc,notice,C_PANEL2,C_LINE,8);
        Txt(dc,gCleanerNotification,notice.left+9,notice.top,W(notice)-18,H(notice),noticeColor,gFontSmall);
    }

    DrawButton(dc,R(clean.left+cleanPad,clean.top+251,innerW,38),L"Clean now",ID_PURGE,C_ACCENT,RGB(255,255,255),true);

    Txt(dc,L"Timer resolution",timer.left+18,timer.top+12,210,22,C_TEXT,gFontMed);
    Txt(dc,L"Released when N-Lite exits.",timer.left+18,timer.top+38,205,17,C_MUTED,gFontSmall);
    const int sx=timer.left+232,sw=(std::max)(120,W(timer)-385),sy=timer.top+37;
    Fill(dc,R(sx,sy-2,sw,4),C_TRACK);
    gTimerSliderHit=R(sx,sy-15,sw,31);
    const double span=static_cast<double>(gTimerMaxResolution-gTimerMinResolution);
    const double bounded=static_cast<double>((std::max)(gTimerMinResolution,(std::min)(gTimerResolution,gTimerMaxResolution)));
    double ratio=span?static_cast<double>(bounded-gTimerMinResolution)/span:0.0;
    ratio=(std::max)(0.0,(std::min)(1.0,ratio));
    int knobX=sx+static_cast<int>(ratio*sw);
    Fill(dc,R(sx,sy-2,(std::max)(0,knobX-sx),4),C_ACCENT);
    Round(dc,R(knobX-7,sy-8,14,16),gTimerEnabled?C_ACCENT:C_MUTED,gTimerEnabled?C_ACCENT:C_MUTED,8);
    AddHit(gTimerSliderHit,ID_TIMER_PLUS);
    Txt(dc,TimerText(gTimerResolution),timer.right-138,timer.top+16,81,39,C_TEXT,gFontSmall,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    DrawSwitch(dc,R(timer.right-48,timer.top+23,32,26),gTimerEnabled,ID_TIMER_TOGGLE);

    if(gIntervalOpen){
        static const unsigned choices[]={60,120,300,600,900,1800,3600,7200};
        const int optionH=25,rows=4,popH=rows*optionH+10;
        RECT pop=R(interval.left,interval.bottom+4,W(interval),popH);
        Round(dc,pop,C_PANEL,C_LINE,9);
        for(int i=0;i<8;++i){
            const int column=i/rows,row=i%rows,colW=(W(pop)-14)/2;
            RECT option=R(pop.left+5+column*colW,pop.top+5+row*optionH,colW-2,optionH);
            const bool active=choices[i]==gIntervalSec,hover=gIntervalHover==i;
            if(hover||active)Round(dc,option,active?C_ACCENT_SOFT:C_NAV_HOVER,active?C_ACCENT_SOFT:C_NAV_HOVER,5);
            Txt(dc,IntervalLabel(choices[i]),option.left+7,option.top,W(option)-14,H(option),C_TEXT,gFontSmall);
            AddHit(option,ID_INTERVAL_OPTION,choices[i]);
        }
    }
}
static void DrawProcesses(HDC dc, int cw, int ch);
static void DrawStartup(HDC dc,int cw,int ch) {
    RECT content=MainContent(cw);DrawPageTitle(dc,content,L"Startup",L"Manage apps that launch when you sign in.");
    DrawButton(dc,R(content.right-218,142,100,35),L"Refresh",ID_REFRESH,C_PANEL2,C_TEXT);
    DrawButton(dc,R(content.right-110,142,110,35),L"Add app",ID_STARTUP_ADD,C_ACCENT,RGB(255,255,255),true);
    RECT list=R(content.left,187,W(content),ch-225);Card(dc,list);
    Txt(dc,L"Startup apps",list.left+17,list.top+11,W(list)-34,20,C_TEXT,gFontMed);
    Line(dc,list.left+13,list.top+37,list.right-13,list.top+37,C_LINE);
    const int rowTop=list.top+43,rowH=60,footerY=list.bottom-26;
    const int visibleRows=(std::max)(1,(footerY-rowTop)/rowH);
    gStartupScroll=(std::max)(0,(std::min)(gStartupScroll,(std::max)(0,static_cast<int>(gStartupEntries.size())-visibleRows)));
    if(gStartupEntries.empty()){
        Txt(dc,L"No startup apps were found.",list.left+18,rowTop+17,W(list)-36,24,C_MUTED,gFont);
        Txt(dc,L"Add an app to launch it for your Windows account.",list.left+18,rowTop+43,W(list)-36,20,C_MUTED,gFontSmall);
    }else{
        for(int row=0;row<visibleRows;++row){
            const int index=gStartupScroll+row;if(index>=static_cast<int>(gStartupEntries.size()))break;
            const StartupItem& item=gStartupEntries[static_cast<size_t>(index)];const int y=rowTop+row*rowH;
            RECT rr=R(list.left+9,y,W(list)-27,rowH-2);
            if(row%2)Round(dc,rr,C_ROW,C_ROW,7);
            if(item.canToggle)AddHit(rr,ID_STARTUP_TOGGLE,static_cast<DWORD>(index));
            const int actionWidth=93;
            Txt(dc,item.name,rr.left+12,rr.top+6,W(rr)-actionWidth-20,21,C_TEXT,gFontMed,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            Txt(dc,item.source+L"  ·  "+item.command,rr.left+12,rr.top+31,W(rr)-actionWidth-20,17,C_MUTED,gFontSmall,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            if(item.canToggle)DrawSwitch(dc,R(rr.right-91,rr.top+15,52,29),item.enabled,ID_STARTUP_TOGGLE,static_cast<DWORD>(index));
            else {
                const wchar_t* label=item.kind==StartupKind::WindowsShell||IsProtectedStartupTaskPath(item.taskPath)?L"Windows":
                    item.kind==StartupKind::ScheduledTask?L"Managed":
                    (item.enabled?L"All users":L"Disabled");
                Txt(dc,label,rr.right-98,rr.top+15,60,29,C_MUTED,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
            }
            DrawStartupDelete(dc,R(rr.right-34,rr.top+13,30,33),item.canDelete,static_cast<DWORD>(index));
        }
    }
    if(static_cast<int>(gStartupEntries.size())>visibleRows){
        RECT track=R(list.right-9,rowTop,3,footerY-rowTop);
        Fill(dc,track,C_TRACK);
        const int thumbH=(std::max)(24,H(track)*visibleRows/static_cast<int>(gStartupEntries.size()));
        const int maxScroll=static_cast<int>(gStartupEntries.size())-visibleRows;
        const int thumbY=track.top+(H(track)-thumbH)*gStartupScroll/maxScroll;
        Round(dc,R(track.left-2,thumbY,7,thumbH),C_ACCENT,C_ACCENT,4);
    }
    Line(dc,list.left+13,footerY-4,list.right-13,footerY-4,C_LINE);
    const std::wstring footerStatus=gStatus==L"Ready"?L"Read-only entries are protected Windows or all-users startup items.":gStatus;
    const size_t enabledCount=static_cast<size_t>(std::count_if(gStartupEntries.begin(),gStartupEntries.end(),[](const StartupItem& item){return item.enabled;}));
    Txt(dc,std::to_wstring(enabledCount)+L" active  ·  "+std::to_wstring(gStartupEntries.size()-enabledCount)+L" disabled  |  "+footerStatus,
        list.left+17,footerY,list.right-list.left-34,18,C_MUTED,gFontSmall,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
static void DrawSettings(HDC dc,int cw) {
    RECT content=MainContent(cw);DrawPageTitle(dc,content,L"Settings",L"Startup, update, and app preferences.");
    RECT card=R(content.left,151,W(content),158);Card(dc,card);
    Txt(dc,L"About N-Lite",card.left+20,card.top+18,W(card)-40,26,C_TEXT,gFontMed);
    Txt(dc,L"Version "+std::wstring(APP_VERSION),card.left+20,card.top+54,W(card)-40,21,C_MUTED,gFont);
    Txt(dc,L"Lightweight tools for memory and process management.",card.left+20,card.top+81,W(card)-40,21,C_MUTED,gFontSmall);
    std::wstring updateText;
    if(gUpdateCheckInProgress.load())updateText=L"Checking GitHub for updates…";
    else if(gUpdateInstallInProgress.load())updateText=L"Downloading and verifying the setup installer…";
    else if(!gUpdateInstallMessage.empty())updateText=gUpdateInstallMessage;
    else if(gUpdateAvailable.load())updateText=L"Version "+gLatestVersion+L" is ready to install.";
    else if(gUpdateInstallerMissing.load())updateText=L"A newer version was found, but its verified setup installer is unavailable.";
    else if(gUpdateCheckNoRelease.load())updateText=L"No GitHub release has been published yet.";
    else if(gUpdateCheckSucceeded.load())updateText=L"You are up to date.";
    else updateText=L"Update checks run at launch and every six hours. Select Check for updates to try again.";
    Txt(dc,updateText,card.left+20,card.top+111,W(card)-260,23,(gUpdateAvailable.load()||gUpdateInstallInProgress.load())?C_GREEN:C_MUTED,gFontSmall);
    DrawButton(dc,R(card.right-204,card.top+48,178,38),
        gUpdateInstallInProgress.load()?L"Please wait…":(gUpdateAvailable.load()?L"Install update":L"Check for updates"),
        gUpdateAvailable.load()?ID_UPDATE:ID_UPDATE_CHECK_NOW,C_ACCENT,RGB(255,255,255),true);
    RECT repo=R(content.left,326,W(content),88);Card(dc,repo);
    Txt(dc,L"Project",repo.left+20,repo.top+15,W(repo)-190,23,C_TEXT,gFontMed);
    Txt(dc,L"View N-Lite source, releases and setup builds on GitHub.",repo.left+20,repo.top+43,W(repo)-190,20,C_MUTED,gFontSmall);
    DrawButton(dc,R(repo.right-181,repo.top+25,155,37),L"Open GitHub",ID_OPEN_GITHUB,C_PANEL2,C_TEXT);
    RECT startup=R(content.left,430,W(content),82);Card(dc,startup);
    Txt(dc,L"Launch N-Lite with Windows",startup.left+20,startup.top+14,W(startup)-105,23,C_TEXT,gFontMed);
    Txt(dc,gAutoStart?L"N-Lite opens quietly when you sign in.":L"N-Lite only opens when you launch it.",startup.left+20,startup.top+42,W(startup)-105,20,C_MUTED,gFontSmall);
    DrawSwitch(dc,R(startup.right-70,startup.top+25,48,27),gAutoStart,ID_AUTOSTART);
}
static void Paint(HDC dc, int cw, int ch) {
    Fill(dc,R(0,0,cw,ch),C_BG);gHits.clear();
    DrawHeader(dc,cw,ch);
    if(gPage==0)DrawMemory(dc,cw,ch);
    else if(gPage==1)DrawProcesses(dc,cw,ch);
    else if(gPage==2)DrawStartup(dc,cw,ch);
    else DrawSettings(dc,cw);
}
struct PopupState {
    HWND hwnd=nullptr;DWORD pid=0;int hoverMain=-1;
    int anchorX=0,anchorY=0,width=440,height=176;
};
static PopupState gPopup;
static ProcRow* FindProcess(DWORD pid) {
    auto it=std::find_if(gProcs.begin(),gProcs.end(),[&](const ProcRow& p){return p.pid==pid;});
    return it==gProcs.end()?nullptr:&*it;
}
static void ResizePopup() {
    if(!gPopup.hwnd)return;
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    gPopup.width=(std::min)(440,W(work)-16);gPopup.height=(std::min)(176,H(work)-16);
    if(gPopup.width<340)gPopup.width=(std::max)(280,W(work));
    int x=gPopup.anchorX,y=gPopup.anchorY;
    if(x+gPopup.width>work.right)x=work.right-gPopup.width;
    if(y+gPopup.height>work.bottom)y=work.bottom-gPopup.height;
    if(x<work.left)x=work.left;if(y<work.top)y=work.top;
    SetWindowPos(gPopup.hwnd,HWND_TOPMOST,x,y,gPopup.width,gPopup.height,SWP_NOACTIVATE);
    HRGN region=CreateRoundRectRgn(0,0,gPopup.width+1,gPopup.height+1,16,16);
    SetWindowRgn(gPopup.hwnd,region,TRUE);
    InvalidateRect(gPopup.hwnd,nullptr,FALSE);
}
static void DrawPopupItem(HDC dc,int x,int y,int w,int h,const std::wstring& label,const wchar_t* glyph,bool hover,bool danger=false) {
    if(hover)Round(dc,R(x+4,y,w-8,h-2),C_NAV_HOVER,C_NAV_HOVER,8);
    Round(dc,R(x+12,y+7,23,23),danger?C_AMBER_SOFT:C_ACCENT_SOFT,danger?C_RED:C_ACCENT_SOFT,7);
    Txt(dc,glyph,x+12,y+7,23,23,danger?C_RED:C_ACCENT,gFontSmall,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    Txt(dc,label,x+45,y,w-58,h-2,danger?C_RED:C_TEXT,gFont);
}
static int PopupMainRow(int y){if(y>=10&&y<48)return 0;if(y>=68&&y<106)return 1;return -1;}
static void ClosePopup(){if(gPopup.hwnd)DestroyWindow(gPopup.hwnd);}
static void ApplyPopupCommand(int index) {
    ProcRow* p=FindProcess(gPopup.pid);if(!p){ClosePopup();return;}
    if(index==0){
        if(p->path.empty()||p->path==L"Path unavailable"){gStatus=L"Executable path is unavailable.";}
        else {std::wstring params=L"/select,\""+p->path+L"\"";ShellExecuteW(nullptr,L"open",L"explorer.exe",params.c_str(),nullptr,SW_SHOWNORMAL);gStatus=L"Opened the process location.";}
        ClosePopup();
    }else if(index==1){
        DWORD pid=p->pid;std::wstring name=p->name;ClosePopup();
        if(pid==GetCurrentProcessId()){gStatus=L"N-Lite cannot end itself.";InvalidateRect(gWnd,nullptr,FALSE);return;}
        if(MessageBoxW(gWnd,(L"End "+name+L"? Unsaved work in that process can be lost.").c_str(),L"End task",MB_YESNO|MB_ICONWARNING)==IDYES){
            HANDLE h=OpenProcess(PROCESS_TERMINATE,FALSE,pid);
            if(h&&TerminateProcess(h,1))gStatus=L"End task requested for "+name+L".";else gStatus=L"Windows denied permission to end this process.";
            if(h)CloseHandle(h);RefreshProcesses();
        }
        InvalidateRect(gWnd,nullptr,FALSE);
    }
}
static LRESULT CALLBACK PopupWndProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg){
    case WM_ERASEBKGND:return 1;
    case WM_KILLFOCUS:DestroyWindow(h);return 0;
    case WM_KEYDOWN:if(wp==VK_ESCAPE){DestroyWindow(h);return 0;}break;
    case WM_MOUSEMOVE:{
        const int row=PopupMainRow(GET_Y_LPARAM(lp));
        if(row!=gPopup.hoverMain){gPopup.hoverMain=row;InvalidateRect(h,nullptr,FALSE);}
        return 0;
    }
    case WM_LBUTTONUP:{
        const int row=PopupMainRow(GET_Y_LPARAM(lp));
        if(row>=0)ApplyPopupCommand(row);
        return 0;
    }
    case WM_PAINT:{
        PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT cr;GetClientRect(h,&cr);
        HBRUSH b=CreateSolidBrush(C_PANEL);FillRect(dc,&cr,b);DeleteObject(b);
        Line(dc,224,10,224,cr.bottom-10,C_LINE);
        DrawPopupItem(dc,0,10,224,38,L"Open file location",L"?",gPopup.hoverMain==0);
        Line(dc,13,57,211,57,C_LINE);
        DrawPopupItem(dc,0,68,224,38,L"End task",L"x",gPopup.hoverMain==1,true);
        ProcRow* p=FindProcess(gPopup.pid);
        if(p){
            auto group=std::find_if(gVisible.begin(),gVisible.end(),[&](const ProcRow& row){return row.groupHeader&&row.pid==gPopup.pid;});
            const bool grouped=group!=gVisible.end();
            HICON icon=GetProcessIcon(*p);if(icon)DrawIconEx(dc,242,20,icon,32,32,0,nullptr,DI_NORMAL);
            Txt(dc,p->name,282,17,gPopup.width-294,25,C_TEXT,gFontMed,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            Txt(dc,L"PID  "+std::to_wstring(p->pid),282,43,gPopup.width-294,18,C_MUTED,gFontSmall);
            Line(dc,238,72,gPopup.width-14,72,C_LINE);
            Txt(dc,grouped?L"Group working set":L"Working set",240,84,112,20,C_MUTED,gFontSmall);
            Txt(dc,Bytes(grouped?group->treeWorking:p->working),352,84,gPopup.width-364,20,C_TEXT,gFontMed,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
            Txt(dc,grouped?L"Group CPU usage":L"CPU usage",240,115,112,20,C_MUTED,gFontSmall);
            Txt(dc,Percent(grouped?group->cpu:p->cpu),352,115,gPopup.width-364,20,C_TEXT,gFontMed,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        }else Txt(dc,L"Process is no longer running.",240,24,gPopup.width-256,30,C_MUTED,gFont);
        EndPaint(h,&ps);return 0;
    }
    case WM_DESTROY:if(gPopup.hwnd==h)gPopup.hwnd=nullptr;return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
static void OpenProcessPopup(DWORD pid,int sx,int sy) {
    if(gPopup.hwnd)DestroyWindow(gPopup.hwnd);
    gSelectedPid=pid;gPopup=PopupState{};gPopup.pid=pid;gPopup.anchorX=sx;gPopup.anchorY=sy;
    gPopup.hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,POPUP_CLASS,L"",WS_POPUP,
        sx,sy,gPopup.width,gPopup.height,gWnd,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!gPopup.hwnd)return;
    ResizePopup();ShowWindow(gPopup.hwnd,SW_SHOW);SetForegroundWindow(gPopup.hwnd);SetFocus(gPopup.hwnd);
    InvalidateRect(gWnd,nullptr,FALSE);
}static void CommitThresholdEdit() {
    if(!gThresholdFocus)return;
    wchar_t* end=nullptr;unsigned long n=std::wcstoul(gThresholdEdit.c_str(),&end,10);
    if(end&&end!=gThresholdEdit.c_str()&&*end==0&&n){
        gThresholdMB=static_cast<unsigned>((std::max)(64ul,(std::min)(131072ul,n)));
        SaveSettings();gStatus=L"Ready";
    }else gStatus=L"Enter a threshold from 64 to 131072 MB.";
    gThresholdFocus=false;gThresholdReplaceOnType=false;gThresholdEdit.clear();
}
static void RefreshStartupEntries() {
    gStartupEntries=EnumerateStartupItems();gAutoStart=ReadAutoStart();gStartupLastRefresh=GetTickCount();
}
static void SaveToggleAuto() {
    if(gAutoPurge){gAutoPurge=kAutoCleanDefaultEnabled;SaveSettings();gStatus=L"Ready";return;}
    gAutoPurge=true;
    SaveSettings();
    if(gCleanerTaskUsable)
        RequestCleanerTaskRun(false);
    else if(gCleanerSetupProcess && WaitForSingleObject(gCleanerSetupProcess,0)==WAIT_TIMEOUT)
        StartCleanerSetup(true,false);
    else if(!ShouldPromptCleanerSetup(false,gCleanerSetupBlocked,false))
        DisableAutoCleanAfterSetupFailure(true);
    else StartCleanerSetup(true,false);
}
static void HandleClick(int x,int y,bool dbl) {
    Hit* target=nullptr;
    for(auto it=gHits.rbegin();it!=gHits.rend();++it)if(Inside(it->r,x,y)){target=&*it;break;}
    if(gIntervalOpen&&(!target||(target->id!=ID_INTERVAL_FIELD&&target->id!=ID_INTERVAL_OPTION))){gIntervalOpen=false;gIntervalHover=-1;}
    if(dbl&&(!target||(target->id!=100&&target->id!=101)))return;
    if(gThresholdFocus&&(!target||target->id!=ID_THRESHOLD_FIELD))CommitThresholdEdit();
    if(!target){gSearchFocus=false;InvalidateRect(gWnd,nullptr,FALSE);return;}
    int id=target->id;
    if(id>=ID_SORT_NAME&&id<=ID_SORT_PRIVATE){
        int next=id-ID_SORT_NAME;
        if(gSortColumn==next)gSortDescending=!gSortDescending;
        else{gSortColumn=next;gSortDescending=next>=2;}
        gScroll=0;RefreshProcesses();
    }
    else if(id==ID_MEMORY){gPage=0;gSearchFocus=false;}
    else if(id==ID_PROCESSES){gPage=1;gSearchFocus=false;RefreshProcesses();}
    else if(id==ID_STARTUP){gPage=2;gSearchFocus=false;gStartupScroll=0;RefreshStartupEntries();}
    else if(id==ID_SETTINGS){gPage=3;gSearchFocus=false;}
    else if(id==ID_THEME){gDarkTheme=!gDarkTheme;ApplyThemeColors();RegWriteDword(L"ThemeDark",gDarkTheme?1:0);ApplyWindowChromeTheme(gWnd);}
    else if(id==ID_UPDATE)InstallLatestUpdate();
    else if(id==ID_UPDATE_CHECK_NOW){CheckForUpdatesAsync();}
    else if(id==ID_OPEN_GITHUB)ShellExecuteW(gWnd,L"open",L"https://github.com/gxlka/N-Lite",nullptr,nullptr,SW_SHOWNORMAL);
    else if(id==ID_PROCESS_FILTER){gShowAllProcesses=!gShowAllProcesses;gScroll=0;SaveSettings();RefreshProcesses();gStatus=gShowAllProcesses?L"Showing all processes.":L"Showing current-user processes.";}
    else if(id==ID_REFRESH){
        if(gPage==2){RefreshStartupEntries();gStatus=L"Startup list refreshed.";}
        else{RefreshProcesses();UpdateMetrics();gStatus=L"Process list refreshed.";}
    }
    else if(id==ID_SEARCH){gSearchFocus=true;gThresholdFocus=false;}
    else if(id==ID_THRESHOLD_FIELD){gThresholdFocus=true;gThresholdReplaceOnType=true;gThresholdEdit=std::to_wstring(gThresholdMB);gSearchFocus=false;}
    else if(id==ID_AUTO)SaveToggleAuto();
    else if(id==ID_INTERVAL_FIELD){gIntervalOpen=!gIntervalOpen;gIntervalHover=-1;}
    else if(id==ID_INTERVAL_OPTION){gIntervalSec=target->data;gIntervalOpen=false;gIntervalHover=-1;SaveSettings();gStatus=L"Ready";}
    else if(id==ID_PURGE)DoPurge();
    else if(id==ID_AUTOSTART){
        bool next=!gAutoStart;
        if(SetAutoStart(next)){gAutoStart=next;gStatus=next?L"N-Lite will start with Windows.":L"Windows startup entry removed.";RefreshStartupEntries();}
        else gStatus=L"Could not update the current-user startup entry.";
    }
    else if(id==ID_STARTUP_ADD){
        std::wstring added;
        if(AddStartupApplication(gWnd,added)){gStatus=L"Added "+added+L" to startup.";RefreshStartupEntries();}
        else if(!added.empty())gStatus=L"Windows could not add that app to startup.";
    }
    else if(id==ID_STARTUP_TOGGLE){
        const size_t index=target->data;
        if(index<gStartupEntries.size()){
            StartupItem& item=gStartupEntries[index];bool ok=false;const bool next=!item.enabled;
            const bool ownStartup=item.kind==StartupKind::UserRun&&item.registryView==KEY_WOW64_64KEY&&_wcsicmp(item.name.c_str(),L"N-Lite")==0;
            ok=SetStartupItemEnabled(item,next);
            if(ok&&ownStartup)gAutoStart=next;
            if(ok){gStatus=next?L"Startup app enabled.":L"Startup app disabled.";RefreshStartupEntries();}
            else gStatus=L"Could not change that startup item.";
        }
    }
    else if(id==ID_STARTUP_DELETE){
        const size_t index=target->data;
        if(index<gStartupEntries.size()){
            if(DeleteStartupItem(gStartupEntries[index])){gStatus=L"Startup entry deleted.";RefreshStartupEntries();}
            else gStatus=L"Could not delete that startup entry.";
        }
    }
    else if(id==ID_TIMER_TOGGLE){
        gTimerEnabled=!gTimerEnabled;
        if(gTimerEnabled)SetTimerRequest(true);else SetTimerRequest(false);
        SaveSettings();
        if(gTimerEnabled&&gTimerActive)gStatus=L"Timer resolution enabled and saved.";
        else if(!gTimerEnabled)gStatus=L"Timer resolution disabled.";
    }
    else if(id==ID_TIMER_PLUS){UpdateTimerSlider(x);CommitTimerSlider();}
    else if(id==100||id==101){
        gSelectedPid=target->data;
        if((id==101&&!dbl)||(id==100&&dbl)){
            auto currentRow=std::find_if(gVisible.begin(),gVisible.end(),[&](const ProcRow& row){return row.pid==gSelectedPid&&row.groupHeader;});
            if(currentRow!=gVisible.end()){
                const std::wstring groupKey=currentRow->groupKey;
                int screenRow=static_cast<int>(currentRow-gVisible.begin())-gScroll;
                gExpanded[groupKey]=!gExpanded[groupKey];
                RefreshProcesses();
                auto anchored=std::find_if(gVisible.begin(),gVisible.end(),[&](const ProcRow& row){return row.groupHeader&&row.groupKey==groupKey;});
                if(anchored!=gVisible.end())gScroll=(std::max)(0,static_cast<int>(anchored-gVisible.begin())-screenRow);
            }
        }
    }
    InvalidateRect(gWnd,nullptr,FALSE);
}
static void ShowTrayMenu() {
    POINT p;GetCursorPos(&p);HMENU m=CreatePopupMenu();
    AppendMenuW(m,MF_STRING,ID_SHOW,L"Open N-Lite");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m,MF_STRING,ID_EXIT,L"Exit");
    SetForegroundWindow(gWnd);
    int cmd=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,gWnd,nullptr);
    DestroyMenu(m);
    if(cmd==ID_SHOW)ShowWindowFromTray();
    else if(cmd==ID_EXIT){gExiting=true;DestroyWindow(gWnd);}
}
static bool BeginTimerSliderDrag(int x,int y) {
    Hit* target=nullptr;
    for(auto it=gHits.rbegin();it!=gHits.rend();++it)if(Inside(it->r,x,y)){target=&*it;break;}
    if(!target||target->id!=ID_TIMER_PLUS)return false;
    gTimerDragOriginal=gTimerResolution;gTimerDragging=true;SetCapture(gWnd);
    UpdateTimerSlider(x);InvalidateRect(gWnd,nullptr,FALSE);return true;
}
static LRESULT CALLBACK WndProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg){
    case WM_CREATE: {
        gWnd=h; gUserSid=CurrentUserSid();gCleanerRoot=ProgramDataNlite();
        LoadNt(); LoadSettings(); gAutoStart=ReadAutoStart();gHasProcessOverrides=ProcessOverridesExist();
        SYSTEM_INFO si{};GetSystemInfo(&si);gPageSize=si.dwPageSize?si.dwPageSize:4096;
        LoadTimerRange();if(gTimerEnabled)SetTimerRequest(true);
        gFont=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        gFontSmall=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        gFontMed=CreateFontW(-16,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        gFontBold=CreateFontW(-22,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        gFontTitle=CreateFontW(-27,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        AddTray(); SetTimer(h,TIMER_REFRESH,2200,nullptr); SetTimer(h,TIMER_UPDATE_CHECK,6u*60u*60u*1000u,nullptr); UpdateMetrics(); RefreshProcesses();
        ApplyWindowChromeTheme(h);
        if(gAutoPurge){
            if(ShouldPromptCleanerSetup(gCleanerTaskUsable,gCleanerSetupBlocked,false))
                StartCleanerSetup(true,false);
            else if(!gCleanerTaskUsable)DisableAutoCleanAfterSetupFailure(false);
        }
        CheckForUpdatesAsync();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto m=reinterpret_cast<MINMAXINFO*>(lp);
        RECT minimum{0,0,960,620};
        AdjustWindowRectEx(&minimum,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,FALSE,0);
        m->ptMinTrackSize.x=W(minimum);m->ptMinTrackSize.y=H(minimum);return 0;
    }
    case WM_SIZE:
        if(wp==SIZE_MINIMIZED)HideToTray();else InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_CLOSE:
        if(!gExiting){HideToTray();return 0;} DestroyWindow(h);return 0;
    case WM_QUERYENDSESSION:
        if((lp&ENDSESSION_CLOSEAPP)!=0)return TRUE;
        return DefWindowProcW(h,msg,wp,lp);
    case WM_ENDSESSION:
        if(wp){gExiting=true;DestroyWindow(h);return 0;}
        return DefWindowProcW(h,msg,wp,lp);
    case WM_TRAY:
        if(lp==WM_LBUTTONUP||lp==WM_LBUTTONDBLCLK)ShowWindowFromTray();
        else if(lp==WM_RBUTTONUP||lp==WM_CONTEXTMENU)ShowTrayMenu();
        return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==ID_SHOW)ShowWindowFromTray();
        else if(LOWORD(wp)==ID_EXIT){gExiting=true;DestroyWindow(h);}
        return 0;
    case WM_TIMER:
        if(wp==TIMER_UPDATE_CHECK){CheckForUpdatesAsync();return 0;}
        if(wp==TIMER_REFRESH){
            PollCleanerSetup();
            PollCleanerStatus();
            if(gManualCleanerPending)RequestCleanerTaskRun(true);
            if(IsWindowVisible(h))UpdateMetrics();
            if(IsWindowVisible(h)&&gPage==2&&GetTickCount()-gStartupLastRefresh>=30000)RefreshStartupEntries();
            if((IsWindowVisible(h)&&gPage==1)||(gHasProcessOverrides&&GetTickCount()-gLastRefresh>=5000))RefreshProcesses();
            if(IsWindowVisible(h))InvalidateRect(h,nullptr,FALSE);
        } return 0;
    case WM_MOUSEMOVE:{
        if(gTimerDragging){const ULONG before=gTimerResolution;UpdateTimerSlider(GET_X_LPARAM(lp));if(before!=gTimerResolution)InvalidateRect(h,nullptr,FALSE);SetCursor(LoadCursorW(nullptr,IDC_HAND));return 0;}
        POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};DWORD hover=0;int nav=-1,intervalHover=-1;bool hand=false;
        static const unsigned choices[]={60,120,300,600,900,1800,3600,7200};
        for(auto it=gHits.rbegin();it!=gHits.rend();++it)if(Inside(it->r,pt.x,pt.y)){
            if(it->id==ID_MEMORY||it->id==ID_PROCESSES||it->id==ID_STARTUP||it->id==ID_SETTINGS||it->id==ID_THEME){nav=it->id;hand=true;}
            if(gPage==1&&(it->id==100||it->id==101)){hover=it->data;hand=true;}
            if(gPage==1&&it->id==ID_PROCESS_FILTER)hand=true;
            if(gPage==1&&it->id>=ID_SORT_NAME&&it->id<=ID_SORT_PRIVATE)hand=true;
            if(it->id==ID_INTERVAL_FIELD||it->id==ID_INTERVAL_OPTION)hand=true;
            if(gPage==2&&(it->id==ID_STARTUP_ADD||it->id==ID_STARTUP_TOGGLE||it->id==ID_STARTUP_DELETE||it->id==ID_REFRESH))hand=true;
            if(it->id==ID_INTERVAL_OPTION)for(int i=0;i<static_cast<int>(sizeof(choices)/sizeof(choices[0]));++i)if(choices[i]==it->data)intervalHover=i;
            break;
        }
        bool repaint=false;
        if(gPage==1&&hover!=gHoveredPid){gHoveredPid=hover;repaint=true;}
        if(nav!=gNavHover){gNavHover=nav;repaint=true;}
        if(intervalHover!=gIntervalHover){gIntervalHover=intervalHover;if(gIntervalOpen)repaint=true;}
        if(repaint)InvalidateRect(h,nullptr,FALSE);
        SetCursor(LoadCursorW(nullptr,hand?IDC_HAND:IDC_ARROW));return 0;
    }
    case WM_RBUTTONUP:{
        if(gPage==1){
            int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            for(auto it=gHits.rbegin();it!=gHits.rend();++it)if((it->id==100||it->id==101)&&Inside(it->r,x,y)){
                POINT pt{x,y};ClientToScreen(h,&pt);OpenProcessPopup(it->data,pt.x,pt.y);return 0;
            }
        }
        return 0;
    }
    case WM_LBUTTONDOWN:if(BeginTimerSliderDrag(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)))return 0;return 0;
    case WM_LBUTTONUP:
        if(gTimerDragging){
            UpdateTimerSlider(GET_X_LPARAM(lp));const bool changed=gTimerResolution!=gTimerDragOriginal;
            gTimerDragging=false;if(GetCapture()==h)ReleaseCapture();if(changed)CommitTimerSlider();
            InvalidateRect(h,nullptr,FALSE);return 0;
        }
        HandleClick(GET_X_LPARAM(lp),GET_Y_LPARAM(lp),false);return 0;
    case WM_LBUTTONDBLCLK:
        if(BeginTimerSliderDrag(GET_X_LPARAM(lp),GET_Y_LPARAM(lp)))return 0;
        HandleClick(GET_X_LPARAM(lp),GET_Y_LPARAM(lp),true);return 0;
    case WM_CAPTURECHANGED:
        if(gTimerDragging){gTimerDragging=false;gTimerResolution=gTimerDragOriginal;InvalidateRect(h,nullptr,FALSE);}return 0;
    case WM_MOUSEWHEEL:
        if(gPage==1){gScroll=(std::max)(0,gScroll-(GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA)*3);RefreshProcesses();InvalidateRect(h,nullptr,FALSE);}
        else if(gPage==2){gStartupScroll=(std::max)(0,gStartupScroll-(GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA)*3);InvalidateRect(h,nullptr,FALSE);}return 0;
    case WM_CHAR:
        if(gThresholdFocus){
            if(wp==13){CommitThresholdEdit();InvalidateRect(h,nullptr,FALSE);return 0;}
            if(wp==8){if(gThresholdReplaceOnType){gThresholdEdit.clear();gThresholdReplaceOnType=false;}else if(!gThresholdEdit.empty())gThresholdEdit.pop_back();}
            else if(wp>='0'&&wp<='9'){
                if(gThresholdReplaceOnType){gThresholdEdit.clear();gThresholdReplaceOnType=false;}
                if(gThresholdEdit.size()<6)gThresholdEdit.push_back(static_cast<wchar_t>(wp));
            }
            InvalidateRect(h,nullptr,FALSE);return 0;
        }
        if(gSearchFocus&&gPage==1){
            if(wp==8){if(!gSearch.empty())gSearch.pop_back();}
            else if(wp>=32&&wp<127&&gSearch.size()<80)gSearch.push_back(static_cast<wchar_t>(wp));
            gScroll=0;RefreshProcesses();InvalidateRect(h,nullptr,FALSE);return 0;
        } return 0;
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE&&gThresholdFocus){gThresholdFocus=false;gThresholdEdit.clear();gThresholdReplaceOnType=false;InvalidateRect(h,nullptr,FALSE);}
        else if(wp==VK_RETURN&&gThresholdFocus){CommitThresholdEdit();InvalidateRect(h,nullptr,FALSE);}
        else if(wp==L'A'&&(GetKeyState(VK_CONTROL)&0x8000)&&gThresholdFocus){gThresholdEdit.clear();gThresholdReplaceOnType=false;InvalidateRect(h,nullptr,FALSE);}
        else if(wp==VK_ESCAPE&&gSearchFocus){gSearch.clear();gSearchFocus=false;RefreshProcesses();InvalidateRect(h,nullptr,FALSE);}
        else if(gPage==1&&(wp==VK_DOWN||wp==VK_UP)){
            auto it=std::find_if(gVisible.begin(),gVisible.end(),[](const ProcRow& p){return p.pid==gSelectedPid;});
            int i=it==gVisible.end()?0:static_cast<int>(it-gVisible.begin());
            i=(std::max)(0,(std::min)(static_cast<int>(gVisible.size())-1,i+(wp==VK_DOWN?1:-1)));
            if(!gVisible.empty())gSelectedPid=gVisible[i].pid;InvalidateRect(h,nullptr,FALSE);
        } return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT cr;GetClientRect(h,&cr);int cw=W(cr),ch=H(cr);
        HDC mem=CreateCompatibleDC(dc);HBITMAP bm=CreateCompatibleBitmap(dc,cw,ch);HGDIOBJ old=SelectObject(mem,bm);
        Paint(mem,cw,ch);BitBlt(dc,0,0,cw,ch,mem,0,0,SRCCOPY);
        SelectObject(mem,old);DeleteObject(bm);DeleteDC(mem);EndPaint(h,&ps);return 0;
    }
    case WM_UPDATE_READY:InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_UPDATE_INSTALL_DONE: {
        std::unique_ptr<UpdateInstallResult> result(reinterpret_cast<UpdateInstallResult*>(lp));
        if(!result)return 0;
        if(!result->ok){
            gUpdateInstallMessage=result->message.empty()?L"Update download failed. Check your connection and try again.":result->message;
            InvalidateRect(h,nullptr,FALSE);return 0;
        }
        SHELLEXECUTEINFOW execute{};execute.cbSize=sizeof(execute);execute.fMask=SEE_MASK_NOCLOSEPROCESS;
        execute.lpVerb=L"open";execute.lpFile=result->path.c_str();
        execute.lpParameters=L"/SP- /SILENT /SUPPRESSMSGBOXES /NORESTART";execute.nShow=SW_SHOWNORMAL;
        if(!ShellExecuteExW(&execute)){
            DWORD error=GetLastError();DeleteFileW(result->path.c_str());
            gUpdateInstallMessage=L"Could not start the setup installer (Windows error "+std::to_wstring(error)+L").";
            gUpdateInstallInProgress.store(false,std::memory_order_release);
            InvalidateRect(h,nullptr,FALSE);return 0;
        }
        if(execute.hProcess)CloseHandle(execute.hProcess);
        gUpdateInstallMessage=L"Installing "+gLatestVersion+L" and restarting N-Lite…";
        gExiting=true;DestroyWindow(h);return 0;
    }
    case WM_DESTROY:
        if(gCleanerSetupProcess){CloseHandle(gCleanerSetupProcess);gCleanerSetupProcess=nullptr;}
        if(gPopup.hwnd)DestroyWindow(gPopup.hwnd);
        KillTimer(h,TIMER_REFRESH);KillTimer(h,TIMER_UPDATE_CHECK);SetTimerRequest(false);RemoveTray();
        for(auto& kv:gProcessIcons)if(kv.second)DestroyIcon(kv.second);
        gProcessIcons.clear();
        if(gIcon)DestroyIcon(gIcon);
        if(gFont)DeleteObject(gFont);if(gFontSmall)DeleteObject(gFontSmall);if(gFontMed)DeleteObject(gFontMed);if(gFontBold)DeleteObject(gFontBold);if(gFontTitle)DeleteObject(gFontTitle);
        PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE, PWSTR cmd,int show) {
    gExePath.resize(32768);DWORD n=GetModuleFileNameW(nullptr,gExePath.data(),static_cast<DWORD>(gExePath.size()));gExePath.resize(n);
    std::wstring args=cmd?cmd:L"";
    gUserSid=CurrentUserSid();
    gCleanerRoot=ProgramDataNlite();
    gMutex=CreateMutexW(nullptr,TRUE,L"Local\\N-Lite-Single-Instance");
    if(gMutex&&GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(gMutex);return 0;}
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
    WNDCLASSEXW pc{};pc.cbSize=sizeof(pc);pc.hInstance=inst;pc.lpfnWndProc=PopupWndProc;pc.lpszClassName=POPUP_CLASS;
    pc.hCursor=LoadCursorW(nullptr,IDC_ARROW);pc.hbrBackground=nullptr;pc.style=CS_DROPSHADOW;
    if(!RegisterClassExW(&pc))return 1;
    gIcon=static_cast<HICON>(LoadImageW(inst,MAKEINTRESOURCEW(IDI_NLITE),IMAGE_ICON,32,32,LR_DEFAULTCOLOR));
    if(!gIcon)gIcon=MakeIcon();
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=inst;wc.lpfnWndProc=WndProc;wc.lpszClassName=APP_CLASS;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=gIcon;wc.hIconSm=gIcon;
    wc.hbrBackground=nullptr;wc.style=CS_DBLCLKS;
    if(!RegisterClassExW(&wc))return 1;
    HWND h=CreateWindowExW(0,APP_CLASS,L"N Lite",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1240,830,nullptr,nullptr,inst,nullptr);
    if(!h)return 1;
    AddTray();
    if(args.find(L"--startup")!=std::wstring::npos)ShowWindow(h,SW_HIDE);
    else {ShowWindow(h,show);UpdateWindow(h);}
    MSG m;while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
    if(gMutex)CloseHandle(gMutex);
    return static_cast<int>(m.wParam);
}
