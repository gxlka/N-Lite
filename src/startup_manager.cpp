#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <taskschd.h>
#include <oleauto.h>
#include <sddl.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#include "startup_manager.h"
#include "startup_policy.h"

#ifdef NLITE_STARTUP_TESTING
namespace {
StartupFilePickerForTesting gStartupFilePickerForTesting = nullptr;
}

void SetStartupFilePickerForTesting(StartupFilePickerForTesting picker) {
    gStartupFilePickerForTesting = picker;
}
#endif

namespace {
const wchar_t* RunSubkey(StartupKind kind) {
    return kind==StartupKind::UserRun||kind==StartupKind::MachineRun?
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run":
        L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
}
bool IsUserRegistryStartup(StartupKind kind) {
    return kind==StartupKind::UserRun||kind==StartupKind::UserRunOnce;
}
std::wstring SourceName(StartupKind kind,DWORD view) {
    if(kind==StartupKind::UserRun)return view==KEY_WOW64_32KEY?L"Current user / Run (32-bit)":L"Current user / Run (64-bit)";
    if(kind==StartupKind::UserRunOnce)return view==KEY_WOW64_32KEY?L"Current user / Run once (32-bit)":L"Current user / Run once (64-bit)";
    if(kind==StartupKind::MachineRun)return view==KEY_WOW64_32KEY?L"All users / Run (32-bit)":L"All users / Run (64-bit)";
    if(kind==StartupKind::MachineRunOnce)return view==KEY_WOW64_32KEY?L"All users / Run once (32-bit)":L"All users / Run once (64-bit)";
    return kind==StartupKind::UserFolder?L"Current user / Startup folder":L"All users / Startup folder";
}
std::wstring BackupRoot(StartupKind kind,DWORD view) {
    return std::wstring(L"Software\\N-Lite\\DisabledStartup\\")+
        (kind==StartupKind::UserRun?L"Run":L"RunOnce")+(view==KEY_WOW64_32KEY?L"32":L"64");
}
std::wstring BackupPath(const StartupItem& item) { return BackupRoot(item.kind,item.registryView)+L"\\"+item.name; }
std::wstring RawString(const std::vector<BYTE>& data) {
    if(data.size()<sizeof(wchar_t)||data.size()%sizeof(wchar_t))return L"(invalid startup command)";
    std::wstring value(reinterpret_cast<const wchar_t*>(data.data()),data.size()/sizeof(wchar_t));
    while(!value.empty()&&value.back()==L'\0')value.pop_back();
    return value;
}
std::wstring QuoteArgument(const std::wstring& arg) {
    std::wstring out=L"\"";size_t slashes=0;
    for(wchar_t c:arg){
        if(c==L'\\'){++slashes;continue;}
        if(c==L'\"'){out.append(slashes*2+1,L'\\');out.push_back(L'\"');slashes=0;continue;}
        out.append(slashes,L'\\');slashes=0;out.push_back(c);
    }
    out.append(slashes*2,L'\\');out.push_back(L'\"');return out;
}
bool ReadValue(HKEY root,const std::wstring& subkey,DWORD view,const std::wstring& name,
    DWORD& type,std::vector<BYTE>& data) {
    HKEY key=nullptr;if(RegOpenKeyExW(root,subkey.c_str(),0,KEY_QUERY_VALUE|view,&key)!=ERROR_SUCCESS)return false;
    DWORD size=0;LONG result=RegQueryValueExW(key,name.c_str(),nullptr,&type,nullptr,&size);
    if(result==ERROR_SUCCESS){data.resize(size);result=RegQueryValueExW(key,name.c_str(),nullptr,&type,data.empty()?nullptr:data.data(),&size);data.resize(size);}
    RegCloseKey(key);return result==ERROR_SUCCESS;
}

std::wstring ApprovalSubkey(StartupKind kind, DWORD view) {
    const wchar_t* category = kind==StartupKind::UserFolder||kind==StartupKind::CommonFolder ?
        L"StartupFolder" : (view==KEY_WOW64_32KEY ? L"Run32" : L"Run");
    return std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\")+category;
}
StartupApprovalState ReadApproval(HKEY root,const std::wstring& subkey,DWORD view,const std::wstring& name) {
    DWORD type=0;std::vector<BYTE> bytes;
    if(!ReadValue(root,subkey,view,name,type,bytes)||type!=REG_BINARY)return StartupApprovalState::Unknown;
    std::vector<uint8_t> data(bytes.begin(),bytes.end());
    return ParseStartupApprovalState(data);
}
bool WriteApproval(const StartupItem& item,bool enabled) {
    if(item.approvalSubkey.empty()||item.approvalName.empty())return false;
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,item.approvalSubkey.c_str(),0,nullptr,0,
        KEY_QUERY_VALUE|KEY_SET_VALUE|item.registryView,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    DWORD type=0,size=0;LONG result=RegQueryValueExW(key,item.approvalName.c_str(),nullptr,&type,nullptr,&size);
    std::vector<uint8_t> prior;
    if(result==ERROR_SUCCESS&&type==REG_BINARY&&size){
        prior.resize(size);result=RegQueryValueExW(key,item.approvalName.c_str(),nullptr,&type,
            reinterpret_cast<BYTE*>(prior.data()),&size);
        if(result==ERROR_SUCCESS)prior.resize(size);else prior.clear();
    }
    if(result!=ERROR_SUCCESS&&result!=ERROR_FILE_NOT_FOUND){RegCloseKey(key);return false;}
    const std::vector<uint8_t> bytes=SetStartupApprovalState(prior,enabled);
    result=RegSetValueExW(key,item.approvalName.c_str(),0,REG_BINARY,
        reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()));
    RegCloseKey(key);return result==ERROR_SUCCESS;
}

