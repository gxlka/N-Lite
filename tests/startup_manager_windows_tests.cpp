#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <taskschd.h>
#include <oleauto.h>

#include <algorithm>
#include <iostream>
#include <string>
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

template <typename T> void Release(T*& value) {
    if (value) { value->Release(); value = nullptr; }
}

bool CurrentSid(std::wstring& sid) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> buffer(bytes);
    bool ok = bytes && GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes);
    if (ok) {
        LPWSTR text = nullptr;
        ok = ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &text) != FALSE;
        if (ok) { sid = text; LocalFree(text); }
    }
    CloseHandle(token);
    return ok;
}

bool CreateDisabledStartupTask(const std::wstring& name, std::wstring& path) {
    std::wstring sid;
    if (!CurrentSid(sid)) return false;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;
    ITaskService* service = nullptr; ITaskFolder* root = nullptr;
    ITaskDefinition* definition = nullptr; IPrincipal* principal = nullptr;
    ITriggerCollection* triggers = nullptr; ITrigger* trigger = nullptr;
    IActionCollection* actions = nullptr; IAction* action = nullptr;
    IExecAction* exec = nullptr; IRegisteredTask* registered = nullptr;
    VARIANT empty; VariantInit(&empty);
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITaskService, reinterpret_cast<void**>(&service));
    if (SUCCEEDED(hr)) hr = service->Connect(empty, empty, empty, empty);
    BSTR rootPath = SysAllocString(L"\\");
    if (SUCCEEDED(hr) && rootPath) hr = service->GetFolder(rootPath, &root);
    else if (SUCCEEDED(hr)) hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) hr = service->NewTask(0, &definition);
    if (SUCCEEDED(hr)) hr = definition->get_Principal(&principal);
    if (SUCCEEDED(hr)) {
        BSTR user = SysAllocString(sid.c_str());
        hr = user ? principal->put_UserId(user) : E_OUTOFMEMORY;
        if (user) SysFreeString(user);
        if (SUCCEEDED(hr)) hr = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
    }
    if (SUCCEEDED(hr)) hr = definition->get_Triggers(&triggers);
    if (SUCCEEDED(hr)) hr = triggers->Create(TASK_TRIGGER_LOGON, &trigger);
    if (SUCCEEDED(hr)) hr = trigger->put_Enabled(VARIANT_FALSE);
    if (SUCCEEDED(hr)) hr = definition->get_Actions(&actions);
    if (SUCCEEDED(hr)) hr = actions->Create(TASK_ACTION_EXEC, &action);
    if (SUCCEEDED(hr)) hr = action->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&exec));
    if (SUCCEEDED(hr)) {
        BSTR executable = SysAllocString(L"C:\\Windows\\System32\\cmd.exe");
        BSTR arguments = SysAllocString(L"/c exit 0");
        hr = executable ? exec->put_Path(executable) : E_OUTOFMEMORY;
        if (SUCCEEDED(hr) && arguments) hr = exec->put_Arguments(arguments);
        if (SUCCEEDED(hr) && !arguments) hr = E_OUTOFMEMORY;
        SysFreeString(executable); SysFreeString(arguments);
    }
    BSTR taskName = SysAllocString(name.c_str());
    VARIANT user; VariantInit(&user); user.vt = VT_BSTR; user.bstrVal = SysAllocString(sid.c_str());
    VARIANT security; VariantInit(&security); security.vt = VT_BSTR;
    const std::wstring sddlText=L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;"+sid+L")";
    security.bstrVal=SysAllocString(sddlText.c_str());
    if (SUCCEEDED(hr) && taskName && user.bstrVal)
        hr = root->RegisterTaskDefinition(taskName, definition, TASK_CREATE_OR_UPDATE,
            user, empty, TASK_LOGON_INTERACTIVE_TOKEN, security, &registered);
    else if (SUCCEEDED(hr)) hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) path = L"\\" + name;
    VariantClear(&user);
    VariantClear(&security);
    if (taskName) SysFreeString(taskName);
    if (rootPath) SysFreeString(rootPath);
    Release(registered); Release(exec); Release(action); Release(actions);
    Release(trigger); Release(triggers); Release(principal); Release(definition);
    Release(root); Release(service);
    if (uninitialize) CoUninitialize();
    return SUCCEEDED(hr);
}

