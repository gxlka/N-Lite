#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <dwmapi.h>
#include <shlwapi.h>
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

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")

static const wchar_t* APP_CLASS = L"NLiteWindow";
static const UINT WM_TRAY = WM_APP + 11;
static const UINT_PTR TIMER_REFRESH = 1;
static const int ID_PROCESSES = 1, ID_MEMORY = 2, ID_REFRESH = 10, ID_SEARCH = 11;
static const int ID_PURGE = 30, ID_AUTO = 31, ID_THRESHOLD_DOWN = 32, ID_THRESHOLD_UP = 33;
static const int ID_INTERVAL_DOWN = 34, ID_INTERVAL_UP = 35, ID_ELEVATE = 36;
static const int ID_TIMER_TOGGLE = 40, ID_TIMER_MINUS = 41, ID_TIMER_PLUS = 42, ID_AUTOSTART = 43;
static const int ID_END = 50, ID_AFFINITY = 51, ID_PRIORITY = 52, ID_GPU_HIGH = 53, ID_GPU_SAVE = 54;
static const int ID_EXIT = 9001, ID_SHOW = 9002;
static const COLORREF C_BG = RGB(13, 17, 27), C_PANEL = RGB(21, 27, 40), C_PANEL2 = RGB(26, 33, 49);
static const COLORREF C_LINE = RGB(42, 51, 69), C_TEXT = RGB(232, 237, 247), C_MUTED = RGB(144, 156, 177);
static const COLORREF C_ACCENT = RGB(126, 132, 255), C_GREEN = RGB(91, 214, 164), C_AMBER = RGB(246, 186, 89);
static const COLORREF C_RED = RGB(245, 105, 119);

