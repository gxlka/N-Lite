#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef WIN32_WINNT
#define WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <aclapi.h>
#include <taskschd.h>
#include <oleauto.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include "cleaner_policy.h"

#ifndef NLITE_CLEANER_VERSION
#define NLITE_CLEANER_VERSION "5"
#endif
#define NLITE_WIDEN2(x) L##x
#define NLITE_WIDEN(x) NLITE_WIDEN2(x)
static const wchar_t* const kHelperVersion = NLITE_WIDEN(NLITE_CLEANER_VERSION);
static const wchar_t* const kRootName = L"N-Lite";
static const wchar_t* const kTaskPrefix = L"N-Lite Cleaner ";
static const wchar_t* const kTaskDacl = L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;BU)";

using NtQuerySysFn = LONG (NTAPI*)(ULONG, PVOID, ULONG, PULONG);
using NtSetSysFn = LONG (NTAPI*)(ULONG, PVOID, ULONG);

static std::wstring Join(const std::wstring& a, const std::wstring& b) {
    return a + (a.empty() || a.back() == L'\\' ? L"" : L"\\") + b;
}

static bool IsAdmin() {
    BOOL yes = FALSE;
    PSID admins = nullptr;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &admins)) {
        CheckTokenMembership(nullptr, admins, &yes);
        FreeSid(admins);
    }
    return yes != FALSE;
}

static bool IsLocalSystem() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> buffer(bytes);
    bool system = false;
    if (bytes && GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes)) {
        auto user = reinterpret_cast<TOKEN_USER*>(buffer.data());
        system = IsWellKnownSid(user->User.Sid, WinLocalSystemSid) != FALSE;
    }
    CloseHandle(token);
    return system;
}

static std::wstring ProgramDataRoot() {
    DWORD size = GetEnvironmentVariableW(L"ProgramData", nullptr, 0);
    if (!size || size > 32767) return L"";
    std::vector<wchar_t> value(size);
    DWORD copied = GetEnvironmentVariableW(L"ProgramData", value.data(), size);
    if (!copied || copied >= size) return L"";
    return Join(value.data(), kRootName);
}

static std::wstring SettingsPath(const std::wstring& root, const std::wstring& sid) {
    return Join(root, L"settings-" + sid + L".txt");
}

static std::wstring StatusPath(const std::wstring& root, const std::wstring& sid) {
    return Join(root, L"status-" + sid + L".txt");
}

static std::wstring TaskName(const std::wstring& sid) {
    return std::wstring(kTaskPrefix) + sid;
}

static std::wstring QuoteArg(const std::wstring& value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(L'\"');
            slashes = 0;
            continue;
        }
        out.append(slashes, L'\\');
        slashes = 0;
        out.push_back(ch);
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

static bool RunSchtasks(const std::vector<std::wstring>& args) {
    wchar_t systemDir[MAX_PATH]{};
    if (!GetSystemDirectoryW(systemDir, MAX_PATH)) return false;
    std::wstring command = QuoteArg(Join(systemDir, L"schtasks.exe"));
    for (const auto& arg : args) { command.push_back(L' '); command += QuoteArg(arg); }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    if (GetStdHandle(STD_ERROR_HANDLE) && GetStdHandle(STD_ERROR_HANDLE) != INVALID_HANDLE_VALUE) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE); si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &command[0], nullptr, nullptr, (si.dwFlags & STARTF_USESTDHANDLES) != 0, CREATE_NO_WINDOW,
        nullptr, nullptr, &si, &pi)) return false;
    DWORD wait = WaitForSingleObject(pi.hProcess, 30000), code = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return wait == WAIT_OBJECT_0 && code == 0;
}

static bool ApplyDacl(HANDLE object, const std::wstring& sddl) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
        &descriptor, nullptr)) return false;
    BOOL present = FALSE, defaulted = FALSE, ownerDefaulted = FALSE;
    PACL dacl = nullptr;
    PSID owner = nullptr;
    bool ok = GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) && present && dacl &&
        GetSecurityDescriptorOwner(descriptor, &owner, &ownerDefaulted) && owner;
    if (ok) {
        DWORD result = SetSecurityInfo(object, SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            owner, nullptr, dacl, nullptr);
        ok = result == ERROR_SUCCESS;
    }
    LocalFree(descriptor);
    return ok;
}

