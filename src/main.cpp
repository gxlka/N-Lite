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
#include <winhttp.h>
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

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")

#ifndef NLITE_VERSION
#define NLITE_VERSION "0.1.0"
#endif
#define NLITE_WIDEN2(x) L##x
#define NLITE_WIDEN(x) NLITE_WIDEN2(x)
static const wchar_t* APP_VERSION = NLITE_WIDEN(NLITE_VERSION);
static const wchar_t* APP_CLASS = L"NLiteWindow";
static const wchar_t* POPUP_CLASS = L"NLiteContextPopup";
static const UINT WM_UPDATE_READY = WM_APP + 12;
static const UINT WM_TRAY = WM_APP + 11;
static const UINT_PTR TIMER_REFRESH = 1, TIMER_UPDATE_CHECK = 2;
static const int ID_PROCESSES = 1, ID_MEMORY = 2, ID_STARTUP = 3, ID_SETTINGS = 4, ID_REFRESH = 10, ID_SEARCH = 11;
static const int ID_SORT_NAME = 20, ID_SORT_PID = 21, ID_SORT_CPU = 22, ID_SORT_MEMORY = 23, ID_SORT_PRIVATE = 24;
static const int ID_PURGE = 30, ID_AUTO = 31, ID_THRESHOLD_DOWN = 32, ID_THRESHOLD_UP = 33, ID_THRESHOLD_FIELD = 37;
static const int ID_INTERVAL_FIELD = 34, ID_ELEVATE = 36, ID_INTERVAL_OPTION = 38;
static const int ID_TIMER_TOGGLE = 40, ID_TIMER_MINUS = 41, ID_TIMER_PLUS = 42, ID_AUTOSTART = 43, ID_UPDATE_CHECK_NOW = 44, ID_OPEN_GITHUB = 45;
static const int ID_END = 50, ID_AFFINITY = 51, ID_PRIORITY = 52, ID_GPU_HIGH = 53, ID_GPU_SAVE = 54;
static const int ID_EXIT = 9001, ID_SHOW = 9002, ID_UPDATE = 9003;
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
    SIZE_T working = 0, privateBytes = 0, treeWorking = 0, treePrivateBytes = 0;
    bool hasChildren = false;
};
struct Metrics {
    double total = 0, available = 0, free = 0, standby = 0;
    double commit = 0, commitLimit = 0, pagefileUsed = 0, pagefileTotal = 0;
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
static int gPage = 0, gScroll = 0, gNavHover = -1, gIntervalHover = -1;
static bool gIntervalOpen = false;
static int gSortColumn = 0;
static bool gSortDescending = false;
static DWORD gSelectedPid = 0;
static std::wstring gSearch, gStatus = L"Ready";
static bool gSearchFocus = false, gTrayAdded = false, gExiting = false, gAutoPurge = false, gAutoStart = false;
static bool gThresholdFocus=false, gThresholdReplaceOnType=false, gTimerEnabled=false, gTimerActive=false, gAutoTaskReady=false, gHasProcessOverrides=false;
static std::wstring gThresholdEdit;
static DWORD gHoveredPid=0;
static std::unordered_map<std::wstring,HICON> gProcessIcons;
static std::atomic<bool> gUpdateAvailable{false};
static std::atomic<bool> gUpdateCheckSucceeded{false};
static std::atomic<bool> gUpdateCheckNoRelease{false};
static std::atomic<bool> gUpdateCheckInProgress{false};
static std::wstring gLatestVersion, gLatestUrl;
static bool gTimerNeed = false;
static unsigned gThresholdMB = 4096, gIntervalSec = 60;
static ULONG gTimerResolution=5000, gTimerApplied=5000, gTimerMinResolution=5000, gTimerMaxResolution=156250;
static DWORD gLastRefresh = 0;
static ULONGLONG gLastPurgeCheck = 0, gLastPurge = 0, gLastSeenPurgeTick = 0;
static bool gPurgeLatched = false;
static std::wstring gExePath;
static DWORD gPageSize = 4096;
static HANDLE gMutex = nullptr;

static bool ReadProcessDword(const wchar_t* kind, const std::wstring& path, DWORD& value);
static bool ReadProcessQword(const wchar_t* kind, const std::wstring& path, uint64_t& value);
static bool WriteProcessDword(const wchar_t* kind, const std::wstring& path, DWORD value);
static bool WriteProcessQword(const wchar_t* kind, const std::wstring& path, uint64_t value);
static void ApplyStoredProcessSettings(const ProcRow& p);
static bool ProcessOverridesExist();

using NtQuerySysFn = LONG (NTAPI*)(ULONG, PVOID, ULONG, PULONG);
using NtSetSysFn = LONG (NTAPI*)(ULONG, PVOID, ULONG);
using NtQueryTimerFn = LONG (NTAPI*)(PULONG, PULONG, PULONG);
using NtSetTimerFn = LONG (NTAPI*)(ULONG, BOOLEAN, PULONG);
static NtQuerySysFn gNtQuerySys = nullptr;
static NtSetSysFn gNtSetSys = nullptr;
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
    gNtSetSys = reinterpret_cast<NtSetSysFn>(GetProcAddress(n, "NtSetSystemInformation"));
    gNtQueryTimer = reinterpret_cast<NtQueryTimerFn>(GetProcAddress(n, "NtQueryTimerResolution"));
    gNtSetTimer = reinterpret_cast<NtSetTimerFn>(GetProcAddress(n, "NtSetTimerResolution"));
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
    double sb = 0, freePages = 0; if (ReadStandby(sb, freePages)) { gMetrics.standby = sb; gMetrics.free = freePages; }
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
static void RefreshProcesses() {
    DWORD nowMs = GetTickCount();
    DWORD elapsed = gLastRefresh ? nowMs - gLastRefresh : 0;
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
            if (gExpanded.find(p.pid) == gExpanded.end()) gExpanded[p.pid] = false;
            if(!p.path.empty())ApplyStoredProcessSettings(p);
            fresh.push_back(std::move(p));
        } while (Process32NextW(snap, &e));
        CloseHandle(snap);
    }
    gLastRefresh = nowMs;
    gProcs.swap(fresh);

    std::unordered_set<DWORD> ids; for (auto& p : gProcs) ids.insert(p.pid);
    for(auto it=gExpanded.begin();it!=gExpanded.end();)if(!ids.count(it->first))it=gExpanded.erase(it);else ++it;
    for(auto it=gCpuPrevious.begin();it!=gCpuPrevious.end();)if(!ids.count(it->first))it=gCpuPrevious.erase(it);else ++it;
    std::unordered_map<DWORD, std::vector<DWORD>> children;
    std::unordered_map<DWORD, size_t> index;
    std::unordered_set<DWORD> groupedChildren;
    for (size_t i = 0; i < gProcs.size(); ++i) index[gProcs[i].pid] = i;
    for (auto& p : gProcs) {
        auto parent = index.find(p.ppid);
        if (p.ppid == p.pid || parent == index.end() || p.path.empty() || p.path == L"Path unavailable")
            continue;
        ProcRow& parentRow = gProcs[parent->second];
        if (parentRow.path.empty() || parentRow.path == L"Path unavailable" ||
            _wcsicmp(p.path.c_str(), parentRow.path.c_str()) != 0)
            continue;
        children[p.ppid].push_back(p.pid);
        groupedChildren.insert(p.pid);
        parentRow.hasChildren = true;
    }
    std::unordered_map<DWORD,std::pair<SIZE_T,SIZE_T>> totals;
    std::unordered_set<DWORD> calculating;
    std::function<std::pair<SIZE_T,SIZE_T>(DWORD)> totalFor = [&](DWORD id) -> std::pair<SIZE_T,SIZE_T> {
        auto memo=totals.find(id);if(memo!=totals.end())return memo->second;
        auto ix=index.find(id);if(ix==index.end())return {0,0};
        ProcRow& row=gProcs[ix->second];
        if(calculating.count(id))return {0,0};
        calculating.insert(id);
        SIZE_T working=row.working,privateBytes=row.privateBytes;
        auto kid=children.find(id);
        if(kid!=children.end())for(DWORD child:kid->second){auto part=totalFor(child);working+=part.first;privateBytes+=part.second;}
        calculating.erase(id);
        return totals[id]={working,privateBytes};
    };
    for(auto& p:gProcs){auto total=totalFor(p.pid);p.treeWorking=total.first;p.treePrivateBytes=total.second;}
    auto compareRows = [&](const ProcRow& a,const ProcRow& b) {
        int cmp=0;
        if(gSortColumn==0)cmp=_wcsicmp(a.name.c_str(),b.name.c_str());
        else if(gSortColumn==1)cmp=a.pid<b.pid?-1:(a.pid>b.pid?1:0);
        else if(gSortColumn==2)cmp=a.cpu<b.cpu?-1:(a.cpu>b.cpu?1:0);
        else if(gSortColumn==3){SIZE_T av=a.hasChildren&&!gExpanded[a.pid]?a.treeWorking:a.working,bv=b.hasChildren&&!gExpanded[b.pid]?b.treeWorking:b.working;cmp=av<bv?-1:(av>bv?1:0);}
        else {SIZE_T av=a.hasChildren&&!gExpanded[a.pid]?a.treePrivateBytes:a.privateBytes,bv=b.hasChildren&&!gExpanded[b.pid]?b.treePrivateBytes:b.privateBytes;cmp=av<bv?-1:(av>bv?1:0);}
        if(cmp==0)cmp=_wcsicmp(a.name.c_str(),b.name.c_str());
        return gSortDescending?cmp>0:cmp<0;
    };
    auto sortKids = [&](std::vector<DWORD>& v) {
        std::sort(v.begin(), v.end(), [&](DWORD a, DWORD b) {
            return compareRows(gProcs[index[a]],gProcs[index[b]]);
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
    for (auto& p : gProcs) if (!groupedChildren.count(p.pid)) roots.push_back(p.pid);
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
static void SaveSettings() {
    RegWriteDword(L"AutoPurge",gAutoPurge?1:0); RegWriteDword(L"ThresholdMB",gThresholdMB); RegWriteDword(L"IntervalSec",gIntervalSec);
    RegWriteDword(L"TimerEnabled",gTimerEnabled?1:0); RegWriteDword(L"TimerResolution100ns",gTimerResolution);
}
static void LoadSettings() {
    DWORD v=0; RegReadDword(L"AutoPurge",v); gAutoPurge=v!=0;
    v=4096; RegReadDword(L"ThresholdMB",v); gThresholdMB=static_cast<unsigned>((std::max)(64u,(std::min)(131072u,static_cast<unsigned>(v))));
    v=60; RegReadDword(L"IntervalSec",v); gIntervalSec=static_cast<unsigned>((std::max)(60u,(std::min)(7200u,static_cast<unsigned>(v))));
    v=0; RegReadDword(L"TimerEnabled",v); gTimerEnabled=v!=0;
    v=5000; RegReadDword(L"TimerResolution100ns",v); gTimerResolution=v;
    v=0; RegReadDword(L"AutoTaskReady",v); gAutoTaskReady=v!=0;
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
static bool WriteProcessDword(const wchar_t* kind,const std::wstring& path,DWORD value) {
    if(path.empty()||path==L"Path unavailable")return false;
    HKEY k;if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\N-Lite\\ProcessSettings",0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS)return false;
    std::wstring name=ProcessSettingName(kind,path);LONG r=RegSetValueExW(k,name.c_str(),0,REG_DWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value));
    RegCloseKey(k);if(r==ERROR_SUCCESS)gHasProcessOverrides=true;return r==ERROR_SUCCESS;
}
static bool WriteProcessQword(const wchar_t* kind,const std::wstring& path,uint64_t value) {
    if(path.empty()||path==L"Path unavailable")return false;
    HKEY k;if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\N-Lite\\ProcessSettings",0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS)return false;
    std::wstring name=ProcessSettingName(kind,path);LONG r=RegSetValueExW(k,name.c_str(),0,REG_QWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value));
    RegCloseKey(k);if(r==ERROR_SUCCESS)gHasProcessOverrides=true;return r==ERROR_SUCCESS;
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
static std::wstring QuoteWindowsArg(const std::wstring& arg) {
    std::wstring out=L"\"";size_t slashes=0;
    for(wchar_t c:arg){
        if(c==L'\\'){slashes++;continue;}
        if(c==L'\"'){out.append(slashes*2+1,L'\\');out.push_back(L'\"');slashes=0;continue;}
        out.append(slashes,L'\\');slashes=0;out.push_back(c);
    }
    out.append(slashes*2,L'\\');out.push_back(L'\"');return out;
}
static bool RunSchtasks(const std::vector<std::wstring>& arguments) {
    wchar_t systemDir[MAX_PATH]{}; GetSystemDirectoryW(systemDir,MAX_PATH);
    std::wstring command=QuoteWindowsArg(std::wstring(systemDir)+L"\\schtasks.exe");
    for(const auto& arg:arguments){command.push_back(L' ');command+=QuoteWindowsArg(arg);}
    STARTUPINFOW si{}; si.cb=sizeof(si); si.dwFlags=STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi{};
    if(!CreateProcessW(nullptr,&command[0],nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return false;
    DWORD wait=WaitForSingleObject(pi.hProcess,20000), code=1;
    if(wait==WAIT_OBJECT_0)GetExitCodeProcess(pi.hProcess,&code);
    else TerminateProcess(pi.hProcess,1);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return wait==WAIT_OBJECT_0&&code==0;
}
static bool InstallAutoCleanTask() {
    std::wstring action=L"\""+gExePath+L"\" --auto-clean-check";
    std::vector<std::wstring> args={L"/Create",L"/F",L"/SC",L"MINUTE",L"/MO",L"1",L"/TN",L"N-Lite Auto Clean",L"/TR",action,L"/RL",L"HIGHEST",L"/IT"};
    bool ok=RunSchtasks(args);
    RegWriteDword(L"AutoTaskReady",ok?1:0);
    RegWriteDword(L"AutoTaskSetupResult",ok?0:1);
    if(!ok){RegWriteDword(L"AutoPurge",0);RegWriteDword(L"PurgeArmed",0);}
    return ok;
}
static void RunAutoCleanCheck() {
    LoadSettings();
    if(!gAutoPurge){RunSchtasks({L"/Change",L"/TN",L"N-Lite Auto Clean",L"/DISABLE"});return;}
    SYSTEM_INFO si{};GetSystemInfo(&si);gPageSize=si.dwPageSize?si.dwPageSize:4096;
    double standby=0,freePages=0;
    if(!ReadStandby(standby,freePages))return;
    uint64_t now=GetTickCount64(),last=RegReadQword(L"LastAutoPurgeTick",0);
    DWORD armed=1;RegReadDword(L"PurgeArmed",armed);
    if(standby<gThresholdMB*1024.0*1024.0){RegWriteDword(L"PurgeArmed",1);return;}
    if(armed && (!last || now<last || now-last>=static_cast<uint64_t>(gIntervalSec)*1000)){
        LONG status=PurgeStandby();RegWriteDword(L"LastAutoPurgeStatus",static_cast<DWORD>(status));
        RegWriteQword(L"LastAutoPurgeTick",now);
        if(IsNtOk(status))RegWriteDword(L"PurgeArmed",0);
    }
}
static void RequestAutoTaskInstall() {
    RegWriteDword(L"AutoTaskReady",0);RegWriteDword(L"PurgeArmed",1);
    HINSTANCE result=ShellExecuteW(gWnd,L"runas",gExePath.c_str(),L"--install-auto-task",nullptr,SW_HIDE);
    if(reinterpret_cast<INT_PTR>(result)>32)gStatus=L"Approve the one-time Windows prompt to enable automatic cleaning.";
    else {
        gAutoPurge=false;SaveSettings();gStatus=L"Automatic cleaning was not enabled because admin access was cancelled.";
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
static std::string JsonString(const std::string& json,const std::string& key) {
    std::string marker="\""+key+"\"";size_t p=json.find(marker);if(p==std::string::npos)return {};
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
static void CheckForUpdatesAsync() {
    if(gUpdateCheckInProgress.exchange(true))return;
    gUpdateCheckNoRelease.store(false,std::memory_order_release);
    std::thread([](){
        std::wstring latest,url;bool noRelease=false;
        HINTERNET session=WinHttpOpen(L"N-Lite",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
        if(session){
            WinHttpSetTimeouts(session,4000,4000,4000,6000);
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
                                while(WinHttpQueryDataAvailable(req,&available)&&available){
                                    size_t old=body.size();body.resize(old+available);DWORD got=0;
                                    if(!WinHttpReadData(req,&body[old],available,&got)){body.resize(old);break;}
                                    body.resize(old+got);
                                }
                                std::string tag=JsonString(body,"tag_name"),link=JsonString(body,"html_url");
                                latest.assign(tag.begin(),tag.end());url.assign(link.begin(),link.end());
                            }
                        }
                    }
                    WinHttpCloseHandle(req);
                }
                WinHttpCloseHandle(conn);
            }
            WinHttpCloseHandle(session);
        }
        if(!latest.empty()&&!url.empty()){
            gUpdateCheckSucceeded.store(true,std::memory_order_release);
            gUpdateCheckNoRelease.store(false,std::memory_order_release);
            if(VersionNewer(latest,APP_VERSION)){gLatestVersion=latest;gLatestUrl=url;gUpdateAvailable.store(true,std::memory_order_release);}
            else gUpdateAvailable.store(false,std::memory_order_release);
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
static void OpenLatestRelease() {
    if(!gUpdateAvailable.load(std::memory_order_acquire)||gLatestUrl.empty())return;
    ShellExecuteW(gWnd,L"open",gLatestUrl.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
}
static void DrawButton(HDC dc, RECT r, const std::wstring& s, int id, COLORREF bg = C_PANEL2, COLORREF fg = C_TEXT, bool accent = false) {
    Round(dc, r, bg, accent ? C_ACCENT : C_LINE, 9);
    Txt(dc, s, r.left + 8, r.top, W(r) - 16, H(r), fg, gFontMed, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    AddHit(r, id);
}
static RECT MainContent(int width) {
    int available=width-188;
    int contentWidth=(std::max)(360,(std::min)(1040,available-112));
    int left=188+(available-contentWidth)/2;
    return R(left,0,contentWidth,0);
}
static void DrawPageTitle(HDC dc,RECT content,const wchar_t* title,const wchar_t* subtitle) {
    Txt(dc,title,content.left,69,W(content),34,C_TEXT,gFontTitle);
    Txt(dc,subtitle,content.left,106,W(content),22,C_MUTED,gFont);
}
static void DrawHeader(HDC dc, int width,int height) {
    RECT sidebar=R(0,0,188,height);Fill(dc,sidebar,RGB(15,19,29));
    Line(dc,187,0,187,height,C_LINE);
    Txt(dc,L"N Lite",20,15,145,30,C_TEXT,gFontMed);
    Txt(dc,L"SYSTEM",20,63,140,18,C_MUTED,gFontSmall);
    auto nav=[&](int y,int id,const wchar_t* glyph,const wchar_t* label,int page){
        RECT r=R(12,y,164,36);bool active=gPage==page,hover=gNavHover==id;
        if(active)Round(dc,r,RGB(242,243,247),RGB(242,243,247),8);
        else if(hover)Round(dc,r,RGB(31,38,53),RGB(31,38,53),8);
        COLORREF fg=active?RGB(20,23,31):(hover?C_TEXT:C_MUTED);
        Txt(dc,glyph,r.left+12,r.top,22,H(r),active?C_ACCENT:fg,gFontMed,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        Txt(dc,label,r.left+43,r.top,W(r)-51,H(r),fg,gFontMed,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        AddHit(r,id);
    };
    nav(84,ID_MEMORY,L"◉",L"Memory",0);
    nav(126,ID_PROCESSES,L"▦",L"Processes",1);
    Line(dc,15,177,173,177,C_LINE);
    Txt(dc,L"APP",20,191,140,18,C_MUTED,gFontSmall);
    nav(211,ID_STARTUP,L"↗",L"Startup",2);
    nav(253,ID_SETTINGS,L"⚙",L"Settings",3);
    RECT top=R(188,0,width-188,48);Fill(dc,top,RGB(17,21,31));
    Line(dc,188,47,width,47,C_LINE);
    Txt(dc,L"N Lite",210,0,180,48,C_TEXT,gFontMed,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
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
    DrawPageTitle(dc,content,L"Processes",L"View running apps and processes, then adjust how they use your system.");
    RECT search=R(content.left,142,W(content)-126,38);
    Round(dc,search,C_PANEL,gSearchFocus?C_ACCENT:C_LINE,9);
    Txt(dc,gSearch.empty()?L"Search processes by name or path":L"Search  ·  "+gSearch,search.left+14,search.top, W(search)-28,H(search),gSearch.empty()?C_MUTED:C_TEXT,gFont);
    AddHit(search,ID_SEARCH);
    DrawButton(dc,R(content.right-108,142,108,38),L"Refresh",ID_REFRESH);
    RECT table=R(tableX,tableY,tableW,ch-tableY-25);Card(dc,table);
    int nameX=tableX+56,pidX=tableX+tableW*56/100,cpuX=tableX+tableW*67/100,ramX=tableX+tableW*79/100,privateX=tableX+tableW*91/100;
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
        if(selected)Round(dc,rr,RGB(38,47,73),RGB(64,78,116),8);
        else if(hover)Round(dc,rr,RGB(30,38,56),RGB(30,38,56),8);
        else if(i%2)Round(dc,rr,RGB(23,29,43),RGB(23,29,43),8);
        AddHit(rr,100,p.pid);
        int depth=static_cast<int>(p.ppid), base=tableX+17+(std::min)(depth,8)*17;
        if(p.hasChildren){
            POINT tri[3];int ty=y+15;
            if(gExpanded[p.pid]){tri[0]={base,ty};tri[1]={base+9,ty};tri[2]={base+4,ty+6};}
            else{tri[0]={base,ty};tri[1]={base,ty+9};tri[2]={base+6,ty+4};}
            HBRUSH b=CreateSolidBrush(selected?C_ACCENT:C_MUTED);HGDIOBJ old=SelectObject(dc,b);Polygon(dc,tri,3);SelectObject(dc,old);DeleteObject(b);
            AddHit(R(base-4,y+5,21,rowH-12),101,p.pid);
        }
        int ix=base+13;HICON icon=GetProcessIcon(p);
        if(icon)DrawIconEx(dc,ix,y+12,icon,18,18,0,nullptr,DI_NORMAL);
        else {Round(dc,R(ix,y+13,16,16),RGB(53,61,83),RGB(53,61,83),5);Txt(dc,L"N",ix,y+12,16,18,C_ACCENT,gFontSmall,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
        Txt(dc,p.name,ix+24,y,pidX-(ix+29),rowH-2,selected?C_TEXT:RGB(203,212,228),gFont);
        Txt(dc,std::to_wstring(p.pid),pidX,y,cpuX-pidX-12,rowH-2,C_MUTED,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        Txt(dc,Percent(p.cpu),cpuX,y,ramX-cpuX-12,rowH-2,C_TEXT,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        SIZE_T shownWorking=p.hasChildren&&!gExpanded[p.pid]?p.treeWorking:p.working;
        SIZE_T shownPrivate=p.hasChildren&&!gExpanded[p.pid]?p.treePrivateBytes:p.privateBytes;
        Txt(dc,Bytes(shownWorking),ramX,y,privateX-ramX-12,rowH-2,C_TEXT,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        Txt(dc,Bytes(shownPrivate),privateX,y,table.right-privateX-20,rowH-2,C_MUTED,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    }
    if(gVisible.empty())Txt(dc,L"No processes match that search.",tableX+22,firstY+20,tableW-44,36,C_MUTED,gFont);
    if(maxScroll>0){
        RECT track=R(table.right-7,firstY,3,rows*rowH);Fill(dc,track,RGB(31,38,55));
        int thumbH=(std::max)(24,H(track)*rows/static_cast<int>(gVisible.size()));
        int thumbY=track.top+(H(track)-thumbH)*gScroll/maxScroll;
        Round(dc,R(track.left-2,thumbY,7,thumbH),C_ACCENT,C_ACCENT,5);
    }
    Line(dc,tableX+14,table.bottom-28,table.right-14,table.bottom-28,C_LINE);
    Txt(dc,std::to_wstring(gVisible.size())+L" processes",tableX+18,table.bottom-25,150,19,C_MUTED,gFontSmall);
    Txt(dc,L"Right-click for actions  ·  Expand groups with the chevron or double-click",tableX+170,table.bottom-25,tableW-190,19,C_MUTED,gFontSmall,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
}
static void DrawSwitch(HDC dc,RECT r,bool enabled,int id) {
    Round(dc,r,enabled?C_ACCENT:RGB(49,56,71),enabled?C_ACCENT:C_LINE,H(r)/2);
    int d=H(r)-6,x=enabled?r.right-d-3:r.left+3;
    Round(dc,R(x,r.top+3,d,d),RGB(250,251,255),RGB(250,251,255),d/2);
    AddHit(r,id);
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
    RECT content=MainContent(cw);DrawPageTitle(dc,content,L"Memory",L"Standby cleanup and system timer controls.");
    int gap=10,cardY=151,cardH=76,cardW=(W(content)-gap*3)/4;
    double used=(std::max)(0.0,gMetrics.total-gMetrics.available);
    DrawMetricCard(dc,R(content.left,cardY,cardW,cardH),L"IN USE",Bytes(used),Percent(gMetrics.total?100*used/gMetrics.total:0),C_ACCENT);
    DrawMetricCard(dc,R(content.left+cardW+gap,cardY,cardW,cardH),L"AVAILABLE",Bytes(gMetrics.available),L"Free "+Bytes(gMetrics.free),C_GREEN);
    DrawMetricCard(dc,R(content.left+2*(cardW+gap),cardY,cardW,cardH),L"STANDBY",Bytes(gMetrics.standby),L"Cached and reclaimable RAM",C_AMBER);
    std::wstring pagefileValue=Bytes(gMetrics.pagefileUsed)+L" / "+Bytes(gMetrics.pagefileTotal);
    std::wstring commitSub=L"Commit "+Bytes(gMetrics.commit)+L" / "+Bytes(gMetrics.commitLimit);
    DrawMetricCard(dc,R(content.left+3*(cardW+gap),cardY,cardW,cardH),L"PAGE FILE",pagefileValue,commitSub,C_ACCENT);

    RECT clean=R(content.left,241,W(content),170);Card(dc,clean);
    Txt(dc,L"Standby cleaner",clean.left+18,clean.top+14,W(clean)-110,25,C_TEXT,gFontMed);
    Txt(dc,L"Clear cached standby pages automatically when memory reaches your threshold.",clean.left+18,clean.top+42,W(clean)-120,20,C_MUTED,gFontSmall);
    DrawSwitch(dc,R(clean.right-68,clean.top+20,48,27),gAutoPurge,ID_AUTO);

    int inside=W(clean)-36,thresholdW=inside*36/100,intervalW=inside*37/100;
    int x1=clean.left+18,x2=x1+thresholdW+14,x3=x2+intervalW+14;
    int actionW=clean.right-18-x3;
    int labelY=clean.top+72,fieldY=clean.top+93,fieldH=36;
    Txt(dc,L"Threshold",x1,labelY,thresholdW,17,C_MUTED,gFontSmall);
    Txt(dc,L"Check interval",x2,labelY,intervalW,17,C_MUTED,gFontSmall);
    Txt(dc,L"Action",x3,labelY,actionW,17,C_MUTED,gFontSmall);

    RECT field=R(x1,fieldY,thresholdW,fieldH);
    Round(dc,field,RGB(16,20,29),gThresholdFocus?C_ACCENT:C_LINE,8);
    Txt(dc,gThresholdFocus?gThresholdEdit:std::to_wstring(gThresholdMB),field.left+12,field.top,W(field)-55,H(field),C_TEXT,gFontMed,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    Line(dc,field.right-43,field.top+6,field.right-43,field.bottom-6,C_LINE);
    Txt(dc,L"MB",field.right-41,field.top,38,H(field),C_MUTED,gFontSmall,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(gThresholdFocus){int cx=field.left+12+static_cast<int>((gThresholdFocus?gThresholdEdit:std::to_wstring(gThresholdMB)).size())*9;Line(dc,cx,field.top+9,cx,field.bottom-9,C_ACCENT,2);}
    AddHit(field,ID_THRESHOLD_FIELD);

    RECT interval=R(x2,fieldY,intervalW,fieldH);
    Round(dc,interval,RGB(16,20,29),gIntervalOpen?C_ACCENT:C_LINE,8);
    Txt(dc,IntervalLabel(gIntervalSec),interval.left+13,interval.top,W(interval)-46,H(interval),C_TEXT,gFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    Txt(dc,gIntervalOpen?L"⌃":L"⌄",interval.right-32,interval.top,24,H(interval),C_MUTED,gFontMed,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    AddHit(interval,ID_INTERVAL_FIELD);

    RECT purge=R(x3,fieldY,actionW,fieldH);
    DrawButton(dc,purge,L"Clean now",ID_PURGE,C_ACCENT,RGB(255,255,255),true);
    Txt(dc,gStatus,clean.left+18,clean.bottom-22,W(clean)-36,15,C_MUTED,gFontSmall,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

    RECT timer=R(content.left,425,W(content),90);Card(dc,timer);
    Txt(dc,L"Timer resolution",timer.left+18,timer.top+11,260,24,C_TEXT,gFontMed);
    Txt(dc,L"Keep the selected system timing precision active while N-Lite runs.",timer.left+18,timer.top+37,W(timer)-240,19,C_MUTED,gFontSmall);
    int sx=timer.left+18,sy=timer.top+69,sw=(std::max)(170,W(timer)-310);
    Fill(dc,R(sx,sy-2,sw,4),RGB(57,64,81));
    double span=static_cast<double>(gTimerMaxResolution-gTimerMinResolution);
    double ratio=span?static_cast<double>(gTimerResolution-gTimerMinResolution)/span:0.0;
    int knobX=sx+static_cast<int>(ratio*sw);
    Fill(dc,R(sx,sy-2,(std::max)(0,knobX-sx),4),C_ACCENT);
    Round(dc,R(knobX-7,sy-8,14,16),gTimerEnabled?C_ACCENT:C_MUTED,gTimerEnabled?C_ACCENT:C_MUTED,8);
    AddHit(R(sx,sy-15,sw,31),ID_TIMER_PLUS);
    RECT value=R(timer.right-159,timer.top+20,86,34);
    Round(dc,value,RGB(16,20,29),C_LINE,8);
    Txt(dc,TimerText(gTimerResolution),value.left+5,value.top,W(value)-10,H(value),C_TEXT,gFontSmall,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    DrawSwitch(dc,R(timer.right-61,timer.top+24,44,26),gTimerEnabled,ID_TIMER_TOGGLE);

    if(gIntervalOpen){
        static const unsigned choices[]={60,120,300,600,900,1800,3600,7200};
        int optionH=29,popupX=interval.left,popupY=interval.bottom+5;
        RECT pop=R(popupX,popupY,intervalW,(sizeof(choices)/sizeof(choices[0]))*optionH+10);
        Round(dc,pop,RGB(22,27,39),C_LINE,10);
        for(int i=0;i<static_cast<int>(sizeof(choices)/sizeof(choices[0]));++i){
            RECT option=R(pop.left+5,pop.top+5+i*optionH,W(pop)-10,optionH);
            bool active=choices[i]==gIntervalSec;
            if(gIntervalHover==i||active)Round(dc,option,gIntervalHover==i?RGB(41,49,68):RGB(33,41,59),gIntervalHover==i?RGB(41,49,68):RGB(33,41,59),6);
            Txt(dc,IntervalLabel(choices[i]),option.left+10,option.top,W(option)-38,H(option),C_TEXT,gFontSmall,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            if(active)Txt(dc,L"✓",option.right-27,option.top,19,H(option),C_GREEN,gFontMed,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            AddHit(option,ID_INTERVAL_OPTION,choices[i]);
        }
    }
    (void)ch;
}
static void DrawProcesses(HDC dc, int cw, int ch);
static void DrawStartup(HDC dc,int cw) {
    RECT content=MainContent(cw);DrawPageTitle(dc,content,L"Startup",L"Choose whether N-Lite starts with Windows.");
    RECT card=R(content.left,151,W(content),116);Card(dc,card);
    Txt(dc,L"Start N-Lite when I sign in",card.left+20,card.top+20,W(card)-115,25,C_TEXT,gFontMed);
    Txt(dc,L"Launch quietly in the notification area and keep your saved memory and timer settings active.",card.left+20,card.top+51,W(card)-125,36,C_MUTED,gFontSmall,DT_LEFT|DT_TOP|DT_WORDBREAK);
    DrawSwitch(dc,R(card.right-71,card.top+25,48,27),gAutoStart,ID_AUTOSTART);
    RECT note=R(content.left,282,W(content),70);
    Txt(dc,gAutoStart?L"N-Lite is set to start for your Windows account.":L"N-Lite will only start when you open it.",note.left+4,note.top,W(note),24,gAutoStart?C_GREEN:C_MUTED,gFont);
}
static void DrawSettings(HDC dc,int cw) {
    RECT content=MainContent(cw);DrawPageTitle(dc,content,L"Settings",L"Application information and update preferences.");
    RECT card=R(content.left,151,W(content),158);Card(dc,card);
    Txt(dc,L"About N-Lite",card.left+20,card.top+18,W(card)-40,26,C_TEXT,gFontMed);
    Txt(dc,L"Version "+std::wstring(APP_VERSION),card.left+20,card.top+54,W(card)-40,21,C_MUTED,gFont);
    Txt(dc,L"Lightweight tools for memory and process management.",card.left+20,card.top+81,W(card)-40,21,C_MUTED,gFontSmall);
    std::wstring updateText;
    if(gUpdateCheckInProgress.load())updateText=L"Checking GitHub for updates…";
    else if(gUpdateAvailable.load())updateText=L"Version "+gLatestVersion+L" is ready to download.";
    else if(gUpdateCheckNoRelease.load())updateText=L"No GitHub release has been published yet.";
    else if(gUpdateCheckSucceeded.load())updateText=L"You are up to date.";
    else updateText=L"Update checks run at launch and every six hours. Select Check for updates to try again.";
    Txt(dc,updateText,card.left+20,card.top+111,W(card)-260,23,gUpdateAvailable.load()?C_GREEN:C_MUTED,gFontSmall);
    DrawButton(dc,R(card.right-204,card.top+48,178,38),
        gUpdateAvailable.load()?L"Download update":L"Check for updates",
        gUpdateAvailable.load()?ID_UPDATE:ID_UPDATE_CHECK_NOW,C_ACCENT,RGB(255,255,255),true);
    RECT repo=R(content.left,326,W(content),88);Card(dc,repo);
    Txt(dc,L"Project",repo.left+20,repo.top+15,W(repo)-190,23,C_TEXT,gFontMed);
    Txt(dc,L"View N-Lite source, releases and setup builds on GitHub.",repo.left+20,repo.top+43,W(repo)-190,20,C_MUTED,gFontSmall);
    DrawButton(dc,R(repo.right-181,repo.top+25,155,37),L"Open GitHub",ID_OPEN_GITHUB,C_PANEL2,C_TEXT);
}
static void Paint(HDC dc, int cw, int ch) {
    Fill(dc,R(0,0,cw,ch),C_BG);gHits.clear();
    DrawHeader(dc,cw,ch);
    if(gPage==0)DrawMemory(dc,cw,ch);
    else if(gPage==1)DrawProcesses(dc,cw,ch);
    else if(gPage==2)DrawStartup(dc,cw);
    else DrawSettings(dc,cw);
}
struct PopupState {
    HWND hwnd=nullptr;DWORD pid=0;int subKind=-1,hoverMain=-1,hoverSub=-1,scroll=0;
    int anchorX=0,anchorY=0,width=492,height=286;
};
static PopupState gPopup;
static ProcRow* FindProcess(DWORD pid) {
    auto it=std::find_if(gProcs.begin(),gProcs.end(),[&](const ProcRow& p){return p.pid==pid;});
    return it==gProcs.end()?nullptr:&*it;
}
static bool GetAffinity(DWORD pid,DWORD_PTR& procMask,DWORD_PTR& sysMask) {
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!h)return false;
    BOOL ok=GetProcessAffinityMask(h,&procMask,&sysMask);CloseHandle(h);return ok!=FALSE;
}
static void ResizePopup() {
    if(!gPopup.hwnd)return;
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    gPopup.width=(std::min)(492,W(work)-16);gPopup.height=(std::min)(286,H(work)-16);
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
static void DrawPopupItem(HDC dc,int x,int y,int w,int h,const std::wstring& label,const wchar_t* glyph,bool hover,bool arrow=false,bool checked=false,bool danger=false) {
    if(hover)Round(dc,R(x+4,y,w-8,h-2),RGB(42,51,74),RGB(42,51,74),8);
    Round(dc,R(x+12,y+7,23,23),danger?RGB(76,39,50):RGB(44,53,77),danger?RGB(105,47,60):RGB(44,53,77),7);
    Txt(dc,glyph,x+12,y+7,23,23,danger?C_RED:C_ACCENT,gFontSmall,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    Txt(dc,label,x+45,y,w-78,h-2,danger?RGB(255,184,191):C_TEXT,gFont);
    if(arrow)Txt(dc,L"›",x+w-30,y,18,h-2,C_MUTED,gFontMed,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(checked)Txt(dc,L"✓",x+w-31,y,18,h-2,C_GREEN,gFontMed,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}
static int PopupMainRow(int y){return (y-10)/38;}
static int PopupSubRow(int y){return gPopup.scroll+(y-50)/32;}
static void ClosePopup(){if(gPopup.hwnd)DestroyWindow(gPopup.hwnd);}
static void ApplyPopupCommand(int kind,int index) {
    ProcRow* p=FindProcess(gPopup.pid);if(!p){ClosePopup();return;}
    if(kind==0){
        if(index==0){
            if(p->path.empty()||p->path==L"Path unavailable"){gStatus=L"Executable path is unavailable.";}
            else {std::wstring params=L"/select,\""+p->path+L"\"";ShellExecuteW(nullptr,L"open",L"explorer.exe",params.c_str(),nullptr,SW_SHOWNORMAL);gStatus=L"Opened the process location.";}
            ClosePopup();
        }else if(index==5){
            DWORD pid=p->pid;std::wstring name=p->name;ClosePopup();
            if(pid==GetCurrentProcessId()){gStatus=L"N-Lite cannot end itself.";InvalidateRect(gWnd,nullptr,FALSE);return;}
            if(MessageBoxW(gWnd,(L"End "+name+L"? Unsaved work in that process can be lost.").c_str(),L"End task",MB_YESNO|MB_ICONWARNING)==IDYES){
                HANDLE h=OpenProcess(PROCESS_TERMINATE,FALSE,pid);
                if(h&&TerminateProcess(h,1))gStatus=L"End task requested for "+name+L".";else gStatus=L"Windows denied permission to end this process.";
                if(h)CloseHandle(h);RefreshProcesses();
            }
            InvalidateRect(gWnd,nullptr,FALSE);
        }
    }else if(kind==1){
        static const DWORD cls[]={IDLE_PRIORITY_CLASS,BELOW_NORMAL_PRIORITY_CLASS,NORMAL_PRIORITY_CLASS,ABOVE_NORMAL_PRIORITY_CLASS,HIGH_PRIORITY_CLASS};
        if(index<0||index>=5)return;
        HANDLE h=OpenProcess(PROCESS_SET_INFORMATION|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p->pid);
        bool applied=h&&SetPriorityClass(h,cls[index]);
        if(applied){
            if(WriteProcessDword(L"Priority",p->path,cls[index]))gStatus=L"Priority saved for future instances of "+p->name+L".";
            else gStatus=L"Priority changed, but N-Lite could not save it.";
        }else gStatus=L"Windows denied the priority change.";
        if(h)CloseHandle(h);ClosePopup();RefreshProcesses();InvalidateRect(gWnd,nullptr,FALSE);
    }else if(kind==2){
        DWORD_PTR procMask=0,sysMask=0;if(!GetAffinity(p->pid,procMask,sysMask))return;
        int core=0,seen=0;
        for(;core<static_cast<int>(sizeof(DWORD_PTR)*8);core++)if(sysMask&(static_cast<DWORD_PTR>(1)<<core)){if(seen++==index)break;}
        if(core>=static_cast<int>(sizeof(DWORD_PTR)*8))return;
        DWORD_PTR bit=static_cast<DWORD_PTR>(1)<<core,next=procMask^bit;
        HANDLE h=OpenProcess(PROCESS_SET_INFORMATION|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p->pid);
        if(h&&next&&SetProcessAffinityMask(h,next)){
            if(WriteProcessQword(L"Affinity",p->path,static_cast<uint64_t>(next)))gStatus=L"CPU affinity saved for future instances of "+p->name+L".";
            else gStatus=L"CPU affinity changed, but N-Lite could not save it.";
        }else gStatus=L"Keep at least one CPU selected; Windows may also deny the change.";
        if(h)CloseHandle(h);InvalidateRect(gWnd,nullptr,FALSE);if(gPopup.hwnd)InvalidateRect(gPopup.hwnd,nullptr,FALSE);
    }else if(kind==3){
        if(index==0||index==1){
            if(SetGpuPreference(p->path,index==0))gStatus=L"Windows GPU preference saved. Relaunch the process to apply it.";
            else gStatus=L"Could not save a GPU preference for this process.";
        }else if(index==2&&p->path!=L"Path unavailable"){
            HKEY k;if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\DirectX\\UserGpuPreferences",0,KEY_SET_VALUE,&k)==ERROR_SUCCESS){
                RegDeleteValueW(k,p->path.c_str());RegCloseKey(k);gStatus=L"Windows GPU preference reset to default.";
            }
        }
        ClosePopup();InvalidateRect(gWnd,nullptr,FALSE);
    }
}
static LRESULT CALLBACK PopupWndProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg){
    case WM_ERASEBKGND:return 1;
    case WM_KILLFOCUS:DestroyWindow(h);return 0;
    case WM_KEYDOWN:if(wp==VK_ESCAPE){DestroyWindow(h);return 0;}break;
    case WM_MOUSEWHEEL:
        if(gPopup.subKind==2){
            int count=0;DWORD_PTR pm=0,sm=0;if(GetAffinity(gPopup.pid,pm,sm))for(int i=0;i<static_cast<int>(sizeof(DWORD_PTR)*8);i++)if(sm&(static_cast<DWORD_PTR>(1)<<i))count++;
            int rows=(std::max)(1,(gPopup.height-60)/32),maxScroll=(std::max)(0,count-rows);
            gPopup.scroll=(std::max)(0,(std::min)(maxScroll,gPopup.scroll-(GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA)*3));
            InvalidateRect(h,nullptr,FALSE);
        }return 0;
    case WM_MOUSEMOVE:{
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp),oldKind=gPopup.subKind,oldMain=gPopup.hoverMain,oldSub=gPopup.hoverSub;
        if(x<246){
            int row=PopupMainRow(y);gPopup.hoverMain=(y>=10&&row<6)?row:-1;
            if(row==1&&gPopup.hoverMain>=0)gPopup.subKind=1;
            else if(row==2&&gPopup.hoverMain>=0)gPopup.subKind=2;
            else if(row==3&&gPopup.hoverMain>=0)gPopup.subKind=3;
            else gPopup.subKind=-1;
            if(gPopup.subKind!=oldKind){gPopup.scroll=0;gPopup.hoverSub=-1;}
        }else{
            gPopup.hoverMain=-1;
            if(gPopup.subKind>=0&&y>=50)gPopup.hoverSub=(y-50)/32;else gPopup.hoverSub=-1;
        }
        if(oldKind!=gPopup.subKind||oldMain!=gPopup.hoverMain||oldSub!=gPopup.hoverSub)InvalidateRect(h,nullptr,FALSE);
        return 0;
    }
    case WM_LBUTTONUP:{
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        if(x<246){
            int row=PopupMainRow(y);
            if(y>=10&&row==0)ApplyPopupCommand(0,0);
            else if(y>=10&&row==5)ApplyPopupCommand(0,5);
            else if(y>=10&&row>=1&&row<=3){gPopup.subKind=row;gPopup.scroll=0;gPopup.hoverSub=-1;InvalidateRect(h,nullptr,FALSE);}
        }else if(gPopup.subKind>=0&&y>=50){
            int row=PopupSubRow(y);
            if(gPopup.subKind==1)ApplyPopupCommand(1,row);
            else if(gPopup.subKind==2)ApplyPopupCommand(2,row);
            else if(gPopup.subKind==3)ApplyPopupCommand(3,row);
        }
        return 0;
    }
    case WM_PAINT:{
        PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT cr;GetClientRect(h,&cr);
        HBRUSH b=CreateSolidBrush(RGB(20,25,37));FillRect(dc,&cr,b);DeleteObject(b);
        Line(dc,246,10,246,cr.bottom-10,C_LINE);
        int mainRows=6;
        for(int i=0;i<mainRows;i++){
            int y=10+i*38;
            if(i==4){Line(dc,13,y+17,233,y+17,C_LINE);continue;}
            const wchar_t* glyph=i==0?L"↗":i==1?L"P":i==2?L"C":i==3?L"G":L"×";
            std::wstring label=i==0?L"Open file location":i==1?L"Set priority":i==2?L"CPU affinity":i==3?L"GPU preference":L"End task";
            DrawPopupItem(dc,0,y,246,37,label,glyph,gPopup.hoverMain==i,i>=1&&i<=3,gPopup.subKind==i,i==5);
        }
        ProcRow* p=FindProcess(gPopup.pid);
        if(gPopup.subKind<0){
            if(p){
                HICON icon=GetProcessIcon(*p);if(icon)DrawIconEx(dc,270,24,icon,34,34,0,nullptr,DI_NORMAL);
                Txt(dc,p->name,314,19,gPopup.width-334,25,C_TEXT,gFontMed,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                Txt(dc,L"PID  "+std::to_wstring(p->pid),314,44,gPopup.width-334,18,C_MUTED,gFontSmall);
                Line(dc,263,77,gPopup.width-14,77,C_LINE);
                Txt(dc,L"Working set",266,90,110,20,C_MUTED,gFontSmall);
                Txt(dc,Bytes(p->hasChildren&&!gExpanded[p->pid]?p->treeWorking:p->working),380,90,gPopup.width-398,20,C_TEXT,gFontMed,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
                Txt(dc,L"CPU usage",266,121,110,20,C_MUTED,gFontSmall);
                Txt(dc,Percent(p->cpu),380,121,gPopup.width-398,20,C_TEXT,gFontMed,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
                Txt(dc,L"Choose an action to see its options.",266,168,gPopup.width-286,36,C_MUTED,gFontSmall,DT_LEFT|DT_TOP|DT_WORDBREAK);
            }else Txt(dc,L"Process is no longer running.",266,24,gPopup.width-282,30,C_MUTED,gFont);
        }else{
            const wchar_t* title=gPopup.subKind==1?L"Process priority":gPopup.subKind==2?L"CPU affinity":L"GPU preference";
            Txt(dc,title,264,12,gPopup.width-278,25,C_TEXT,gFontMed);
            Txt(dc,p?p->name:L"Process",264,33,gPopup.width-278,17,C_MUTED,gFontSmall,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            Line(dc,260,47,gPopup.width-12,47,C_LINE);
            int count=0;
            if(gPopup.subKind==1)count=5;
            else if(gPopup.subKind==2){DWORD_PTR pm=0,sm=0;if(GetAffinity(gPopup.pid,pm,sm))for(int i=0;i<static_cast<int>(sizeof(DWORD_PTR)*8);i++)if(sm&(static_cast<DWORD_PTR>(1)<<i))count++;}
            else count=3;
            int rows=(std::max)(1,(gPopup.height-60)/32),begin=gPopup.subKind==2?gPopup.scroll:0,end=(std::min)(count,begin+rows);
            DWORD_PTR pm=0,sm=0;if(gPopup.subKind==2)GetAffinity(gPopup.pid,pm,sm);
            DWORD prio=0;HANDLE hp=p?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p->pid):nullptr;if(hp){prio=GetPriorityClass(hp);CloseHandle(hp);}
            std::wstring pref=p?GpuPreference(p->path):L"";
            for(int i=begin;i<end;i++){
                int y=51+(i-begin)*32;std::wstring label;const wchar_t* glyph=L"•";bool checked=false;
                if(gPopup.subKind==1){
                    static const wchar_t* names[]={L"Idle",L"Below normal",L"Normal",L"Above normal",L"High"};
                    static const DWORD classes[]={IDLE_PRIORITY_CLASS,BELOW_NORMAL_PRIORITY_CLASS,NORMAL_PRIORITY_CLASS,ABOVE_NORMAL_PRIORITY_CLASS,HIGH_PRIORITY_CLASS};
                    label=names[i];checked=prio==classes[i];glyph=L"P";
                }else if(gPopup.subKind==2){
                    int core=0,seen=0;for(;core<static_cast<int>(sizeof(DWORD_PTR)*8);core++)if(sm&(static_cast<DWORD_PTR>(1)<<core)){if(seen++==i)break;}
                    label=L"CPU "+std::to_wstring(core);checked=(pm&(static_cast<DWORD_PTR>(1)<<core))!=0;glyph=L"C";
                }else{
                    static const wchar_t* names[]={L"High performance",L"Power saving",L"Use Windows default"};
                    label=names[i];checked=(i==0&&pref.find(L"=2")!=std::wstring::npos)||(i==1&&pref.find(L"=1")!=std::wstring::npos)||(i==2&&pref.empty());glyph=L"G";
                }
                DrawPopupItem(dc,248,y,gPopup.width-248,31,label,glyph,gPopup.hoverSub==i,false,checked);
            }
        }
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
}
static void CommitThresholdEdit() {
    if(!gThresholdFocus)return;
    wchar_t* end=nullptr;unsigned long n=std::wcstoul(gThresholdEdit.c_str(),&end,10);
    if(end&&end!=gThresholdEdit.c_str()&&*end==0&&n){
        gThresholdMB=static_cast<unsigned>((std::max)(64ul,(std::min)(131072ul,n)));
        SaveSettings();gStatus=L"Standby threshold saved.";
    }else gStatus=L"Enter a threshold from 64 to 131072 MB.";
    gThresholdFocus=false;gThresholdReplaceOnType=false;gThresholdEdit.clear();
}
static void SaveToggleAuto() {
    if(gAutoPurge){gAutoPurge=false;SaveSettings();gStatus=L"Automatic standby cleaning disabled.";return;}
    gAutoPurge=true;SaveSettings();RegWriteDword(L"PurgeArmed",1);
    RequestAutoTaskInstall();
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
    else if(id==ID_STARTUP){gPage=2;gSearchFocus=false;}
    else if(id==ID_SETTINGS){gPage=3;gSearchFocus=false;}
    else if(id==ID_UPDATE)OpenLatestRelease();
    else if(id==ID_UPDATE_CHECK_NOW){CheckForUpdatesAsync();}
    else if(id==ID_OPEN_GITHUB)ShellExecuteW(gWnd,L"open",L"https://github.com/gxlka/N-Lite",nullptr,nullptr,SW_SHOWNORMAL);
    else if(id==ID_REFRESH){RefreshProcesses();UpdateMetrics();gStatus=L"Process list refreshed.";}
    else if(id==ID_SEARCH){gSearchFocus=true;gThresholdFocus=false;}
    else if(id==ID_THRESHOLD_FIELD){gThresholdFocus=true;gThresholdReplaceOnType=true;gThresholdEdit=std::to_wstring(gThresholdMB);gSearchFocus=false;}
    else if(id==ID_AUTO)SaveToggleAuto();
    else if(id==ID_INTERVAL_FIELD){gIntervalOpen=!gIntervalOpen;gIntervalHover=-1;}
    else if(id==ID_INTERVAL_OPTION){gIntervalSec=target->data;gIntervalOpen=false;gIntervalHover=-1;SaveSettings();gStatus=L"Automatic clean interval saved.";}
    else if(id==ID_PURGE)DoPurge(true);
    else if(id==ID_ELEVATE)RequestElevatedPurge();
    else if(id==ID_AUTOSTART){
        bool next=!gAutoStart;
        if(SetAutoStart(next)){gAutoStart=next;gStatus=next?L"N-Lite will start with Windows.":L"Windows startup entry removed.";}
        else gStatus=L"Could not update the current-user startup entry.";
    }
    else if(id==ID_TIMER_TOGGLE){
        gTimerEnabled=!gTimerEnabled;
        if(gTimerEnabled)SetTimerRequest(true);else SetTimerRequest(false);
        SaveSettings();
        if(gTimerEnabled&&gTimerActive)gStatus=L"Timer resolution enabled and saved.";
        else if(!gTimerEnabled)gStatus=L"Timer resolution disabled.";
    }
    else if(id==ID_TIMER_PLUS){
        RECT r=target->r;double den=(std::max)(1,W(r)-14);
        double pos=(std::max)(0.0,(std::min)(1.0,(x-r.left-7)/den));
        double desired=gTimerMinResolution+pos*static_cast<double>(gTimerMaxResolution-gTimerMinResolution);
        gTimerResolution=static_cast<ULONG>((std::max)(static_cast<double>(gTimerMinResolution),(std::min)(static_cast<double>(gTimerMaxResolution),desired)));
        gTimerResolution=(gTimerResolution/1000)*1000;
        if(gTimerEnabled){SetTimerRequest(false);SetTimerRequest(true);}
        SaveSettings();
        if(gTimerEnabled&&!gTimerActive)gStatus=L"Windows rejected the selected timer resolution.";
        else gStatus=L"Timer resolution saved.";
    }
    else if(id==ID_END){
        ProcRow* p=Selected();
        if(p){
            DWORD pid=p->pid;std::wstring name=p->name;
            if(pid==GetCurrentProcessId())gStatus=L"N-Lite cannot end itself.";
            else if(MessageBoxW(gWnd,(L"End "+name+L"? Unsaved work in that process can be lost.").c_str(),L"End task",MB_YESNO|MB_ICONWARNING)==IDYES){
                HANDLE ph=OpenProcess(PROCESS_TERMINATE,FALSE,pid);
                if(ph&&TerminateProcess(ph,1))gStatus=L"End task requested for "+name+L".";
                else gStatus=L"Windows denied permission to end this process.";
                if(ph)CloseHandle(ph);RefreshProcesses();
            }
        }
    }
    else if(id==100||id==101){
        gSelectedPid=target->data;
        if((id==101&&!dbl)||(id==100&&dbl)){
            auto p=std::find_if(gProcs.begin(),gProcs.end(),[&](const ProcRow& a){return a.pid==gSelectedPid;});
            if(p!=gProcs.end()&&p->hasChildren)gExpanded[p->pid]=!gExpanded[p->pid];
            RefreshProcesses();
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
static LRESULT CALLBACK WndProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg){
    case WM_CREATE: {
        gWnd=h; LoadNt(); LoadSettings(); gAutoStart=ReadAutoStart();gHasProcessOverrides=ProcessOverridesExist();
        SYSTEM_INFO si{};GetSystemInfo(&si);gPageSize=si.dwPageSize?si.dwPageSize:4096;
        LoadTimerRange();if(gTimerEnabled)SetTimerRequest(true);
        gFont=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        gFontSmall=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        gFontMed=CreateFontW(-16,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        gFontBold=CreateFontW(-22,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        gFontTitle=CreateFontW(-27,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        AddTray(); SetTimer(h,TIMER_REFRESH,2200,nullptr); SetTimer(h,TIMER_UPDATE_CHECK,6u*60u*60u*1000u,nullptr); UpdateMetrics(); RefreshProcesses();
        BOOL dark=TRUE;DwmSetWindowAttribute(h,19,&dark,sizeof(dark));DwmSetWindowAttribute(h,20,&dark,sizeof(dark));
        COLORREF caption=C_BG,titleText=C_TEXT,border=C_BG;DwmSetWindowAttribute(h,35,&caption,sizeof(caption));DwmSetWindowAttribute(h,36,&titleText,sizeof(titleText));DwmSetWindowAttribute(h,34,&border,sizeof(border));
        CheckForUpdatesAsync();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize.x=960;m->ptMinTrackSize.y=620;return 0;
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
        if(wp==TIMER_UPDATE_CHECK){CheckForUpdatesAsync();return 0;}
        if(wp==TIMER_REFRESH){
            if(IsWindowVisible(h))UpdateMetrics();
            if((IsWindowVisible(h)&&gPage==1)||(gHasProcessOverrides&&GetTickCount()-gLastRefresh>=5000))RefreshProcesses();
            if(gAutoPurge){
                DWORD ready=0,enabled=0;RegReadDword(L"AutoTaskReady",ready);RegReadDword(L"AutoPurge",enabled);gAutoTaskReady=ready!=0;
                if(!enabled){gAutoPurge=false;gStatus=L"Automatic cleaning setup failed or was disabled.";SaveSettings();}
                uint64_t last=RegReadQword(L"LastAutoPurgeTick",0);
                if(last&&last!=gLastSeenPurgeTick){
                    gLastSeenPurgeTick=last;DWORD result=0;RegReadDword(L"LastAutoPurgeStatus",result);
                    gStatus=static_cast<LONG>(result)>=0?L"Automatic standby purge completed.":L"Automatic purge was denied by Windows.";
                }
            }
            if(IsWindowVisible(h))InvalidateRect(h,nullptr,FALSE);
        } return 0;
    case WM_MOUSEMOVE:{
        POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};DWORD hover=0;int nav=-1,intervalHover=-1;bool hand=false;
        static const unsigned choices[]={60,120,300,600,900,1800,3600,7200};
        for(auto it=gHits.rbegin();it!=gHits.rend();++it)if(Inside(it->r,pt.x,pt.y)){
            if(it->id==ID_MEMORY||it->id==ID_PROCESSES||it->id==ID_STARTUP||it->id==ID_SETTINGS){nav=it->id;hand=true;}
            if(gPage==1&&(it->id==100||it->id==101)){hover=it->data;hand=true;}
            if(gPage==1&&it->id>=ID_SORT_NAME&&it->id<=ID_SORT_PRIVATE)hand=true;
            if(it->id==ID_INTERVAL_FIELD||it->id==ID_INTERVAL_OPTION)hand=true;
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
    case WM_LBUTTONUP:HandleClick(GET_X_LPARAM(lp),GET_Y_LPARAM(lp),false);return 0;
    case WM_LBUTTONDBLCLK:HandleClick(GET_X_LPARAM(lp),GET_Y_LPARAM(lp),true);return 0;
    case WM_MOUSEWHEEL:
        if(gPage==1){gScroll=(std::max)(0,gScroll-(GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA)*3);RefreshProcesses();InvalidateRect(h,nullptr,FALSE);}return 0;
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
    case WM_DESTROY:
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
    if(args.find(L"--purge-once")!=std::wstring::npos){LoadNt();return IsNtOk(PurgeStandby())?0:1;}
    if(args.find(L"--install-auto-task")!=std::wstring::npos)return InstallAutoCleanTask()?0:1;
    if(args.find(L"--auto-clean-check")!=std::wstring::npos){LoadNt();RunAutoCleanCheck();return 0;}
    gMutex=CreateMutexW(nullptr,TRUE,L"Local\\N-Lite-Single-Instance");
    if(gMutex&&GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(gMutex);return 0;}
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
    WNDCLASSEXW pc{};pc.cbSize=sizeof(pc);pc.hInstance=inst;pc.lpfnWndProc=PopupWndProc;pc.lpszClassName=POPUP_CLASS;
    pc.hCursor=LoadCursorW(nullptr,IDC_ARROW);pc.hbrBackground=nullptr;pc.style=CS_DROPSHADOW;
    if(!RegisterClassExW(&pc))return 1;
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=inst;wc.lpfnWndProc=WndProc;wc.lpszClassName=APP_CLASS;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.hIconSm=wc.hIcon;
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