struct MemListInfo {
    SIZE_T zero, free, modified, modifiedNoWrite, bad;
    SIZE_T standby[8];
    SIZE_T repurposed[8];
    SIZE_T modifiedPageFile;
};
struct ProcRow {
    DWORD pid = 0, ppid = 0;
    std::wstring name, path;
    double cpu = 0.0;
    SIZE_T working = 0, privateBytes = 0;
    bool hasChildren = false;
};
struct Metrics {
    double total = 0, available = 0, free = 0, standby = 0;
    double commit = 0, commitLimit = 0;
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
static std::vector<ProcRow> gProcs, gVisible;
static std::unordered_map<DWORD, uint64_t> gCpuPrevious;
static std::unordered_map<DWORD, bool> gExpanded;
static Metrics gMetrics;
static int gPage = 0, gScroll = 0;
static DWORD gSelectedPid = 0;
static std::wstring gSearch, gStatus = L"Ready";
static bool gSearchFocus = false, gTrayAdded = false, gExiting = false, gAutoPurge = false, gAutoStart = false;
static bool gTimerActive = false, gTimerNeed = false, gThresholdFocus = false;
static unsigned gThresholdMB = 4096, gIntervalSec = 60, gTimerMs = 1, gAppliedTimerMs = 1, gTimerMin = 1, gTimerMax = 15;
static DWORD gLastRefresh = 0;
static ULONGLONG gLastPurgeCheck = 0, gLastPurge = 0;
static bool gPurgeLatched = false;
static std::wstring gExePath;
static DWORD gPageSize = 4096;
static HANDLE gMutex = nullptr;

using NtQuerySysFn = LONG (NTAPI*)(ULONG, PVOID, ULONG, PULONG);
using NtSetSysFn = LONG (NTAPI*)(ULONG, PVOID, ULONG);
using NtQueryTimerFn = LONG (NTAPI*)(PULONG, PULONG, PULONG);
static NtQuerySysFn gNtQuerySys = nullptr;
static NtSetSysFn gNtSetSys = nullptr;
static NtQueryTimerFn gNtQueryTimer = nullptr;

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
    gNtSetSys = reinterpret_cast<NtSetSysFn>(GetProcAddress(n, "NtSetSystemInformation"));
    gNtQueryTimer = reinterpret_cast<NtQueryTimerFn>(GetProcAddress(n, "NtQueryTimerResolution"));
}
static bool ReadStandby(double& out, double& freeOut) {
    if (!gNtQuerySys) return false;
    MemListInfo info{}; ULONG ret = 0;
    LONG st = gNtQuerySys(80, &info, sizeof(info), &ret);
    if (st < 0) return false;
    SIZE_T pages = 0; for (auto v : info.standby) pages += v;
    out = static_cast<double>(pages) * gPageSize; freeOut = static_cast<double>(info.free) * gPageSize; return true;
}
static LONG PurgeStandby() {
    if (!gNtSetSys) return static_cast<LONG>(0xC0000002L);
    ULONG cmd = 4; // MemoryPurgeStandbyList
    return gNtSetSys(80, &cmd, sizeof(cmd));
}
static bool IsNtOk(LONG s) { return s >= 0; }
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
    double sb = 0, freePages = 0; if (ReadStandby(sb, freePages)) { gMetrics.standby = sb; gMetrics.free = freePages; }
}
static std::wstring ImagePath(HANDLE h) {
    wchar_t b[32768]{}; DWORD n = static_cast<DWORD>(std::size(b));
    if (QueryFullProcessImageNameW(h, 0, b, &n)) return std::wstring(b, n);
    return L"Path unavailable";
}
static void RefreshProcesses() {
    DWORD nowMs = GetTickCount();
    DWORD elapsed = gLastRefresh ? nowMs - gLastRefresh : 0;
    SYSTEM_INFO si{}; GetSystemInfo(&si); unsigned cpus = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
    std::vector<ProcRow> fresh;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W e{}; e.dwSize = sizeof(e);
        if (Process32FirstW(snap, &e)) do {
            ProcRow p; p.pid = e.th32ProcessID; p.ppid = e.th32ParentProcessID; p.name = e.szExeFile;
            DWORD rights = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ;
            HANDLE ph = OpenProcess(rights, FALSE, p.pid);
            if (ph) {
                p.path = ImagePath(ph);
                PROCESS_MEMORY_COUNTERS_EX pm{}; pm.cb = sizeof(pm);
                if (GetProcessMemoryInfo(ph, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof(pm))) {
                    p.working = pm.WorkingSetSize; p.privateBytes = pm.PrivateUsage;
                }
                FILETIME c{}, x{}, k{}, u{};
                if (GetProcessTimes(ph, &c, &x, &k, &u)) {
                    uint64_t t = FtValue(k) + FtValue(u);
                    auto it = gCpuPrevious.find(p.pid);
                    if (it != gCpuPrevious.end() && elapsed) {
                        uint64_t diff = t >= it->second ? t - it->second : 0;
                        p.cpu = 100.0 * static_cast<double>(diff) / (static_cast<double>(elapsed) * 10000.0 * cpus);
                        if (p.cpu > 100.0) p.cpu = 100.0;
                    }
                    gCpuPrevious[p.pid] = t;
                }
                CloseHandle(ph);
            }
            if (gExpanded.find(p.pid) == gExpanded.end()) gExpanded[p.pid] = true;
            fresh.push_back(std::move(p));
        } while (Process32NextW(snap, &e));
        CloseHandle(snap);
    }
    gLastRefresh = nowMs;
    gProcs.swap(fresh);

    std::unordered_set<DWORD> ids; for (auto& p : gProcs) ids.insert(p.pid);
    std::unordered_map<DWORD, std::vector<DWORD>> children;
    std::unordered_map<DWORD, size_t> index;
    for (size_t i = 0; i < gProcs.size(); ++i) index[gProcs[i].pid] = i;
    for (auto& p : gProcs) if (ids.count(p.ppid) && p.ppid != p.pid) {
        children[p.ppid].push_back(p.pid);
        gProcs[index[p.ppid]].hasChildren = true;
    }
    auto sortKids = [&](std::vector<DWORD>& v) {
        std::sort(v.begin(), v.end(), [&](DWORD a, DWORD b) {
            return _wcsicmp(gProcs[index[a]].name.c_str(), gProcs[index[b]].name.c_str()) < 0;
        });
    };
    for (auto& kv : children) sortKids(kv.second);
    std::vector<ProcRow> flat; std::unordered_set<DWORD> seen;
    std::function<void(DWORD,int)> walk = [&](DWORD id, int depth) {
        if (seen.count(id) || !index.count(id)) return;
        seen.insert(id); ProcRow p = gProcs[index[id]]; p.ppid = depth; flat.push_back(p);
        if (gExpanded[id] && children.count(id)) for (DWORD child : children[id]) walk(child, depth + 1);
    };
    std::vector<DWORD> roots;
    for (auto& p : gProcs) if (!ids.count(p.ppid) || p.ppid == p.pid) roots.push_back(p.pid);
    sortKids(roots);
    for (DWORD id : roots) walk(id, 0);
    for (auto& p : gProcs) if (!seen.count(p.pid)) walk(p.pid, 0);
    gVisible.clear();
    for (auto& p : flat) if (gSearch.empty() || StrStrIW(p.name.c_str(), gSearch.c_str()) || StrStrIW(p.path.c_str(), gSearch.c_str()))
        gVisible.push_back(p);
    int maxRows = (std::max)(1, (GetSystemMetrics(SM_CYSCREEN) - 300) / 44);
    (void)maxRows;
    gScroll = (std::max)(0, (std::min)(gScroll, static_cast<int>(gVisible.size())));
    if (!gSelectedPid || std::none_of(gProcs.begin(), gProcs.end(), [](const ProcRow& p){ return p.pid == gSelectedPid; })) {
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
static void SaveSettings() {
    RegWriteDword(L"AutoPurge", gAutoPurge ? 1 : 0); RegWriteDword(L"ThresholdMB", gThresholdMB); RegWriteDword(L"IntervalSec", gIntervalSec);
}
static void LoadSettings() {
    DWORD v = 0; RegReadDword(L"AutoPurge", v); gAutoPurge = v != 0;
    v = 4096; RegReadDword(L"ThresholdMB", v); gThresholdMB = static_cast<unsigned>((std::max)(256u, (std::min)(65536u, static_cast<unsigned>(v))));
    v = 60; RegReadDword(L"IntervalSec", v); gIntervalSec = static_cast<unsigned>((std::max)(15u, (std::min)(3600u, static_cast<unsigned>(v))));
}
static bool SetAutoStart(bool on) {
    HKEY k; const wchar_t* sub = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    bool ok = false;
    if (on) {
        std::wstring cmd = L"\"" + gExePath + L"\" --startup";
        ok = RegSetValueExW(k, L"N-Lite", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else { LONG dr = RegDeleteValueW(k, L"N-Lite"); ok = dr == ERROR_SUCCESS || dr == ERROR_FILE_NOT_FOUND; }
    RegCloseKey(k); return ok;
}
static bool ReadAutoStart() {
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, cb = 0; LONG r = RegQueryValueExW(k, L"N-Lite", nullptr, &type, nullptr, &cb); RegCloseKey(k);
    return r == ERROR_SUCCESS && type == REG_SZ;
}
static std::wstring GpuPreference(const std::wstring& path) {
    if (path.empty() || path == L"Path unavailable") return L"";
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return L"";
    wchar_t b[256]{}; DWORD cb = sizeof(b), type = 0;
    LONG r = RegQueryValueExW(k, path.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(b), &cb); RegCloseKey(k);
    if (r != ERROR_SUCCESS || type != REG_SZ) return L"";
    return b;
}
static bool SetGpuPreference(const std::wstring& path, bool high) {
    if (path.empty() || path == L"Path unavailable") return false;
    HKEY k; if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    std::wstring val = high ? L"GpuPreference=2;" : L"GpuPreference=1;";
    LONG r = RegSetValueExW(k, path.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()), static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k); return r == ERROR_SUCCESS;
}
static void SetTimerRequest(bool on) {
    if (on && !gTimerActive) {
        if (timeBeginPeriod(gTimerMs) == TIMERR_NOERROR) { gTimerActive = true; gAppliedTimerMs = gTimerMs; gTimerNeed = false; }
        else { gTimerNeed = true; gStatus = L"Windows rejected that timer resolution."; }
    } else if (!on && gTimerActive) {
        timeEndPeriod(gAppliedTimerMs); gTimerActive = false;
    }
}
static bool IsAdmin() {
    BOOL yes = FALSE; PSID sid = nullptr; SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &sid)) {
        CheckTokenMembership(nullptr, sid, &yes); FreeSid(sid);
    }
    return yes != FALSE;
}
static HICON MakeIcon() {
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, 32, 32), mask = CreateBitmap(32, 32, 1, 1, nullptr);
    HGDIOBJ old = SelectObject(dc, color); RECT r = R(0,0,32,32); Fill(dc, r, RGB(18,22,35));
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
static void RequestElevatedPurge() {
    HINSTANCE r = ShellExecuteW(gWnd, L"runas", gExePath.c_str(), L"--purge-once", nullptr, SW_HIDE);
    if (reinterpret_cast<INT_PTR>(r) > 32) gStatus = L"Elevated one-shot cleaner started. Approve the Windows prompt.";
    else gStatus = L"Elevation was cancelled or could not be started.";
}
static void DoPurge(bool allowPrompt) {
    LONG s = PurgeStandby();
    if (IsNtOk(s)) {
        gPurgeLatched = true; gLastPurge = GetTickCount64(); gStatus = L"Standby list purge requested successfully.";
    } else {
        gStatus = IsAdmin() ? L"Windows denied the standby purge request." : L"Standby purge needs administrator access.";
        if (allowPrompt && !IsAdmin()) {
            int answer = MessageBoxW(gWnd, L"Windows denied the standby purge. Run a one-shot elevated purge now?", L"N-Lite", MB_YESNO | MB_ICONQUESTION);
            if (answer == IDYES) RequestElevatedPurge();
        }
    }
}
static void OpenAffinityMenu(ProcRow* p, POINT pt) {
    if (!p) return;
    HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION, FALSE, p->pid);
    if (!ph) { gStatus = L"Could not open this process to set affinity."; return; }
    DWORD_PTR procMask = 0, sysMask = 0;
    if (!GetProcessAffinityMask(ph, &procMask, &sysMask)) { CloseHandle(ph); gStatus = L"Could not read CPU affinity."; return; }
    HMENU menu = CreatePopupMenu(); int count = static_cast<int>(sizeof(DWORD_PTR) * 8);
    for (int i = 0; i < count; ++i) {
        DWORD_PTR bit = (static_cast<DWORD_PTR>(1) << i);
        if (!(sysMask & bit)) continue;
        std::wstring label = L"CPU " + std::to_wstring(i);
        AppendMenuW(menu, MF_STRING | ((procMask & bit) ? MF_CHECKED : 0), 2000 + i, label.c_str());
    }
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, gWnd, nullptr);
    if (cmd >= 2000 && cmd < 2000 + count) {
        DWORD_PTR bit = static_cast<DWORD_PTR>(1) << (cmd - 2000);
        DWORD_PTR next = procMask ^ bit;
        if (next && SetProcessAffinityMask(ph, next)) gStatus = L"CPU affinity updated for " + p->name + L".";
        else gStatus = L"Affinity must include at least one CPU; Windows may also deny this change.";
    }
    DestroyMenu(menu); CloseHandle(ph); InvalidateRect(gWnd, nullptr, FALSE);
}
static void OpenPriorityMenu(ProcRow* p, POINT pt) {
    if (!p) return;
    HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION, FALSE, p->pid);
    if (!ph) { gStatus = L"Could not open this process to change priority."; return; }
    DWORD cur = GetPriorityClass(ph); HMENU m = CreatePopupMenu();
    struct Opt { DWORD c; const wchar_t* n; } opts[] = {
        { IDLE_PRIORITY_CLASS, L"Idle" }, { BELOW_NORMAL_PRIORITY_CLASS, L"Below normal" },
        { NORMAL_PRIORITY_CLASS, L"Normal" }, { ABOVE_NORMAL_PRIORITY_CLASS, L"Above normal" }, { HIGH_PRIORITY_CLASS, L"High" }
    };
    for (auto& o : opts) AppendMenuW(m, MF_STRING | (cur == o.c ? MF_CHECKED : 0), o.c, o.n);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, gWnd, nullptr);
    if (cmd && SetPriorityClass(ph, cmd)) gStatus = L"Priority updated for " + p->name + L".";
    else if (cmd) gStatus = L"Windows denied the priority change.";
    DestroyMenu(m); CloseHandle(ph); InvalidateRect(gWnd, nullptr, FALSE);
}
static void DrawButton(HDC dc, RECT r, const std::wstring& s, int id, COLORREF bg = C_PANEL2, COLORREF fg = C_TEXT, bool accent = false) {
    Round(dc, r, bg, accent ? C_ACCENT : C_LINE, 9);
    Txt(dc, s, r.left + 8, r.top, W(r) - 16, H(r), fg, gFontMed, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    AddHit(r, id);
}
static void DrawHeader(HDC dc, int width) {
    Txt(dc, L"N-Lite", 28, 16, 180, 39, C_TEXT, gFontTitle);
    Txt(dc, L"Lightweight system tools", 30, 52, 260, 20, C_MUTED, gFontSmall);
    RECT pill = R(width - 212, 25, 184, 30); Round(dc, pill, RGB(23,45,43), RGB(38,90,76), 15);
    Txt(dc, L"●  RUNNING IN TRAY", pill.left + 10, pill.top, pill.right-pill.left-20, pill.bottom-pill.top, C_GREEN, gFontSmall, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT p = R(26, 82, 152, 42), m = R(184, 82, 136, 42);
    Round(dc, p, gPage == 0 ? C_ACCENT : C_PANEL, gPage == 0 ? C_ACCENT : C_LINE, 10);
    Txt(dc, L"Processes", p.left+6, p.top, W(p)-12, H(p), gPage==0 ? RGB(255,255,255) : C_MUTED, gFontMed, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    Round(dc, m, gPage == 1 ? C_ACCENT : C_PANEL, gPage == 1 ? C_ACCENT : C_LINE, 10);
    Txt(dc, L"Memory", m.left+6, m.top, W(m)-12, H(m), gPage==1 ? RGB(255,255,255) : C_MUTED, gFontMed, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    AddHit(p, ID_PROCESSES); AddHit(m, ID_MEMORY);
    Line(dc, 26, 137, width - 26, 137, C_LINE);
}
static void DrawProcesses(HDC dc, int cw, int ch) {
    RECT search = R(26, 151, (cw - 68) * 62 / 100, 39);
    Round(dc, search, C_PANEL, gSearchFocus ? C_ACCENT : C_LINE, 9);
    Txt(dc, gSearch.empty() ? L"⌕  Search by process or path" : L"⌕  " + gSearch, search.left+12, search.top, W(search)-24, H(search), gSearch.empty() ? C_MUTED : C_TEXT, gFont);
    AddHit(search, ID_SEARCH);
    RECT refresh = R(search.right + 10, 151, 94, 39);
    DrawButton(dc, refresh, L"Refresh", ID_REFRESH);
    int tableX = 26, tableY = 202, tableW = (cw - 68) * 62 / 100;
    int detailX = tableX + tableW + 14, detailW = cw - detailX - 26;
    RECT table = R(tableX, tableY, tableW, ch - tableY - 24); Card(dc, table);
    RECT detail = R(detailX, tableY, detailW, ch - tableY - 24); Card(dc, detail);
    Txt(dc, L"PROCESS", tableX+16, tableY+8, tableW*42/100, 24, C_MUTED, gFontSmall);
    Txt(dc, L"PID", tableX+tableW*48/100, tableY+8, tableW*11/100, 24, C_MUTED, gFontSmall, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    Txt(dc, L"CPU", tableX+tableW*62/100, tableY+8, tableW*12/100, 24, C_MUTED, gFontSmall, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    Txt(dc, L"MEMORY", tableX+tableW*75/100, tableY+8, tableW*22/100, 24, C_MUTED, gFontSmall, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    Line(dc, tableX+12, tableY+35, tableX+tableW-12, tableY+35, C_LINE);
    const int rowH = 43, firstY = tableY + 40;
    int rows = (std::max)(0, static_cast<int>(table.bottom - firstY - 10) / rowH);
    if (gScroll >= static_cast<int>(gVisible.size())) gScroll = (std::max)(0, static_cast<int>(gVisible.size()) - rows);
    for (int i = 0; i < rows && gScroll + i < static_cast<int>(gVisible.size()); ++i) {
        const ProcRow& p = gVisible[gScroll + i]; int y = firstY + i*rowH;
        RECT rr = R(tableX+7, y, tableW-14, rowH-2);
        bool selected = p.pid == gSelectedPid;
        if (selected) Round(dc, rr, RGB(34,41,64), RGB(50,61,91), 8);
        else if (i % 2 == 1) Round(dc, rr, RGB(23,29,43), RGB(23,29,43), 8);
        AddHit(rr, 100, p.pid);
        int depth = static_cast<int>(p.ppid);
        int nameX = tableX + 16 + (std::min)(depth, 6) * 15;
        if (p.hasChildren) {
            POINT tri[3]; int tx=nameX, ty=y+15;
            if (gExpanded[p.pid]) { tri[0]={tx,ty}; tri[1]={tx+9,ty}; tri[2]={tx+4,ty+6}; }
            else { tri[0]={tx,ty}; tri[1]={tx,ty+9}; tri[2]={tx+6,ty+4}; }
            HBRUSH b=CreateSolidBrush(C_MUTED); HGDIOBJ old=SelectObject(dc,b); Polygon(dc,tri,3); SelectObject(dc,old); DeleteObject(b);
        }
        Txt(dc, p.name, nameX+13, y, tableW*42/100 - (nameX-tableX), rowH-2, selected?C_TEXT:RGB(203,212,228), gFont);
        Txt(dc, std::to_wstring(p.pid), tableX+tableW*48/100, y, tableW*11/100, rowH-2, C_MUTED, gFontSmall, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        Txt(dc, Percent(p.cpu), tableX+tableW*62/100, y, tableW*12/100, rowH-2, C_TEXT, gFontSmall, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        Txt(dc, Bytes(p.working), tableX+tableW*75/100, y, tableW*22/100, rowH-2, C_TEXT, gFontSmall, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    }
    if (gVisible.empty()) Txt(dc, L"No processes match that search.", tableX+22, firstY+20, tableW-44, 36, C_MUTED, gFont);
    int maxScroll = (std::max)(0, static_cast<int>(gVisible.size()) - rows);
    if (maxScroll > 0) {
        RECT track = R(table.right-7, firstY, 3, rows*rowH);
        Fill(dc, track, RGB(31,38,55));
        int thumbH = (std::max)(25, H(track)*rows/static_cast<int>(gVisible.size()));
        int thumbY = track.top + (H(track)-thumbH)*gScroll/maxScroll;
        Fill(dc, R(track.left,thumbY,3,thumbH), C_ACCENT);
    }
    Txt(dc, L"PROCESS DETAILS", detailX+18, tableY+14, detailW-36, 20, C_MUTED, gFontSmall);
    ProcRow* p = Selected();
    if (!p) { Txt(dc, L"Select a process to view controls.", detailX+20, tableY+54, detailW-40, 28, C_MUTED); return; }
    int dy = tableY + 47;
    Round(dc, R(detailX+18,dy,42,42), RGB(47,52,82), C_LINE, 10);
    Txt(dc, L"N", detailX+18, dy, 42, 42, C_ACCENT, gFontBold, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    Txt(dc, p->name, detailX+70, dy, detailW-90, 26, C_TEXT, gFontMed);
    Txt(dc, L"PID " + std::to_wstring(p->pid) + (p->pid < 1000 ? L"  •  SYSTEM" : L"  •  RUNNING"),
        detailX+70, dy+23, detailW-90, 20, C_MUTED, gFontSmall);
    dy += 58;
    Txt(dc, L"IMAGE PATH", detailX+18, dy, detailW-36, 18, C_MUTED, gFontSmall); dy += 20;
    RECT pathR = R(detailX+18,dy,detailW-36,45);
    Txt(dc, p->path, pathR.left, pathR.top, W(pathR), H(pathR), C_TEXT, gFontSmall, DT_LEFT|DT_TOP|DT_WORDBREAK|DT_END_ELLIPSIS);
    dy += 54;
    int half = (detailW - 48)/2;
    Round(dc,R(detailX+18,dy,half,53),C_PANEL2,C_LINE,9);
    Txt(dc,L"CPU",detailX+29,dy+5,half-20,16,C_MUTED,gFontSmall);
    Txt(dc,Percent(p->cpu),detailX+29,dy+22,half-20,24,C_TEXT,gFontMed);
    Round(dc,R(detailX+30+half,dy,half,53),C_PANEL2,C_LINE,9);
    Txt(dc,L"WORKING SET",detailX+41+half,dy+5,half-20,16,C_MUTED,gFontSmall);
    Txt(dc,Bytes(p->working),detailX+41+half,dy+22,half-20,24,C_TEXT,gFontMed);
    dy += 63;
    Txt(dc,L"CPU PRIORITY & AFFINITY",detailX+18,dy,detailW-36,20,C_MUTED,gFontSmall); dy += 24;
    int actionW = (detailW-48)/2;
    DrawButton(dc,R(detailX+18,dy,actionW,36),L"Set priority",ID_PRIORITY,C_PANEL2,C_TEXT);
    DrawButton(dc,R(detailX+30+actionW,dy,actionW,36),L"CPU affinity",ID_AFFINITY,C_PANEL2,C_TEXT);
    dy += 49;
    Txt(dc,L"WINDOWS GPU PREFERENCE",detailX+18,dy,detailW-36,20,C_MUTED,gFontSmall); dy += 24;
    int gpuW = (detailW-48)/2; std::wstring pref=GpuPreference(p->path);
    DrawButton(dc,R(detailX+18,dy,gpuW,36),L"High performance",ID_GPU_HIGH, C_PANEL2, C_TEXT, pref.find(L"=2")!=std::wstring::npos);
    DrawButton(dc,R(detailX+30+gpuW,dy,gpuW,36),L"Power saving",ID_GPU_SAVE, C_PANEL2, C_TEXT, pref.find(L"=1")!=std::wstring::npos);
    dy += 46;
    Txt(dc,L"Applies in Windows Graphics settings; relaunch the app.",detailX+18,dy,detailW-36,34,C_MUTED,gFontSmall,DT_LEFT|DT_TOP|DT_WORDBREAK);
    DrawButton(dc,R(detailX+18,detail.bottom-54,detailW-36,38),L"End task",ID_END,RGB(62,34,45),RGB(255,180,187));
}
static void DrawMetricCard(HDC dc, RECT r, const wchar_t* label, const std::wstring& value, const std::wstring& sub, COLORREF accent) {
    Card(dc,r); Round(dc,R(r.left+17,r.top+17,5,30),accent,accent,5);
    Txt(dc,label,r.left+32,r.top+13,W(r)-50,20,C_MUTED,gFontSmall);
    Txt(dc,value,r.left+32,r.top+38,W(r)-50,35,C_TEXT,gFontTitle);
    Txt(dc,sub,r.left+32,r.top+80,W(r)-50,19,C_MUTED,gFontSmall);
}
static void DrawMemory(HDC dc, int cw, int ch) {
    int gap=13, margin=26, cardW=(cw-2*margin-2*gap)/3;
    RECT a=R(margin,151,cardW,108), b=R(margin+cardW+gap,151,cardW,108), c=R(margin+2*(cardW+gap),151,cardW,108);
    double used=(std::max)(0.0,gMetrics.total-gMetrics.available);
    DrawMetricCard(dc,a,L"RAM IN USE",Bytes(used),Percent(gMetrics.total?100*used/gMetrics.total:0),C_ACCENT);
    DrawMetricCard(dc,b,L"AVAILABLE MEMORY",Bytes(gMetrics.available),L"Can be reused by Windows",C_GREEN);
    DrawMetricCard(dc,c,L"STANDBY LIST",Bytes(gMetrics.standby),L"Cached, reclaimable RAM",C_AMBER);

    int y=274, h=277, leftW=(cw-2*margin-gap)/2;
    RECT ram=R(margin,y,leftW,h), clean=R(margin+leftW+gap,y,leftW,h);
    Card(dc,ram); Txt(dc,L"Memory overview",ram.left+18,ram.top+15,leftW-36,26,C_TEXT,gFontMed);
    Txt(dc,L"Physical memory",ram.left+18,ram.top+47,leftW-36,19,C_MUTED,gFontSmall);
    int barX=ram.left+18, barY=ram.top+78, barW=leftW-36;
    Fill(dc,R(barX,barY,barW,13),RGB(37,44,61));
    if(gMetrics.total>0) {
        int wu=static_cast<int>(barW*(std::min)(1.0,used/gMetrics.total));
        int ws=static_cast<int>(barW*(std::min)(1.0,gMetrics.standby/gMetrics.total));
        int wf=(std::max)(0,barW-wu-ws);
        Fill(dc,R(barX,barY,wu,13),C_ACCENT);
        Fill(dc,R(barX+wu,barY,ws,13),C_AMBER);
        Fill(dc,R(barX+wu+ws,barY,wf,13),C_GREEN);
    }
    int rowY=ram.top+106;
    auto stat=[&](const wchar_t* label,const std::wstring& value,COLORREF dot) {
        Round(dc,R(ram.left+19,rowY+4,8,8),dot,dot,5);
        Txt(dc,label,ram.left+36,rowY,leftW/2-30,24,C_MUTED,gFontSmall);
        Txt(dc,value,ram.left+leftW/2,rowY,leftW/2-20,24,C_TEXT,gFontMed,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        rowY+=34;
    };
    double physicalFree=gMetrics.free;
    if(gMetrics.standby>gMetrics.available) physicalFree=0;
    stat(L"In use",Bytes(used),C_ACCENT);
    stat(L"Standby",Bytes(gMetrics.standby),C_AMBER);
    stat(L"Free pages",Bytes(physicalFree),C_GREEN);
    Line(dc,ram.left+18,rowY+2,ram.right-18,rowY+2,C_LINE); rowY+=11;
    stat(L"Commit / page file",Bytes(gMetrics.commit),C_ACCENT);
    Txt(dc,L"Commit limit  " + Bytes(gMetrics.commitLimit),ram.left+19,ram.bottom-31,leftW-38,18,C_MUTED,gFontSmall);

    Card(dc,clean); Txt(dc,L"Standby list cleaner",clean.left+18,clean.top+15,leftW-36,26,C_TEXT,gFontMed);
    Txt(dc,L"Purges the reclaimable Windows standby cache.",clean.left+18,clean.top+46,leftW-36,34,C_MUTED,gFontSmall,DT_LEFT|DT_TOP|DT_WORDBREAK);
    int cy=clean.top+87;
    DrawButton(dc,R(clean.left+18,cy,150,32),gAutoPurge?L"Auto purge: ON":L"Auto purge: OFF",ID_AUTO,gAutoPurge?RGB(31,68,58):C_PANEL2,gAutoPurge?C_GREEN:C_MUTED,gAutoPurge);
    Txt(dc,L"when standby reaches",clean.left+178,cy, leftW-196,32,C_MUTED,gFontSmall);
    cy+=43;
    Txt(dc,L"Threshold",clean.left+19,cy,100,28,C_MUTED,gFontSmall);
    DrawButton(dc,R(clean.left+117,cy,31,28),L"−",ID_THRESHOLD_DOWN,C_PANEL2,C_TEXT);
    Round(dc,R(clean.left+153,cy,leftW-242,28),RGB(16,21,32),C_LINE,7);
    Txt(dc,Commas(gThresholdMB)+L" MB",clean.left+157,cy,leftW-250,28,C_TEXT,gFontMed,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    DrawButton(dc,R(clean.right-70,cy,31,28),L"+",ID_THRESHOLD_UP,C_PANEL2,C_TEXT);
    cy+=38;
    Txt(dc,L"Check interval",clean.left+19,cy,112,28,C_MUTED,gFontSmall);
    DrawButton(dc,R(clean.left+132,cy,31,28),L"−",ID_INTERVAL_DOWN,C_PANEL2,C_TEXT);
    Txt(dc,std::to_wstring(gIntervalSec)+L" sec",clean.left+166,cy,leftW-253,28,C_TEXT,gFontMed,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    DrawButton(dc,R(clean.right-70,cy,31,28),L"+",ID_INTERVAL_UP,C_PANEL2,C_TEXT);
    cy+=40;
    int buttonW=(leftW-50)/2;
    DrawButton(dc,R(clean.left+18,cy,buttonW,36),L"Purge now",ID_PURGE,C_ACCENT,RGB(255,255,255),true);
    if (!IsAdmin()) DrawButton(dc,R(clean.left+30+buttonW,cy,buttonW,36),L"Elevate to purge",ID_ELEVATE,C_PANEL2,C_AMBER);
    else Txt(dc,L"Administrator access available",clean.left+30+buttonW,cy,buttonW,36,C_GREEN,gFontSmall,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    Txt(dc,gStatus,clean.left+18,clean.bottom-22,leftW-36,16,C_MUTED,gFontSmall);

    RECT timer=R(margin,566,cw-2*margin,174); Card(dc,timer);
    Txt(dc,L"Timer resolution",timer.left+18,timer.top+13,220,24,C_TEXT,gFontMed);
    Txt(dc,L"Optional system timer request. Lower values can increase power use; N-Lite releases it on exit.",
        timer.left+18,timer.top+40,W(timer)-36,20,C_MUTED,gFontSmall);
    int sy=timer.top+89, sx=timer.left+22, sw=(std::min)(W(timer)-450,500);
    Txt(dc,std::to_wstring(gTimerMin)+L" ms",sx,sy-25,58,18,C_MUTED,gFontSmall);
    Txt(dc,std::to_wstring(gTimerMax)+L" ms",sx+sw-58,sy-25,58,18,C_MUTED,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    Fill(dc,R(sx,sy,sw,5),RGB(52,60,78));
    int knobX=sx+static_cast<int>((gTimerMs-gTimerMin)/(double)(gTimerMax-gTimerMin ? gTimerMax-gTimerMin : 1)*sw);
    Round(dc,R(knobX-7,sy-6,14,17),gTimerActive?C_ACCENT:C_MUTED,gTimerActive?C_ACCENT:C_MUTED,8);
    AddHit(R(sx,sy-14,sw,34),ID_TIMER_PLUS);
    Txt(dc,std::to_wstring(gTimerMs)+L" ms requested",sx+sw+16,sy-11,168,22,C_TEXT,gFontMed);
    DrawButton(dc,R(sx+sw+194,sy-18,118,36),gTimerActive?L"Disable":L"Enable",ID_TIMER_TOGGLE,gTimerActive?RGB(31,68,58):C_PANEL2,gTimerActive?C_GREEN:C_TEXT,gTimerActive);
    DrawButton(dc,R(timer.right-188,timer.top+17,169,32),gAutoStart?L"Startup: enabled":L"Start with Windows",ID_AUTOSTART,gAutoStart?RGB(31,68,58):C_PANEL2,gAutoStart?C_GREEN:C_TEXT,gAutoStart);
    ULONG maxRes=0,minRes=0,curRes=0; std::wstring current=L"unavailable";
    if(gNtQueryTimer && IsNtOk(gNtQueryTimer(&maxRes,&minRes,&curRes))){std::wostringstream q;q<<std::fixed<<std::setprecision(2)<<(curRes/10000.0)<<L" ms";current=q.str();}
    Txt(dc,L"Current system timer: " + current + L"  •  " + gStatus,
        timer.left+18,timer.bottom-28,W(timer)-36,18,C_MUTED,gFontSmall);
    (void)ch;
}
static void Paint(HDC dc, int cw, int ch) {
    Fill(dc,R(0,0,cw,ch),C_BG); gHits.clear();
    DrawHeader(dc,cw);
    if(gPage==0) DrawProcesses(dc,cw,ch); else DrawMemory(dc,cw,ch);
}
static void SaveToggleAuto() {
    gAutoPurge=!gAutoPurge; SaveSettings(); gPurgeLatched=false; gStatus=gAutoPurge?L"Automatic standby purge enabled.":L"Automatic standby purge disabled.";
}
static void HandleClick(int x,int y,bool dbl) {
    for(auto it=gHits.rbegin();it!=gHits.rend();++it) if(Inside(it->r,x,y)) {
        int id=it->id;
        if(id==ID_PROCESSES){gPage=0;gSearchFocus=false;}
        else if(id==ID_MEMORY){gPage=1;gSearchFocus=false;}
        else if(id==ID_REFRESH){RefreshProcesses();UpdateMetrics();gStatus=L"Process list refreshed.";}
        else if(id==ID_SEARCH){gSearchFocus=true;}
        else if(id==ID_AUTO) SaveToggleAuto();
        else if(id==ID_THRESHOLD_DOWN){gThresholdMB=(std::max)(256u,gThresholdMB>512?gThresholdMB-512:256u);SaveSettings();}
        else if(id==ID_THRESHOLD_UP){gThresholdMB=(std::min)(65536u,gThresholdMB+512);SaveSettings();}
        else if(id==ID_INTERVAL_DOWN){gIntervalSec=(std::max)(15u,gIntervalSec>15?gIntervalSec-15:15u);SaveSettings();}
        else if(id==ID_INTERVAL_UP){gIntervalSec=(std::min)(3600u,gIntervalSec+15);SaveSettings(); }
        else if(id==ID_PURGE) DoPurge(true);
        else if(id==ID_ELEVATE) RequestElevatedPurge();
        else if(id==ID_AUTOSTART){bool next=!gAutoStart;if(SetAutoStart(next)){gAutoStart=next;gStatus=next?L"N-Lite will start with Windows.":L"Windows startup entry removed.";}else gStatus=L"Could not update the current-user startup entry.";}
        else if(id==ID_TIMER_TOGGLE){SetTimerRequest(!gTimerActive);gStatus=gTimerActive?L"Timer resolution request enabled.":L"Timer resolution request disabled.";}
        else if(id==ID_TIMER_PLUS){
            RECT r=it->r; int den=(std::max)(1,W(r)-14); double pos=(double)(x-r.left-7)/den; pos=(std::max)(0.0,(std::min)(1.0,pos));
            gTimerMs=gTimerMin+static_cast<unsigned>(pos*(gTimerMax-gTimerMin)+0.5); gTimerMs=(std::max)(gTimerMin,(std::min)(gTimerMax,gTimerMs));
            if(gTimerActive){SetTimerRequest(false);SetTimerRequest(true);}
            gStatus=L"Timer interval selected.";
        }
        else if(id==ID_END){
            ProcRow* p=Selected(); if(p && p->pid!=GetCurrentProcessId() && MessageBoxW(gWnd,(L"End "+p->name+L"? Unsaved work in that process can be lost.").c_str(),L"End task",MB_YESNO|MB_ICONWARNING)==IDYES){
                HANDLE ph=OpenProcess(PROCESS_TERMINATE,FALSE,p->pid);
                if(ph && TerminateProcess(ph,1))gStatus=L"End task requested for "+p->name+L".";
                else gStatus=L"Windows denied permission to end this process.";
                if(ph)CloseHandle(ph);RefreshProcesses();
            } else if(p && p->pid==GetCurrentProcessId()) gStatus=L"N-Lite cannot end itself from this button.";
        }
        else if(id==ID_AFFINITY){POINT pt;GetCursorPos(&pt);OpenAffinityMenu(Selected(),pt);}
        else if(id==ID_PRIORITY){POINT pt;GetCursorPos(&pt);OpenPriorityMenu(Selected(),pt);}
        else if(id==ID_GPU_HIGH || id==ID_GPU_SAVE){ProcRow* p=Selected();if(p && SetGpuPreference(p->path,id==ID_GPU_HIGH))gStatus=L"Windows GPU preference saved; restart the target app to apply it.";else gStatus=L"Could not save a GPU preference for this process.";}
        else if(id==100){
            gSelectedPid=it->data;
            if(dbl) {
                auto p=std::find_if(gProcs.begin(),gProcs.end(),[&](const ProcRow& a){return a.pid==gSelectedPid;});
                if(p!=gProcs.end()&&p->hasChildren)gExpanded[p->pid]=!gExpanded[p->pid];
                RefreshProcesses();
            }
        }
        InvalidateRect(gWnd,nullptr,FALSE);return;
    }
    gSearchFocus=false;
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
static LRESULT CALLBACK WndProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg){
    case WM_CREATE: {
        gWnd=h; LoadNt(); LoadSettings(); gAutoStart=ReadAutoStart();
        TIMECAPS caps{}; if(timeGetDevCaps(&caps,sizeof(caps))==TIMERR_NOERROR){gTimerMin=(std::max)(1u,static_cast<unsigned>(caps.wPeriodMin));gTimerMax=(std::min)(15u,static_cast<unsigned>(caps.wPeriodMax));if(gTimerMax<gTimerMin)gTimerMax=gTimerMin;gTimerMs=gTimerMin;}
        SYSTEM_INFO si{};GetSystemInfo(&si);gPageSize=si.dwPageSize?si.dwPageSize:4096;
        gFont=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        gFontSmall=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        gFontMed=CreateFontW(-16,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        gFontBold=CreateFontW(-22,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        gFontTitle=CreateFontW(-27,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        AddTray(); SetTimer(h,TIMER_REFRESH,2200,nullptr); UpdateMetrics(); RefreshProcesses();
        BOOL dark=TRUE; DwmSetWindowAttribute(h,20,&dark,sizeof(dark));
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize.x=960;m->ptMinTrackSize.y=790;return 0;
    }
    case WM_SIZE:
        if(wp==SIZE_MINIMIZED)HideToTray();else InvalidateRect(h,nullptr,FALSE);return 0;
    case WM_CLOSE:
        if(!gExiting){HideToTray();return 0;} DestroyWindow(h);return 0;
    case WM_TRAY:
        if(lp==WM_LBUTTONUP||lp==WM_LBUTTONDBLCLK)ShowWindowFromTray();
        else if(lp==WM_RBUTTONUP||lp==WM_CONTEXTMENU)ShowTrayMenu();
        return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==ID_SHOW)ShowWindowFromTray();
        else if(LOWORD(wp)==ID_EXIT){gExiting=true;DestroyWindow(h);}
        return 0;
    case WM_TIMER:
        if(wp==TIMER_REFRESH){
            if(IsWindowVisible(h)){
                UpdateMetrics();
                if(gPage==0)RefreshProcesses();
            } else if(gAutoPurge && GetTickCount64()-gLastPurgeCheck>5000) UpdateMetrics();
            if(gAutoPurge){
                ULONGLONG now=GetTickCount64();
                if(now-gLastPurgeCheck>=5000){gLastPurgeCheck=now; if(gMetrics.standby>=gThresholdMB*1024.0*1024.0){
                    if(!gPurgeLatched && now-gLastPurge>=static_cast<ULONGLONG>(gIntervalSec)*1000){
                        LONG st=PurgeStandby();
                        if(IsNtOk(st)){gStatus=L"Automatic standby purge requested.";gPurgeLatched=true;gLastPurge=now;}
                        else {gStatus=IsAdmin()?L"Automatic purge was denied by Windows.":L"Auto purge needs administrator access; click Elevate to purge.";gPurgeLatched=true;gLastPurge=now;}
                    }
                } else gPurgeLatched=false;}
            }
            if(IsWindowVisible(h))InvalidateRect(h,nullptr,FALSE);
        } return 0;
    case WM_LBUTTONUP: HandleClick(GET_X_LPARAM(lp),GET_Y_LPARAM(lp),false);return 0;
    case WM_LBUTTONDBLCLK: HandleClick(GET_X_LPARAM(lp),GET_Y_LPARAM(lp),true);return 0;
    case WM_MOUSEWHEEL:
        if(gPage==0){gScroll=(std::max)(0,gScroll-(GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA)*3);RefreshProcesses();InvalidateRect(h,nullptr,FALSE);}return 0;
    case WM_CHAR:
        if(gSearchFocus&&gPage==0){
            if(wp==8){if(!gSearch.empty())gSearch.pop_back();}
            else if(wp>=32&&wp<127&&gSearch.size()<80)gSearch.push_back(static_cast<wchar_t>(wp));
            gScroll=0;RefreshProcesses();InvalidateRect(h,nullptr,FALSE);return 0;
        } return 0;
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE&&gSearchFocus){gSearch.clear();gSearchFocus=false;RefreshProcesses();InvalidateRect(h,nullptr,FALSE);}
        else if(gPage==0&&(wp==VK_DOWN||wp==VK_UP)){
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
    case WM_DESTROY:
        KillTimer(h,TIMER_REFRESH);SetTimerRequest(false);RemoveTray();
        if(gIcon)DestroyIcon(gIcon);
        if(gFont)DeleteObject(gFont);if(gFontSmall)DeleteObject(gFontSmall);if(gFontMed)DeleteObject(gFontMed);if(gFontBold)DeleteObject(gFontBold);if(gFontTitle)DeleteObject(gFontTitle);
        PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE, PWSTR cmd,int show) {
    gExePath.resize(32768);DWORD n=GetModuleFileNameW(nullptr,gExePath.data(),static_cast<DWORD>(gExePath.size()));
    gExePath.resize(n);
    std::wstring args=cmd?cmd:L"";
    if(args.find(L"--purge-once")!=std::wstring::npos){LoadNt();return IsNtOk(PurgeStandby())?0:1;}
    gMutex=CreateMutexW(nullptr,TRUE,L"Local\\N-Lite-Single-Instance");
    if(gMutex&&GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(gMutex);return 0;}
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=inst;wc.lpfnWndProc=WndProc;wc.lpszClassName=APP_CLASS;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.hIconSm=wc.hIcon;
    wc.hbrBackground=nullptr;wc.style=CS_DBLCLKS;
    if(!RegisterClassExW(&wc))return 1;
    HWND h=CreateWindowExW(0,APP_CLASS,L"N-Lite",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1240,830,nullptr,nullptr,inst,nullptr);
    if(!h)return 1;
    AddTray();
    if(args.find(L"--startup")!=std::wstring::npos)ShowWindow(h,SW_HIDE);
    else {ShowWindow(h,show);UpdateWindow(h);}
    MSG m;while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
    if(gMutex)CloseHandle(gMutex);
    return static_cast<int>(m.wParam);
}