static HANDLE OpenNoReparse(const std::wstring& path, DWORD access, DWORD share, DWORD disposition,
                            DWORD flags, const SECURITY_ATTRIBUTES* security = nullptr) {
    HANDLE file = CreateFileW(path.c_str(), access, share, const_cast<SECURITY_ATTRIBUTES*>(security),
        disposition, flags | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(file);
        SetLastError(ERROR_REPARSE_TAG_INVALID);
        return INVALID_HANDLE_VALUE;
    }
    return file;
}

static bool IsExpectedProtectedDirectory(HANDLE directory) {
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    DWORD result = GetSecurityInfo(directory, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &dacl, nullptr, &descriptor);
    if (result != ERROR_SUCCESS || !descriptor || !owner || !dacl) {
        if (descriptor) LocalFree(descriptor);
        return false;
    }
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    bool ok = GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED) &&
        (IsWellKnownSid(owner, WinBuiltinAdministratorsSid) || IsWellKnownSid(owner, WinLocalSystemSid));
    BYTE systemStorage[SECURITY_MAX_SID_SIZE]{}, adminStorage[SECURITY_MAX_SID_SIZE]{}, usersStorage[SECURITY_MAX_SID_SIZE]{};
    DWORD systemSize = sizeof(systemStorage), adminSize = sizeof(adminStorage), usersSize = sizeof(usersStorage);
    PSID systemSid = systemStorage, adminSid = adminStorage, usersSid = usersStorage;
    ok = ok && CreateWellKnownSid(WinLocalSystemSid, nullptr, systemSid, &systemSize) &&
        CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminSid, &adminSize) &&
        CreateWellKnownSid(WinBuiltinUsersSid, nullptr, usersSid, &usersSize);
    unsigned systemCount = 0, adminCount = 0, usersCount = 0;
    for (DWORD i = 0; ok && i < dacl->AceCount; ++i) {
        void* rawAce = nullptr;
        if (!GetAce(dacl, i, &rawAce) || !rawAce) { ok = false; break; }
        auto header = static_cast<ACE_HEADER*>(rawAce);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) { ok = false; break; }
        auto ace = static_cast<ACCESS_ALLOWED_ACE*>(rawAce);
        PSID trustee = reinterpret_cast<PSID>(&ace->SidStart);
        if (EqualSid(trustee, systemSid) && ace->Mask == FILE_ALL_ACCESS) ++systemCount;
        else if (EqualSid(trustee, adminSid) && ace->Mask == FILE_ALL_ACCESS) ++adminCount;
        else if (EqualSid(trustee, usersSid) && ace->Mask == (FILE_GENERIC_READ | FILE_GENERIC_EXECUTE) &&
            (header->AceFlags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) == (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) ++usersCount;
        else ok = false;
    }
    ok = ok && dacl->AceCount == 3 && systemCount == 1 && adminCount == 1 && usersCount == 1;
    LocalFree(descriptor);
    return ok;
}

static bool SecureDirectory(const std::wstring& path, const std::wstring& sddl) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
        &descriptor, nullptr)) return false;
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.lpSecurityDescriptor = descriptor;
    bool created = CreateDirectoryW(path.c_str(), &security) != FALSE;
    DWORD createError = created ? ERROR_SUCCESS : GetLastError();
    if (!created && createError != ERROR_ALREADY_EXISTS) {
        LocalFree(descriptor);
        return false;
    }
    LocalFree(descriptor);
    HANDLE directory = OpenNoReparse(path, READ_CONTROL | WRITE_DAC | WRITE_OWNER,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    if (directory == INVALID_HANDLE_VALUE) return false;
    bool ok = (created || IsExpectedProtectedDirectory(directory)) && ApplyDacl(directory, sddl);
    CloseHandle(directory);
    return ok;
}