void AddRunItems(std::vector<StartupItem>& items,HKEY root,StartupKind kind,DWORD view) {
    HKEY key=nullptr;const std::wstring subkey=RunSubkey(kind);
    if(RegOpenKeyExW(root,subkey.c_str(),0,KEY_QUERY_VALUE|view,&key)!=ERROR_SUCCESS)return;
    DWORD count=0,maxName=0,maxData=0;
    if(RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&count,&maxName,&maxData,nullptr,nullptr)!=ERROR_SUCCESS){RegCloseKey(key);return;}
    std::vector<wchar_t> name(static_cast<size_t>(maxName)+2);
    std::vector<BYTE> data(static_cast<size_t>(maxData)+2);
    for(DWORD index=0;index<count;++index){
        DWORD nameSize=static_cast<DWORD>(name.size()-1),dataSize=static_cast<DWORD>(data.size()),type=0;
        LONG result=RegEnumValueW(key,index,name.data(),&nameSize,nullptr,&type,data.empty()?nullptr:data.data(),&dataSize);
        if(result!=ERROR_SUCCESS||(type!=REG_SZ&&type!=REG_EXPAND_SZ)||!nameSize)continue;
        StartupItem item;item.name.assign(name.data(),nameSize);item.rawData.assign(data.begin(),data.begin()+dataSize);
        item.command=RawString(item.rawData);item.source=SourceName(kind,view);item.kind=kind;
        item.registryView=view;item.valueType=type;
        item.canToggle=IsUserRegistryStartup(kind)&&item.name.find_first_of(L"\\/")==std::wstring::npos;
        item.canDelete=IsUserRegistryStartup(kind);
        if(kind==StartupKind::UserRun||kind==StartupKind::MachineRun){
            item.approvalSubkey=ApprovalSubkey(kind,view);item.approvalName=item.name;
            item.approvalManaged=kind==StartupKind::UserRun&&item.canToggle;
            const StartupApprovalState state=ReadApproval(root,item.approvalSubkey,view,item.name);
            item.enabled=StartupSourceEnabled(true,state);
        }
        items.push_back(std::move(item));
    }
    RegCloseKey(key);
}
void AddPolicyRunItems(std::vector<StartupItem>& items,HKEY root,const wchar_t* subkey,
                       const wchar_t* source,DWORD view,bool currentUser) {
    HKEY key=nullptr;
    if(RegOpenKeyExW(root,subkey,0,KEY_QUERY_VALUE|view,&key)!=ERROR_SUCCESS)return;
    DWORD count=0,maxName=0,maxData=0;
    if(RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&count,&maxName,&maxData,nullptr,nullptr)!=ERROR_SUCCESS){RegCloseKey(key);return;}
    std::vector<wchar_t> name(static_cast<size_t>(maxName)+2);
    std::vector<BYTE> data(static_cast<size_t>(maxData)+2);
    for(DWORD index=0;index<count;++index){
        DWORD nameSize=static_cast<DWORD>(name.size()-1),dataSize=static_cast<DWORD>(data.size()),type=0;
        const LONG result=RegEnumValueW(key,index,name.data(),&nameSize,nullptr,&type,data.data(),&dataSize);
        if(result!=ERROR_SUCCESS||(type!=REG_SZ&&type!=REG_EXPAND_SZ)||!nameSize)continue;
        StartupItem item;item.name.assign(name.data(),nameSize);item.rawData.assign(data.begin(),data.begin()+dataSize);
        item.command=RawString(item.rawData);item.kind=currentUser?StartupKind::UserRun:StartupKind::MachineRun;
        item.source=std::wstring(source)+(view==KEY_WOW64_32KEY?L" (32-bit)":L" (64-bit)");
        item.registryView=view;item.valueType=type;item.enabled=true;
        items.push_back(std::move(item));
    }
    RegCloseKey(key);
}
void AddDisabledItems(std::vector<StartupItem>& items,StartupKind kind,DWORD view) {
    const std::wstring rootPath=BackupRoot(kind,view);HKEY root=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,rootPath.c_str(),0,KEY_QUERY_VALUE,&root)!=ERROR_SUCCESS)return;
    DWORD count=0,maxName=0;
    if(RegQueryInfoKeyW(root,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&count,&maxName,nullptr,nullptr,nullptr)!=ERROR_SUCCESS){RegCloseKey(root);return;}
    std::vector<wchar_t> name(static_cast<size_t>(maxName)+2);
    for(DWORD i=0;i<count;++i){
        DWORD nameSize=static_cast<DWORD>(name.size()-1);FILETIME time{};
        if(RegEnumKeyExW(root,i,name.data(),&nameSize,nullptr,nullptr,nullptr,&time)!=ERROR_SUCCESS)continue;
        const std::wstring valueName(name.data(),nameSize);HKEY saved=nullptr;
        if(RegOpenKeyExW(root,valueName.c_str(),0,KEY_QUERY_VALUE,&saved)!=ERROR_SUCCESS)continue;
        DWORD type=REG_SZ,typeSize=sizeof(type),dataType=0,dataSize=0;
        LONG result=RegQueryValueExW(saved,L"Type",nullptr,&dataType,reinterpret_cast<BYTE*>(&type),&typeSize);
        if(result==ERROR_SUCCESS&&dataType==REG_DWORD&&typeSize==sizeof(type))result=RegQueryValueExW(saved,L"Data",nullptr,&dataType,nullptr,&dataSize);
        if(result==ERROR_SUCCESS&&dataType==REG_BINARY&&(type==REG_SZ||type==REG_EXPAND_SZ)){
            std::vector<BYTE> bytes(dataSize);if(!bytes.empty())result=RegQueryValueExW(saved,L"Data",nullptr,&dataType,bytes.data(),&dataSize);
            if(result==ERROR_SUCCESS){bytes.resize(dataSize);StartupItem item;item.name=valueName;item.rawData=std::move(bytes);
                item.command=RawString(item.rawData);item.source=SourceName(kind,view)+L" / disabled";item.kind=kind;
                item.registryView=view;item.valueType=type;item.enabled=false;item.canToggle=true;item.disabledBackup=true;
                item.canDelete=true;
                items.push_back(std::move(item));}
        }
        RegCloseKey(saved);
    }
    RegCloseKey(root);
}
void AddFolderItems(std::vector<StartupItem>& items,StartupKind kind,int folderId) {
    wchar_t buffer[MAX_PATH]{};if(FAILED(SHGetFolderPathW(nullptr,folderId,nullptr,SHGFP_TYPE_CURRENT,buffer)))return;
    const std::wstring folder=buffer;WIN32_FIND_DATAW found{};HANDLE search=FindFirstFileW((folder+L"\\*").c_str(),&found);
    if(search==INVALID_HANDLE_VALUE)return;
    do{
        if(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
        const std::wstring filename=found.cFileName;if(filename==L"."||filename==L"..")continue;
        const std::wstring suffix=L".nlite-disabled";
        const bool disabled=filename.size()>=suffix.size()&&_wcsicmp(filename.c_str()+filename.size()-suffix.size(),suffix.c_str())==0;
        std::wstring displayName=disabled?filename.substr(0,filename.size()-suffix.size()):filename;
        if(!IsStartupFolderLaunchableFile(displayName))continue;
        const size_t dot=displayName.find_last_of(L'.');if(dot!=std::wstring::npos&&dot>0)displayName.resize(dot);
        StartupItem item;item.name=displayName;item.kind=kind;item.path=folder+L"\\"+filename;
        item.command=disabled?item.path.substr(0,item.path.size()-suffix.size()):item.path;
        item.source=SourceName(kind,0)+(disabled?L" / disabled":L"");item.enabled=!disabled;
        item.canToggle=kind==StartupKind::UserFolder;
        item.canDelete=kind==StartupKind::UserFolder;
        if(!disabled&&(kind==StartupKind::UserFolder||kind==StartupKind::CommonFolder)){
            item.approvalSubkey=ApprovalSubkey(kind,KEY_WOW64_64KEY);item.approvalName=filename;
            const bool currentUser=kind==StartupKind::UserFolder;
            item.approvalManaged=currentUser;
            item.enabled=StartupSourceEnabled(true,ReadApproval(currentUser?HKEY_CURRENT_USER:HKEY_LOCAL_MACHINE,
                item.approvalSubkey,KEY_WOW64_64KEY,item.approvalName));
        }
        items.push_back(std::move(item));
    }while(FindNextFileW(search,&found));
    FindClose(search);
}

template <typename T> void ReleaseCom(T*& value) {
    if(value){value->Release();value=nullptr;}
}
std::wstring BstrText(BSTR value) {
    return value?std::wstring(value,SysStringLen(value)):std::wstring();
}
bool IsMicrosoftTaskPath(const std::wstring& path) {
    return IsProtectedStartupTaskPath(path);
}
struct ScheduledTaskAccess {
    bool canWrite = false;
    bool canWriteDac = false;
    bool canDelete = false;
};
bool ScheduledTaskAccessGranted(PSECURITY_DESCRIPTOR descriptor,DWORD desiredAccess) {
    if(!descriptor)return false;
    HANDLE primary=nullptr,token=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE,&primary))return false;
    const bool duplicated=DuplicateTokenEx(primary,TOKEN_QUERY,nullptr,SecurityImpersonation,
        TokenImpersonation,&token)!=FALSE;
    CloseHandle(primary);
    if(!duplicated)return false;
    GENERIC_MAPPING mapping{};
    mapping.GenericRead=FILE_GENERIC_READ;mapping.GenericWrite=FILE_GENERIC_WRITE;
    mapping.GenericExecute=FILE_GENERIC_EXECUTE;mapping.GenericAll=FILE_ALL_ACCESS;
    DWORD granted=0;BOOL accessStatus=FALSE;
    std::vector<BYTE> privileges(4096);
    DWORD privilegeBytes=static_cast<DWORD>(privileges.size());
    BOOL checked=AccessCheck(descriptor,token,desiredAccess,&mapping,
        reinterpret_cast<PPRIVILEGE_SET>(privileges.data()),&privilegeBytes,&granted,&accessStatus);
    if(!checked&&GetLastError()==ERROR_INSUFFICIENT_BUFFER&&privilegeBytes>privileges.size()){
        privileges.resize(privilegeBytes);
        privilegeBytes=static_cast<DWORD>(privileges.size());
        checked=AccessCheck(descriptor,token,desiredAccess,&mapping,
            reinterpret_cast<PPRIVILEGE_SET>(privileges.data()),&privilegeBytes,&granted,&accessStatus);
    }
    CloseHandle(token);
    return checked&&accessStatus!=FALSE;
}
ScheduledTaskAccess ReadScheduledTaskAccess(IRegisteredTask* task) {
    ScheduledTaskAccess access;
    if(!task)return access;
    BSTR sddl=nullptr;
    if(FAILED(task->GetSecurityDescriptor(OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|
        DACL_SECURITY_INFORMATION,&sddl))||!sddl)return access;
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    const BOOL converted=ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,
        SDDL_REVISION_1,&descriptor,nullptr);
    SysFreeString(sddl);
    if(!converted||!descriptor)return access;
    access.canWrite=ScheduledTaskAccessGranted(descriptor,FILE_GENERIC_WRITE);
    access.canWriteDac=ScheduledTaskAccessGranted(descriptor,WRITE_DAC);
    access.canDelete=ScheduledTaskAccessGranted(descriptor,DELETE);
    LocalFree(descriptor);
    return access;
}
std::wstring TaskCommand(ITaskDefinition* definition) {
    IActionCollection* actions=nullptr;if(FAILED(definition->get_Actions(&actions)))return L"";
    LONG count=0;actions->get_Count(&count);std::wstring command;
    for(LONG i=1;i<=count&&command.empty();++i){
        IAction* action=nullptr;
        if(SUCCEEDED(actions->get_Item(i,&action))&&action){
            IExecAction* exec=nullptr;
            if(SUCCEEDED(action->QueryInterface(IID_IExecAction,reinterpret_cast<void**>(&exec)))&&exec){
                BSTR path=nullptr,args=nullptr;
                if(SUCCEEDED(exec->get_Path(&path)))command=BstrText(path);
                if(SUCCEEDED(exec->get_Arguments(&args))&&!BstrText(args).empty()){
                    if(!command.empty())command+=L" ";
                    command+=BstrText(args);
                }
                SysFreeString(path);SysFreeString(args);ReleaseCom(exec);
            }
            ReleaseCom(action);
        }
    }
    ReleaseCom(actions);return command;
}
void AddTasksInFolder(ITaskFolder* folder,std::vector<StartupItem>& items,unsigned depth) {
    if(!folder||depth>12)return;
    IRegisteredTaskCollection* tasks=nullptr;
    if(SUCCEEDED(folder->GetTasks(TASK_ENUM_HIDDEN,&tasks))&&tasks){
        LONG count=0;tasks->get_Count(&count);
        for(LONG i=1;i<=count;++i){
            VARIANT index;VariantInit(&index);index.vt=VT_I4;index.lVal=i;
            IRegisteredTask* task=nullptr;
            if(SUCCEEDED(tasks->get_Item(index,&task))&&task){
                ITaskDefinition* definition=nullptr;
                if(SUCCEEDED(task->get_Definition(&definition))&&definition){
                    ITriggerCollection* triggers=nullptr;
                    if(SUCCEEDED(definition->get_Triggers(&triggers))&&triggers){
                        LONG triggerCount=0;triggers->get_Count(&triggerCount);
                        bool startup=false,onlyStartup=triggerCount>0,hasBoot=false,hasLogon=false,hasEnabledStartupTrigger=false;
                        for(LONG t=1;t<=triggerCount;++t){
                            ITrigger* trigger=nullptr;
                            if(SUCCEEDED(triggers->get_Item(t,&trigger))&&trigger){
                                TASK_TRIGGER_TYPE2 type{};trigger->get_Type(&type);
                                if(IsStartupTaskTriggerType(static_cast<int>(type))){
                                    startup=true;
                                    hasBoot=hasBoot||type==TASK_TRIGGER_BOOT;
                                    hasLogon=hasLogon||type==TASK_TRIGGER_LOGON;
                                    VARIANT_BOOL triggerEnabled=VARIANT_FALSE;
                                    if(SUCCEEDED(trigger->get_Enabled(&triggerEnabled))&&triggerEnabled==VARIANT_TRUE)
                                        hasEnabledStartupTrigger=true;
                                }else onlyStartup=false;
                                ReleaseCom(trigger);
                            }else onlyStartup=false;
                        }
                        if(startup){
                            BSTR taskName=nullptr,taskPath=nullptr;task->get_Name(&taskName);task->get_Path(&taskPath);
                            StartupItem item;item.kind=StartupKind::ScheduledTask;
                            item.name=BstrText(taskName);item.taskPath=BstrText(taskPath);
                            item.source=IsMicrosoftTaskPath(item.taskPath)?L"Windows task scheduler / ":L"Task Scheduler / ";
                            item.source+=hasBoot&&hasLogon?L"boot and sign-in":(hasBoot?L"boot":L"sign-in");
                            item.command=TaskCommand(definition);
                            VARIANT_BOOL enabled=VARIANT_FALSE;task->get_Enabled(&enabled);
                            item.enabled=enabled==VARIANT_TRUE&&hasEnabledStartupTrigger;
                            IPrincipal* principal=nullptr;TASK_LOGON_TYPE logonType=TASK_LOGON_NONE;
                            if(SUCCEEDED(definition->get_Principal(&principal))&&principal){
                                principal->get_LogonType(&logonType);ReleaseCom(principal);
                            }
                            const bool protectedTask=IsMicrosoftTaskPath(item.taskPath);
                            const ScheduledTaskAccess taskAccess=ReadScheduledTaskAccess(task);
                            const bool canRestoreDisabledTrigger=hasEnabledStartupTrigger||
                                logonType==TASK_LOGON_INTERACTIVE_TOKEN||logonType==TASK_LOGON_S4U;
                            item.canToggle=StartupTaskCanBeToggled(startup,onlyStartup,
                                protectedTask,canRestoreDisabledTrigger,taskAccess.canWrite,
                                hasEnabledStartupTrigger||taskAccess.canWriteDac);
                            item.canDelete=StartupTaskCanBeDeleted(onlyStartup,protectedTask,
                                taskAccess.canDelete);
                            SysFreeString(taskName);SysFreeString(taskPath);
                            if(!item.taskPath.empty())items.push_back(std::move(item));
                        }
                        ReleaseCom(triggers);
                    }
                    ReleaseCom(definition);
                }
                ReleaseCom(task);
            }
            VariantClear(&index);
        }
        ReleaseCom(tasks);
    }
    ITaskFolderCollection* folders=nullptr;
    if(SUCCEEDED(folder->GetFolders(0,&folders))&&folders){
        LONG count=0;folders->get_Count(&count);
        for(LONG i=1;i<=count;++i){
            VARIANT index;VariantInit(&index);index.vt=VT_I4;index.lVal=i;
            ITaskFolder* child=nullptr;
            if(SUCCEEDED(folders->get_Item(index,&child))&&child){
                AddTasksInFolder(child,items,depth+1);ReleaseCom(child);
            }
            VariantClear(&index);
        }
        ReleaseCom(folders);
    }
}
void AddScheduledStartupItems(std::vector<StartupItem>& items) {
    const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const bool uninitialize=SUCCEEDED(init);
    if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return;
    ITaskService* service=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,
        IID_ITaskService,reinterpret_cast<void**>(&service)))&&service){
        VARIANT empty;VariantInit(&empty);
        if(SUCCEEDED(service->Connect(empty,empty,empty,empty))){
            BSTR rootPath=SysAllocString(L"\\");ITaskFolder* root=nullptr;
            if(rootPath&&SUCCEEDED(service->GetFolder(rootPath,&root))&&root){
                AddTasksInFolder(root,items,0);ReleaseCom(root);
            }
            SysFreeString(rootPath);
        }
        ReleaseCom(service);
    }
    if(uninitialize)CoUninitialize();
}
bool SetScheduledTaskEnabled(const std::wstring& taskPath,bool enabled) {
    const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const bool uninitialize=SUCCEEDED(init);
    if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return false;
    bool ok=false;ITaskService* service=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,
        IID_ITaskService,reinterpret_cast<void**>(&service)))&&service){
        VARIANT empty;VariantInit(&empty);
        if(SUCCEEDED(service->Connect(empty,empty,empty,empty))){
            const size_t split=taskPath.find_last_of(L'\\');
            if(split!=std::wstring::npos&&split+1<taskPath.size()){
                const std::wstring folderName=split==0?L"\\":taskPath.substr(0,split);
                const std::wstring name=taskPath.substr(split+1);
                BSTR folderBstr=SysAllocString(folderName.c_str()),nameBstr=SysAllocString(name.c_str());
                ITaskFolder* folder=nullptr;IRegisteredTask* task=nullptr;
                if(folderBstr&&nameBstr&&SUCCEEDED(service->GetFolder(folderBstr,&folder))&&folder&&
                    SUCCEEDED(folder->GetTask(nameBstr,&task))&&task){
                    ITaskDefinition* definition=nullptr;ITriggerCollection* triggers=nullptr;
                    if(SUCCEEDED(task->get_Definition(&definition))&&definition&&
                        SUCCEEDED(definition->get_Triggers(&triggers))&&triggers){
                        LONG count=0;triggers->get_Count(&count);
                        bool hasStartup=false,hasEnabledStartup=false,onlyStartup=true;
                        for(LONG i=1;i<=count;++i){
                            ITrigger* trigger=nullptr;
                            if(SUCCEEDED(triggers->get_Item(i,&trigger))&&trigger){
                                TASK_TRIGGER_TYPE2 type{};trigger->get_Type(&type);
                                if(IsStartupTaskTriggerType(static_cast<int>(type))){
                                    hasStartup=true;VARIANT_BOOL triggerEnabled=VARIANT_FALSE;
                                    if(SUCCEEDED(trigger->get_Enabled(&triggerEnabled))&&triggerEnabled==VARIANT_TRUE)
                                        hasEnabledStartup=true;
                                }else onlyStartup=false;
                                ReleaseCom(trigger);
                            }else onlyStartup=false;
                        }
                        ok=hasStartup&&onlyStartup;
                        if(ok&&enabled&&!hasEnabledStartup){
                            bool triggersEnabled=true;
                            for(LONG i=1;i<=count&&ok;++i){
                                ITrigger* trigger=nullptr;
                                if(FAILED(triggers->get_Item(i,&trigger))||!trigger){triggersEnabled=false;break;}
                                TASK_TRIGGER_TYPE2 type{};trigger->get_Type(&type);
                                if(IsStartupTaskTriggerType(static_cast<int>(type))&&
                                    FAILED(trigger->put_Enabled(VARIANT_TRUE)))triggersEnabled=false;
                                ReleaseCom(trigger);
                            }
                            IPrincipal* principal=nullptr;BSTR user=nullptr,security=nullptr;
                            TASK_LOGON_TYPE logonType=TASK_LOGON_NONE;IRegisteredTask* updatedTask=nullptr;
                            HRESULT updateResult=triggersEnabled?S_OK:E_FAIL;
                            if(SUCCEEDED(updateResult))updateResult=definition->get_Principal(&principal);
                            if(SUCCEEDED(updateResult))updateResult=principal->get_UserId(&user);
                            if(SUCCEEDED(updateResult))updateResult=principal->get_LogonType(&logonType);
                            if(SUCCEEDED(updateResult)&&logonType!=TASK_LOGON_INTERACTIVE_TOKEN&&
                                logonType!=TASK_LOGON_S4U)updateResult=E_ACCESSDENIED;
                            if(SUCCEEDED(updateResult))updateResult=task->GetSecurityDescriptor(
                                OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
                                &security);
                            VARIANT userId,password,sddl;VariantInit(&userId);VariantInit(&password);VariantInit(&sddl);
                            if(SUCCEEDED(updateResult)&&user&&security){
                                userId.vt=VT_BSTR;userId.bstrVal=user;user=nullptr;
                                sddl.vt=VT_BSTR;sddl.bstrVal=security;security=nullptr;
                                updateResult=folder->RegisterTaskDefinition(nameBstr,definition,
                                    TASK_UPDATE|TASK_DONT_ADD_PRINCIPAL_ACE,userId,password,logonType,sddl,&updatedTask);
                            }else if(SUCCEEDED(updateResult))updateResult=E_ACCESSDENIED;
                            if(SUCCEEDED(updateResult)&&updatedTask)
                                updateResult=updatedTask->put_Enabled(VARIANT_TRUE);
                            ok=SUCCEEDED(updateResult);
                            VariantClear(&userId);VariantClear(&password);VariantClear(&sddl);
                            SysFreeString(user);SysFreeString(security);
                            ReleaseCom(updatedTask);ReleaseCom(principal);
                        }else if(ok){
                            ok=SUCCEEDED(task->put_Enabled(enabled?VARIANT_TRUE:VARIANT_FALSE));
                        }
                    }
                    ReleaseCom(triggers);ReleaseCom(definition);
                }
                ReleaseCom(task);ReleaseCom(folder);SysFreeString(folderBstr);SysFreeString(nameBstr);
            }
        }
        ReleaseCom(service);
    }
    if(uninitialize)CoUninitialize();
    return ok;
}
void AddWindowsShellItems(std::vector<StartupItem>& items) {
    const std::wstring key=L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
    const struct {const wchar_t* value;const wchar_t* label;} shell[]={
        {L"Shell",L"Windows desktop (Explorer)"},
        {L"Userinit",L"Windows sign-in"}
    };
    for(const auto& entry:shell){
        DWORD type=0;std::vector<BYTE> bytes;
        if(!ReadValue(HKEY_LOCAL_MACHINE,key,KEY_WOW64_64KEY,entry.value,type,bytes))continue;
        StartupItem item;item.kind=StartupKind::WindowsShell;item.name=entry.label;
        item.command=RawString(bytes);item.source=L"Windows / core startup";
        item.enabled=true;item.canToggle=false;items.push_back(std::move(item));
    }
}