void RemoveTestTask(const std::wstring& path) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return;
    ITaskService* service = nullptr; ITaskFolder* folder = nullptr;
    VARIANT empty; VariantInit(&empty);
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITaskService, reinterpret_cast<void**>(&service));
    if (SUCCEEDED(hr)) hr = service->Connect(empty, empty, empty, empty);
    const size_t split = path.find_last_of(L'\\');
    const std::wstring folderName = split == 0 ? L"\\" : path.substr(0, split);
    const std::wstring name = split == std::wstring::npos ? L"" : path.substr(split + 1);
    BSTR folderPath = SysAllocString(folderName.c_str()), taskName = SysAllocString(name.c_str());
    if (SUCCEEDED(hr) && folderPath) hr = service->GetFolder(folderPath, &folder);
    if (SUCCEEDED(hr) && folder && taskName) folder->DeleteTask(taskName, 0);
    if (folderPath) SysFreeString(folderPath); if (taskName) SysFreeString(taskName);
    Release(folder); Release(service);
    if (uninitialize) CoUninitialize();
}

bool ReadTaskSecurityDescriptor(const std::wstring& path, std::wstring& descriptor) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;
    ITaskService* service = nullptr; ITaskFolder* folder = nullptr; IRegisteredTask* task = nullptr;
    VARIANT empty; VariantInit(&empty);
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITaskService, reinterpret_cast<void**>(&service));
    if (SUCCEEDED(hr)) hr = service->Connect(empty, empty, empty, empty);
    const size_t split = path.find_last_of(L'\\');
    const std::wstring folderName = split == 0 ? L"\\" : path.substr(0, split);
    const std::wstring name = split == std::wstring::npos ? L"" : path.substr(split + 1);
    BSTR folderPath = SysAllocString(folderName.c_str()), taskName = SysAllocString(name.c_str());
    if (SUCCEEDED(hr) && folderPath) hr = service->GetFolder(folderPath, &folder);
    if (SUCCEEDED(hr) && folder && taskName) hr = folder->GetTask(taskName, &task);
    BSTR value = nullptr;
    if (SUCCEEDED(hr)) hr = task->GetSecurityDescriptor(
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &value);
    if (SUCCEEDED(hr) && value) descriptor.assign(value, SysStringLen(value));
    if (value) SysFreeString(value);
    if (folderPath) SysFreeString(folderPath); if (taskName) SysFreeString(taskName);
    Release(task); Release(folder); Release(service);
    if (uninitialize) CoUninitialize();
    return SUCCEEDED(hr) && !descriptor.empty();
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

    const std::wstring taskName=L"N-Lite Startup Toggle Test "+
        std::to_wstring(GetCurrentProcessId())+L" "+std::to_wstring(GetTickCount64());
    std::wstring taskPath;
    const bool taskCreated=CreateDisabledStartupTask(taskName,taskPath);
    ok &= Check(taskCreated,"startup_task_test_creates_disabled_user_owned_task");
    if(taskCreated){
        std::wstring originalSecurity;
        ok &= Check(ReadTaskSecurityDescriptor(taskPath,originalSecurity),
            "disabled_startup_task_security_descriptor_is_readable");
        auto findTask=[&](StartupItem& found){
            const auto items=EnumerateStartupItems();
            const auto it=std::find_if(items.begin(),items.end(),[&](const StartupItem& candidate){
                return candidate.kind==StartupKind::ScheduledTask&&candidate.taskPath==taskPath;
            });
            if(it==items.end())return false;found=*it;return true;
        };
        StartupItem task;
        const bool listed=findTask(task);
        ok &= Check(listed&&!task.enabled&&task.canToggle&&task.canDelete,
            "disabled_startup_task_controls_match_its_current_user_access");
        if(listed&&task.canToggle){
            ok &= Check(SetStartupItemEnabled(task,true),"disabled_startup_task_can_be_enabled");
            StartupItem enabledTask;
            const bool enabledListed=findTask(enabledTask);
            ok &= Check(enabledListed&&enabledTask.enabled&&enabledTask.canToggle,
                "startup_task_and_boot_or_logon_trigger_enable_together");
            std::wstring enabledSecurity;
            ok &= Check(ReadTaskSecurityDescriptor(taskPath,enabledSecurity)&&enabledSecurity==originalSecurity,
                "re_enabled_startup_task_preserves_its_security_descriptor");
            if(enabledListed&&enabledTask.canDelete){
                ok &= Check(DeleteStartupItem(enabledTask),"owned_startup_task_can_be_deleted");
            }else ok &= Check(false,"owned_startup_task_can_be_deleted");
        }
        RemoveTestTask(taskPath);
    }

    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\N-Lite\\StartupTests");
    return ok ? 0 : 1;
}