static bool SecureFile(const std::wstring& path, const std::wstring& sddl, DWORD access,
                       const std::wstring& initialContents) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
        &descriptor, nullptr)) return false;
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.lpSecurityDescriptor = descriptor;
    HANDLE file = OpenNoReparse(path, access | GENERIC_READ | GENERIC_WRITE | READ_CONTROL | WRITE_DAC | WRITE_OWNER, FILE_SHARE_READ,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, &security);
    LocalFree(descriptor);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool ok = ApplyDacl(file, sddl);
    LARGE_INTEGER size{};
    if (ok && GetFileSizeEx(file, &size) && size.QuadPart == 0 && !initialContents.empty()) {
        DWORD written = 0;
        ok = WriteFile(file, initialContents.data(), static_cast<DWORD>(initialContents.size() * sizeof(wchar_t)),
            &written, nullptr) && written == initialContents.size() * sizeof(wchar_t);
    }
    CloseHandle(file);
    return ok;
}

static bool ReadTextFile(const std::wstring& path, std::wstring& text) {
    HANDLE file = OpenNoReparse(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart <= 32768 &&
        (size.QuadPart % sizeof(wchar_t)) == 0;
    std::vector<wchar_t> buffer(ok ? static_cast<size_t>(size.QuadPart / sizeof(wchar_t)) + 1 : 1, L'\0');
    DWORD read = 0;
    if (ok && size.QuadPart) ok = ReadFile(file, buffer.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr) &&
        read == static_cast<DWORD>(size.QuadPart);
    if (ok) text.assign(buffer.data(), read / sizeof(wchar_t));
    CloseHandle(file);
    return ok;
}

static bool WriteTextFile(const std::wstring& path, const std::wstring& text) {
    HANDLE file = OpenNoReparse(path, GENERIC_WRITE, FILE_SHARE_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER zero{};
    bool ok = SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) && SetEndOfFile(file);
    DWORD written = 0;
    const DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    if (ok) ok = WriteFile(file, text.data(), bytes, &written, nullptr) && written == bytes && FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}

static bool CurrentHelperPath(std::wstring& path) {
    std::vector<wchar_t> buffer(32768);
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) return false;
    path.assign(buffer.data(), size);
    return true;
}

static bool InstallTask(const std::wstring& helperPath, const std::wstring& sid) {
    const std::wstring task = TaskName(sid);
    const std::wstring action = QuoteArg(helperPath) + L" --run " + sid;
    if (!RunSchtasks({L"/Create", L"/F", L"/SC", L"MINUTE", L"/MO", L"1", L"/TN", task,
                      L"/TR", action, L"/RU", L"SYSTEM", L"/RL", L"HIGHEST"})) return false;

    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;
    ITaskService* service = nullptr;
    ITaskFolder* folder = nullptr;
    IRegisteredTask* registered = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITaskService, reinterpret_cast<void**>(&service));
    VARIANT empty;
    VariantInit(&empty);
    if (SUCCEEDED(hr)) hr = service->Connect(empty, empty, empty, empty);
    BSTR rootName = SysAllocString(L"\\");
    if (SUCCEEDED(hr) && rootName) hr = service->GetFolder(rootName, &folder);
    else if (SUCCEEDED(hr)) hr = E_OUTOFMEMORY;
    if (rootName) SysFreeString(rootName);
    BSTR taskName = SysAllocString(task.c_str());
    if (SUCCEEDED(hr) && taskName) hr = folder->GetTask(taskName, &registered);
    else if (SUCCEEDED(hr)) hr = E_OUTOFMEMORY;
    if (taskName) SysFreeString(taskName);
    BSTR acl = SysAllocString(kTaskDacl);
    if (SUCCEEDED(hr) && acl) hr = registered->SetSecurityDescriptor(acl, TASK_DONT_ADD_PRINCIPAL_ACE);
    if (acl) SysFreeString(acl);
    BSTR actual = nullptr;
    if (SUCCEEDED(hr)) hr = registered->GetSecurityDescriptor(
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &actual);
    if (SUCCEEDED(hr)) {
        std::wstring descriptor(actual ? actual : L"");
        if (!HasExpectedCleanerTaskSecurityDescriptor(descriptor)) hr = E_ACCESSDENIED;
    }
    if (actual) {
        if (FAILED(hr)) {
        const std::wstring value(actual); const std::string message(value.begin(), value.end()); DWORD written = 0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
        }
        SysFreeString(actual);
    }
    if (registered) registered->Release();
    if (folder) folder->Release();
    if (service) service->Release();
    if (uninitialize) CoUninitialize();
    if (FAILED(hr)) {
        const std::string message = "Cleaner task installation HRESULT " + std::to_string(static_cast<unsigned long>(hr)) + "\n";
        DWORD written = 0; WriteFile(GetStdHandle(STD_ERROR_HANDLE), message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
    }
    if (FAILED(hr)) RunSchtasks({L"/Delete", L"/F", L"/TN", task});
    return SUCCEEDED(hr);
}