bool DeleteBackup(const StartupItem& item) {
    const std::wstring path=BackupPath(item);HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_SET_VALUE,&key)!=ERROR_SUCCESS)return false;
    RegDeleteValueW(key,L"Type");RegDeleteValueW(key,L"Data");RegCloseKey(key);
    LONG removed=RegDeleteKeyW(HKEY_CURRENT_USER,path.c_str());return removed==ERROR_SUCCESS||removed==ERROR_FILE_NOT_FOUND;
}
bool DeleteScheduledTask(const std::wstring& taskPath) {
    const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const bool uninitialize=SUCCEEDED(init);
    if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return false;
    bool ok=false;ITaskService* service=nullptr;ITaskFolder* folder=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,
        IID_ITaskService,reinterpret_cast<void**>(&service)))&&service){
        VARIANT empty;VariantInit(&empty);
        if(SUCCEEDED(service->Connect(empty,empty,empty,empty))){
            const size_t split=taskPath.find_last_of(L'\\');
            if(split!=std::wstring::npos&&split+1<taskPath.size()){
                const std::wstring folderName=split==0?L"\\":taskPath.substr(0,split);
                const std::wstring name=taskPath.substr(split+1);
                BSTR folderBstr=SysAllocString(folderName.c_str()),nameBstr=SysAllocString(name.c_str());
                if(folderBstr&&nameBstr&&SUCCEEDED(service->GetFolder(folderBstr,&folder))&&folder)
                    ok=SUCCEEDED(folder->DeleteTask(nameBstr,0));
                SysFreeString(folderBstr);SysFreeString(nameBstr);
            }
        }
    }
    ReleaseCom(folder);ReleaseCom(service);
    if(uninitialize)CoUninitialize();
    return ok;
}
}

