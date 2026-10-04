#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#include "startup_manager.h"
#include "startup_policy.h"

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
        const size_t dot=displayName.find_last_of(L'.');if(dot!=std::wstring::npos&&dot>0)displayName.resize(dot);
        StartupItem item;item.name=displayName;item.kind=kind;item.path=folder+L"\\"+filename;
        item.command=disabled?item.path.substr(0,item.path.size()-suffix.size()):item.path;
        item.source=SourceName(kind,0)+(disabled?L" / disabled":L"");item.enabled=!disabled;
        item.canToggle=kind==StartupKind::UserFolder;items.push_back(std::move(item));
    }while(FindNextFileW(search,&found));
    FindClose(search);
}
bool DeleteBackup(const StartupItem& item) {
    const std::wstring path=BackupPath(item);HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_SET_VALUE,&key)!=ERROR_SUCCESS)return false;
    RegDeleteValueW(key,L"Type");RegDeleteValueW(key,L"Data");RegCloseKey(key);
    LONG removed=RegDeleteKeyW(HKEY_CURRENT_USER,path.c_str());return removed==ERROR_SUCCESS||removed==ERROR_FILE_NOT_FOUND;
}
}

std::vector<StartupItem> EnumerateStartupItems() {
    std::vector<StartupItem> items;const DWORD views[]={KEY_WOW64_64KEY,KEY_WOW64_32KEY};
    for(DWORD view:views){
        AddRunItems(items,HKEY_CURRENT_USER,StartupKind::UserRun,view);AddRunItems(items,HKEY_CURRENT_USER,StartupKind::UserRunOnce,view);
        AddDisabledItems(items,StartupKind::UserRun,view);AddDisabledItems(items,StartupKind::UserRunOnce,view);
        AddRunItems(items,HKEY_LOCAL_MACHINE,StartupKind::MachineRun,view);AddRunItems(items,HKEY_LOCAL_MACHINE,StartupKind::MachineRunOnce,view);
    }
    AddFolderItems(items,StartupKind::UserFolder,CSIDL_STARTUP);AddFolderItems(items,StartupKind::CommonFolder,CSIDL_COMMON_STARTUP);
    auto lower=[](std::wstring value){std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});return value;};
    std::stable_sort(items.begin(),items.end(),[&](const StartupItem& a,const StartupItem& b){
        const std::wstring as=lower(a.source),bs=lower(b.source);if(as!=bs)return as<bs;
        const std::wstring an=lower(a.name),bn=lower(b.name);if(an!=bn)return an<bn;return a.enabled>b.enabled;
    });
    return items;
}

bool SetStartupItemEnabled(StartupItem& item,bool enabled) {
    if(!item.canToggle)return false;
    if(item.kind==StartupKind::UserFolder){
        const std::wstring suffix=L".nlite-disabled";if(item.enabled==enabled)return true;
        const std::wstring destination=enabled?item.path.substr(0,item.path.size()-suffix.size()):item.path+suffix;
        if(!enabled&&GetFileAttributesW(destination.c_str())!=INVALID_FILE_ATTRIBUTES)return false;
        if(!MoveFileExW(item.path.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH))return false;
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

bool AddStartupApplication(HWND owner,std::wstring& addedName) {
    std::vector<wchar_t> selected(32768,L'\0');
    static const wchar_t filter[]=L"Applications (*.exe)\0*.exe\0\0";
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=owner;dialog.lpstrFilter=filter;
    dialog.lpstrFile=selected.data();dialog.nMaxFile=static_cast<DWORD>(selected.size());dialog.lpstrTitle=L"Choose an app to start with Windows";
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_HIDEREADONLY;
    if(!GetOpenFileNameW(&dialog))return false;
    const std::wstring path=selected.data();std::vector<std::wstring> names;
    for(const auto& item:EnumerateStartupItems())if(item.kind==StartupKind::UserRun)names.push_back(item.name);
    addedName=StartupValueNameForPath(path,names);const std::wstring command=QuoteArgument(path);
    HKEY key=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,RunSubkey(StartupKind::UserRun),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    const LONG result=RegSetValueExW(key,addedName.c_str(),0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
    RegCloseKey(key);return result==ERROR_SUCCESS;
}