static bool InstallFailure(unsigned line) {
    const std::wstring message = L"N-Lite cleaner installation failed at line " +
        std::to_wstring(line) + L", Windows error " + std::to_wstring(GetLastError()) + L"\n";
    OutputDebugStringW(message.c_str());
    const std::string ascii(message.begin(), message.end()); DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), ascii.data(), static_cast<DWORD>(ascii.size()), &written, nullptr);
    return false;
}

static bool Install(const std::wstring& sid) {
    if (!IsAdmin()) return InstallFailure(__LINE__);
    PSID sidMemory = nullptr;
    if (!IsValidCleanerSid(sid) || !ConvertStringSidToSidW(sid.c_str(), &sidMemory)) return InstallFailure(__LINE__);
    LocalFree(sidMemory);

    const std::wstring root = ProgramDataRoot();
    if (root.empty()) return InstallFailure(__LINE__);
    const std::wstring rootAcl = L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)";
    if (!SecureDirectory(root, rootAcl)) return InstallFailure(__LINE__);

    std::wstring source;
    if (!CurrentHelperPath(source)) return InstallFailure(__LINE__);
    const std::wstring installed = Join(root, L"N-Lite-Cleaner.exe");
    DWORD attrs = GetFileAttributesW(installed.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return InstallFailure(__LINE__);
    if (_wcsicmp(source.c_str(), installed.c_str()) != 0 && !CopyFileW(source.c_str(), installed.c_str(), FALSE)) return InstallFailure(__LINE__);
    const std::wstring helperAcl = L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x1200a9;;;BU)";
    HANDLE helper = OpenNoReparse(installed, READ_CONTROL | WRITE_DAC | WRITE_OWNER,
        FILE_SHARE_READ | FILE_SHARE_DELETE, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (helper == INVALID_HANDLE_VALUE) return InstallFailure(__LINE__);
    bool secured = ApplyDacl(helper, helperAcl);
    CloseHandle(helper);
    if (!secured) return InstallFailure(__LINE__);

    const std::wstring userAce = L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x12019f;;;" + sid + L")";
    const std::wstring readAce = L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x120089;;;" + sid + L")";
    const std::wstring config = SettingsPath(root, sid);
    const std::wstring status = StatusPath(root, sid);
    CleanerSettings defaults;
    CleanerStatus empty;
    empty.helperVersion = 1;
    if (!SecureFile(config, userAce, GENERIC_READ | GENERIC_WRITE,
                    SerializeCleanerSettings(defaults)) ||
        !SecureFile(status, readAce, GENERIC_READ,
                    SerializeCleanerStatus(empty))) return InstallFailure(__LINE__);
    std::wstring saved;
    CleanerSettings checkedSettings;
    if (!ReadTextFile(config, saved) || !ParseCleanerSettings(saved, checkedSettings)) {
        if (!WriteTextFile(config, SerializeCleanerSettings(defaults))) return InstallFailure(__LINE__);
    }
    CleanerStatus checkedStatus;
    if (!ReadTextFile(status, saved) || !ParseCleanerStatus(saved, checkedStatus)) {
        if (!WriteTextFile(status, SerializeCleanerStatus(empty))) return InstallFailure(__LINE__);
    }

    if (!InstallTask(installed, sid)) return InstallFailure(__LINE__);
    const std::wstring versionPath = Join(root, L"helper-version.txt");
    const std::wstring versionAcl = L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GR;;;BU)";
    if (!SecureFile(versionPath, versionAcl, GENERIC_READ, std::wstring(kHelperVersion) + L"\n") ||
        !WriteTextFile(versionPath, std::wstring(kHelperVersion) + L"\n")) {
        RunSchtasks({L"/Delete", L"/F", L"/TN", TaskName(sid)});
        return InstallFailure(__LINE__);
    }
    return true;
}