std::vector<StartupItem> EnumerateStartupItems() {
    std::vector<StartupItem> items;const DWORD views[]={KEY_WOW64_64KEY,KEY_WOW64_32KEY};
    for(DWORD view:views){
        AddRunItems(items,HKEY_CURRENT_USER,StartupKind::UserRun,view);AddRunItems(items,HKEY_CURRENT_USER,StartupKind::UserRunOnce,view);
        AddDisabledItems(items,StartupKind::UserRun,view);AddDisabledItems(items,StartupKind::UserRunOnce,view);
        AddRunItems(items,HKEY_LOCAL_MACHINE,StartupKind::MachineRun,view);AddRunItems(items,HKEY_LOCAL_MACHINE,StartupKind::MachineRunOnce,view);
        AddPolicyRunItems(items,HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run",
            L"Current user / enforced startup",view,true);
        AddPolicyRunItems(items,HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run",
            L"All users / enforced startup",view,false);
    }
    AddFolderItems(items,StartupKind::UserFolder,CSIDL_STARTUP);AddFolderItems(items,StartupKind::CommonFolder,CSIDL_COMMON_STARTUP);
    AddWindowsShellItems(items);
    AddScheduledStartupItems(items);
    auto lower=[](std::wstring value){std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});return value;};
    std::stable_sort(items.begin(),items.end(),[&](const StartupItem& a,const StartupItem& b){
        const bool shellA=a.kind==StartupKind::WindowsShell,shellB=b.kind==StartupKind::WindowsShell;
        if(shellA!=shellB)return StartupEntryPriorityBefore(shellA,shellB);
        if(a.enabled!=b.enabled)return a.enabled>b.enabled;
        const std::wstring as=lower(a.source),bs=lower(b.source);if(as!=bs)return as<bs;
        const std::wstring an=lower(a.name),bn=lower(b.name);if(an!=bn)return an<bn;return a.enabled>b.enabled;
    });
    return items;
}

bool SetStartupItemEnabled(StartupItem& item,bool enabled) {
    if(!item.canToggle)return false;
    if(item.kind==StartupKind::ScheduledTask){
        if(item.enabled==enabled)return true;
        if(!SetScheduledTaskEnabled(item.taskPath,enabled))return false;
        item.enabled=enabled;return true;
    }
    if(item.approvalManaged){
        if(item.enabled==enabled)return true;
        if(!WriteApproval(item,enabled))return false;
        item.enabled=enabled;return true;
    }
    if(item.kind==StartupKind::UserFolder){
        const std::wstring suffix=L".nlite-disabled";if(item.enabled==enabled)return true;
        const std::wstring destination=enabled?item.path.substr(0,item.path.size()-suffix.size()):item.path+suffix;
        if(!enabled&&GetFileAttributesW(destination.c_str())!=INVALID_FILE_ATTRIBUTES)return false;
        const DWORD originalAttributes=GetFileAttributesW(item.path.c_str());
        if(originalAttributes==INVALID_FILE_ATTRIBUTES)return false;
        const bool wasReadOnly=(originalAttributes&FILE_ATTRIBUTE_READONLY)!=0;
        if(wasReadOnly&&!SetFileAttributesW(item.path.c_str(),originalAttributes&~FILE_ATTRIBUTE_READONLY))return false;
        if(!MoveFileExW(item.path.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH)){
            if(wasReadOnly)SetFileAttributesW(item.path.c_str(),originalAttributes);
            return false;
        }
        if(wasReadOnly){
            const DWORD destinationAttributes=GetFileAttributesW(destination.c_str());
            if(destinationAttributes!=INVALID_FILE_ATTRIBUTES)
                SetFileAttributesW(destination.c_str(),destinationAttributes|FILE_ATTRIBUTE_READONLY);
        }
        item.path=destination;item.enabled=enabled;return true;
    }
    const std::wstring activeSubkey=RunSubkey(item.kind);
    if(enabled){
        if(!item.disabledBackup)return true;
        HKEY active=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,activeSubkey.c_str(),0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE|item.registryView,nullptr,&active,nullptr)!=ERROR_SUCCESS)return false;
        DWORD existingType=0,existingSize=0;LONG exists=RegQueryValueExW(active,item.name.c_str(),nullptr,&existingType,nullptr,&existingSize);
        if(exists==ERROR_SUCCESS){RegCloseKey(active);return false;}if(exists!=ERROR_FILE_NOT_FOUND){RegCloseKey(active);return false;}
        HKEY saved=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,BackupPath(item).c_str(),0,KEY_QUERY_VALUE,&saved)!=ERROR_SUCCESS){RegCloseKey(active);return false;}
        DWORD type=REG_SZ,typeSize=sizeof(type),dataType=0,size=0;
        LONG result=RegQueryValueExW(saved,L"Type",nullptr,&dataType,reinterpret_cast<BYTE*>(&type),&typeSize);
        if(result==ERROR_SUCCESS&&dataType==REG_DWORD&&typeSize==sizeof(type))result=RegQueryValueExW(saved,L"Data",nullptr,&dataType,nullptr,&size);
        std::vector<BYTE> bytes;if(result==ERROR_SUCCESS&&dataType==REG_BINARY&&size){bytes.resize(size);result=RegQueryValueExW(saved,L"Data",nullptr,&dataType,bytes.data(),&size);}
        RegCloseKey(saved);if(result==ERROR_SUCCESS)result=RegSetValueExW(active,item.name.c_str(),0,type,bytes.empty()?nullptr:bytes.data(),size);
        RegCloseKey(active);if(result!=ERROR_SUCCESS||!DeleteBackup(item))return false;
        item.enabled=true;item.disabledBackup=false;return true;
    }
    if(item.disabledBackup)return true;
    DWORD type=0;std::vector<BYTE> bytes;if(!ReadValue(HKEY_CURRENT_USER,activeSubkey,item.registryView,item.name,type,bytes))return false;
    HKEY saved=nullptr;const std::wstring savedPath=BackupPath(item);
    if(RegCreateKeyExW(HKEY_CURRENT_USER,savedPath.c_str(),0,nullptr,REG_OPTION_NON_VOLATILE,KEY_SET_VALUE,nullptr,&saved,nullptr)!=ERROR_SUCCESS)return false;
    LONG result=RegSetValueExW(saved,L"Type",0,REG_DWORD,reinterpret_cast<const BYTE*>(&type),sizeof(type));
    if(result==ERROR_SUCCESS)result=RegSetValueExW(saved,L"Data",0,REG_BINARY,bytes.empty()?nullptr:bytes.data(),static_cast<DWORD>(bytes.size()));
    RegCloseKey(saved);if(result!=ERROR_SUCCESS){DeleteBackup(item);return false;}
    HKEY active=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,activeSubkey.c_str(),0,KEY_SET_VALUE|item.registryView,&active)!=ERROR_SUCCESS){DeleteBackup(item);return false;}
    result=RegDeleteValueW(active,item.name.c_str());RegCloseKey(active);if(result!=ERROR_SUCCESS){DeleteBackup(item);return false;}
    item.enabled=false;item.disabledBackup=true;return true;
}