static LONG PurgeStandby() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return static_cast<LONG>(0xC0000002L);
    auto setSystem = reinterpret_cast<NtSetSysFn>(GetProcAddress(ntdll, "NtSetSystemInformation"));
    if (!setSystem) return static_cast<LONG>(0xC0000002L);
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &token))
        return static_cast<LONG>(0xC0000022L);
    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, L"SeProfileSingleProcessPrivilege", &luid)) {
        CloseHandle(token);
        return static_cast<LONG>(0xC0000061L);
    }
    TOKEN_PRIVILEGES requested{};
    requested.PrivilegeCount = 1;
    requested.Privileges[0].Luid = luid;
    requested.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    TOKEN_PRIVILEGES previous{};
    DWORD previousSize = 0;
    SetLastError(ERROR_SUCCESS);
    if (!AdjustTokenPrivileges(token, FALSE, &requested, sizeof(previous), &previous, &previousSize)) {
        CloseHandle(token);
        return static_cast<LONG>(0xC0000022L);
    }
    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        CloseHandle(token);
        return static_cast<LONG>(0xC0000061L);
    }
    ULONG command = kMemoryPurgeStandbyListCommand; // MemoryPurgeStandbyList
    LONG status = setSystem(kSystemMemoryListInformationClass, &command, sizeof(command));
    if (previous.PrivilegeCount) AdjustTokenPrivileges(token, FALSE, &previous, 0, nullptr, nullptr);
    CloseHandle(token);
    return status;
}

static bool ReadStandbyBytes(uint64_t& bytes) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto querySystem = ntdll ? reinterpret_cast<NtQuerySysFn>(GetProcAddress(ntdll, "NtQuerySystemInformation")) : nullptr;
    if (!querySystem) return false;
    SystemMemoryListInfo info{};
    const bool queried = QuerySystemMemoryListInfo(
        [&](uint32_t informationClass, void* buffer, uint32_t bufferLength, uint32_t* returnLength) {
            ULONG returned = 0;
            const LONG status = querySystem(informationClass, buffer, bufferLength, &returned);
            if (returnLength) *returnLength = returned;
            return static_cast<int32_t>(status);
        }, info);
    if (!queried) return false;
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const uint64_t pageSize = system.dwPageSize ? system.dwPageSize : 4096;
    bytes = StandbyBytesFromPageCounts(info, static_cast<uint32_t>(pageSize));
    return true;
}