bool DeleteStartupItem(StartupItem& item) {
    if(!item.canDelete)return false;
    HKEY key=nullptr;
    if(item.kind==StartupKind::ScheduledTask){
        if(IsProtectedStartupTaskPath(item.taskPath))return false;
        return DeleteScheduledTask(item.taskPath);
    }
    if(item.kind==StartupKind::UserFolder){
        const DWORD attributes=GetFileAttributesW(item.path.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
        const bool wasReadOnly=(attributes&FILE_ATTRIBUTE_READONLY)!=0;
        if(wasReadOnly&&!SetFileAttributesW(item.path.c_str(),attributes&~FILE_ATTRIBUTE_READONLY))return false;
        if(!DeleteFileW(item.path.c_str())){
            if(wasReadOnly)SetFileAttributesW(item.path.c_str(),attributes);
            return false;
        }
        if(!item.approvalSubkey.empty()&&!item.approvalName.empty()&&
            RegOpenKeyExW(HKEY_CURRENT_USER,item.approvalSubkey.c_str(),0,
                KEY_SET_VALUE|item.registryView,&key)==ERROR_SUCCESS){
            RegDeleteValueW(key,item.approvalName.c_str());RegCloseKey(key);
        }
        item.canDelete=false;item.canToggle=false;return true;
    }
    if(!IsUserRegistryStartup(item.kind))return false;
    if(item.disabledBackup){
        if(!DeleteBackup(item))return false;
        item.canDelete=false;item.canToggle=false;return true;
    }
    if(RegOpenKeyExW(HKEY_CURRENT_USER,RunSubkey(item.kind),0,
        KEY_SET_VALUE|item.registryView,&key)!=ERROR_SUCCESS)return false;
    const LONG removed=RegDeleteValueW(key,item.name.c_str());
    RegCloseKey(key);
    if(removed!=ERROR_SUCCESS)return false;
    if(!item.approvalSubkey.empty()&&!item.approvalName.empty()&&
        RegOpenKeyExW(HKEY_CURRENT_USER,item.approvalSubkey.c_str(),0,
            KEY_SET_VALUE|item.registryView,&key)==ERROR_SUCCESS){
        RegDeleteValueW(key,item.approvalName.c_str());RegCloseKey(key);
    }
    DeleteBackup(item);
    item.canDelete=false;item.canToggle=false;return true;
}

bool AddStartupApplication(HWND owner,std::wstring& addedName) {
    std::vector<wchar_t> selected(32768,L'\0');
    static const wchar_t filter[]=L"Applications (*.exe)\0*.exe\0\0";
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=owner;dialog.lpstrFilter=filter;
    dialog.lpstrFile=selected.data();dialog.nMaxFile=static_cast<DWORD>(selected.size());dialog.lpstrTitle=L"Choose an app to start with Windows";
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_HIDEREADONLY;
#ifdef NLITE_STARTUP_TESTING
    const BOOL pickerSucceeded = gStartupFilePickerForTesting ?
        gStartupFilePickerForTesting(&dialog) : GetOpenFileNameW(&dialog);
#else
    const BOOL pickerSucceeded = GetOpenFileNameW(&dialog);
#endif
    if(!pickerSucceeded)return false;
    const std::wstring path=selected.data();std::vector<std::wstring> names;
    for(const auto& item:EnumerateStartupItems())if(item.kind==StartupKind::UserRun)names.push_back(item.name);
    addedName=StartupValueNameForPath(path,names);const std::wstring command=QuoteArgument(path);
    HKEY key=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,RunSubkey(StartupKind::UserRun),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    const LONG result=RegSetValueExW(key,addedName.c_str(),0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
    RegCloseKey(key);if(result!=ERROR_SUCCESS)return false;
    StartupItem approval;approval.kind=StartupKind::UserRun;approval.enabled=false;approval.canToggle=true;
    approval.approvalManaged=true;approval.registryView=KEY_WOW64_64KEY;
    approval.approvalName=addedName;
    approval.approvalSubkey=ApprovalSubkey(StartupKind::UserRun,KEY_WOW64_64KEY);
    if(SetStartupItemEnabled(approval,true))return true;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,RunSubkey(StartupKind::UserRun),0,KEY_SET_VALUE|KEY_WOW64_64KEY,&key)==ERROR_SUCCESS){
        RegDeleteValueW(key,addedName.c_str());RegCloseKey(key);
    }
    return false;
}