static bool Run(const std::wstring& sid) {
    if (!IsLocalSystem() || !IsValidCleanerSid(sid)) return false;
    PSID sidMemory = nullptr;
    if (!ConvertStringSidToSidW(sid.c_str(), &sidMemory)) return false;
    LocalFree(sidMemory);
    const std::wstring root = ProgramDataRoot();
    if (root.empty()) return false;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, (L"Global\\N-Lite-Cleaner-" + sid).c_str());
    if (!mutex) return false;
    DWORD lock = WaitForSingleObject(mutex, 0);
    if (lock != WAIT_OBJECT_0 && lock != WAIT_ABANDONED) { CloseHandle(mutex); return true; }

    const std::wstring settingsPath = SettingsPath(root, sid);
    const std::wstring statusPath = StatusPath(root, sid);
    std::wstring settingsText, statusText;
    CleanerSettings settings;
    CleanerStatus status;
    if (!ReadTextFile(settingsPath, settingsText) || !ParseCleanerSettings(settingsText, settings)) {
        ReleaseMutex(mutex); CloseHandle(mutex); return false;
    }
    const bool statusValid = ReadTextFile(statusPath, statusText) &&
        ParseCleanerStatusOrDefault(statusText, status);
    if (!statusValid) {
        status = CleanerStatus{};
        const std::wstring statusAcl = L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x120089;;;" + sid + L")";
        const std::wstring resetStatus = SerializeCleanerStatus(status);
        if (!SecureFile(statusPath, statusAcl, GENERIC_READ, resetStatus) ||
            !WriteTextFile(statusPath, resetStatus)) {
            ReleaseMutex(mutex); CloseHandle(mutex); return false;
        }
    }

    bool changed = false;
    uint64_t standby = 0;
    bool standbyValid = ReadStandbyBytes(standby);
    status.standbyValid = standbyValid;
    status.standbyBytes = standbyValid ? standby : 0;
    status.standbyTick = GetTickCount64();
    changed = true;
    if (settings.manualRequestId > status.completedManualRequestId) {
        const uint64_t before = standby;
        const bool beforeValid = standbyValid;
        LONG result = PurgeStandby();
        uint64_t after = 0;
        const bool afterValid = ReadStandbyBytes(after);
        status.completedManualRequestId = settings.manualRequestId;
        status.lastManualStatus = static_cast<int32_t>(result);
        status.manualStandbyValid = beforeValid && afterValid;
        status.manualStandbyBefore = beforeValid ? before : 0;
        status.manualStandbyAfter = afterValid ? after : 0;
        status.standbyValid = afterValid;
        status.standbyBytes = afterValid ? after : 0;
        status.standbyTick = GetTickCount64();
        changed = true;
    } else if (settings.enabled && standbyValid) {
        const uint64_t now = GetTickCount64();
        if (standby < static_cast<uint64_t>(settings.thresholdMb) * 1024u * 1024u) {
            status.autoArmed = true;
        } else if (ShouldRunAutoClean(settings, standby, now, status.lastAutoTick, status.autoArmed)) {
            const uint64_t before = standby;
            LONG result = PurgeStandby();
            status.lastAutoTick = now;
            status.lastAutoStatus = static_cast<int32_t>(result);
            uint64_t after = 0;
            const bool afterValid = ReadStandbyBytes(after);
            status.autoStandbyValid = afterValid;
            status.autoStandbyBefore = before;
            status.autoStandbyAfter = afterValid ? after : 0;
            SYSTEM_INFO system{};
            GetSystemInfo(&system);
            const uint64_t pageSize = system.dwPageSize ? system.dwPageSize : 4096;
            status.autoArmed = AutoCleanNeedsRetry(
                static_cast<int32_t>(result), afterValid, before, after, pageSize);
            status.standbyValid = afterValid;
            status.standbyBytes = afterValid ? after : 0;
            status.standbyTick = GetTickCount64();
        }
    }
    if (changed) status.helperVersion = 1;
    bool wrote = !changed || WriteTextFile(StatusPath(root, sid), SerializeCleanerStatus(status));
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return wrote;
}

static bool Uninstall(const std::wstring& sid) {
    if (!IsAdmin() || !IsValidCleanerSid(sid)) return false;
    const std::wstring root = ProgramDataRoot();
    if (root.empty()) return false;
    const std::wstring task = TaskName(sid);
    bool ok = RunSchtasks({L"/Delete", L"/F", L"/TN", task});
    if (!ok && RunSchtasks({L"/Query", L"/TN", task})) return false;
    ok = true;
    const std::wstring settings = SettingsPath(root, sid), status = StatusPath(root, sid);
    if (GetFileAttributesW(settings.c_str()) != INVALID_FILE_ATTRIBUTES && !DeleteFileW(settings.c_str())) ok = false;
    if (GetFileAttributesW(status.c_str()) != INVALID_FILE_ATTRIBUTES && !DeleteFileW(status.c_str())) ok = false;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(Join(root, L"settings-*.txt").c_str(), &data);
    bool remaining = false;
    if (find != INVALID_HANDLE_VALUE) {
        do { if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { remaining = true; break; } }
        while (FindNextFileW(find, &data));
        FindClose(find);
    }
    if (!remaining) {
        DeleteFileW(Join(root, L"helper-version.txt").c_str());
        std::wstring installed = Join(root, L"N-Lite-Cleaner.exe");
        if (!DeleteFileW(installed.c_str())) MoveFileExW(installed.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        RemoveDirectoryW(root.c_str());
    }
    return ok;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 2;
    bool ok = false;
    if (argc == 3 && wcscmp(argv[1], L"--install") == 0) ok = Install(argv[2]);
    else if (argc == 3 && wcscmp(argv[1], L"--run") == 0) ok = Run(argv[2]);
    else if (argc == 3 && wcscmp(argv[1], L"--uninstall") == 0) ok = Uninstall(argv[2]);
    LocalFree(argv);
    return ok ? 0 : 1;
}
