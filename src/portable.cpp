#include "portable.h"
#include "backup.h"
#include <fstream>
#include <vector>

namespace fs=std::filesystem;
namespace ve {
void refuse_reparse(const fs::path& path){
    auto current=fs::absolute(path);while(!current.empty()){
        const DWORD attr=GetFileAttributesW(current.c_str());if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Refusing cleanup or writes through a reparse point");
        const auto parent=current.parent_path();if(parent==current)break;current=parent;
    }
}
void verify_product(const fs::path& root){
    refuse_reparse(root);refuse_reparse(root/L"product.id");std::ifstream file(root/L"product.id",std::ios::binary);std::string id;std::getline(file,id);
    if(!id.empty()&&id.back()=='\r')id.pop_back();
    if(id!=ProductId)throw std::runtime_error("Cannot verify this portable deployment; cleanup stopped");
}
void ensure_state(){verify_product(deployment_root());refuse_reparse(state_directory());fs::create_directory(state_directory());}
std::wstring deployment_identity(bool create){
    const auto path=state_directory()/L"instance.id";refuse_reparse(path);
    if(!fs::exists(path)){
        if(!create)return {};
        ensure_state();GUID guid{};hr(CoCreateGuid(&guid));wchar_t value[40];StringFromGUID2(guid,value,40);atomic_text(path,utf8(value)+"\n");
    }
    std::ifstream file(path,std::ios::binary);std::string line;std::getline(file,line);auto text=wide(line);GUID guid{};
    if(text.size()!=38||FAILED(CLSIDFromString(text.c_str(),&guid)))throw std::runtime_error("Invalid portable deployment identity");return text;
}
struct Key{HKEY value{};~Key(){if(value)RegCloseKey(value);}};
static std::wstring run_name(bool create){const auto id=deployment_identity(create);return id.empty()?L"":L"VolumeEdit.Portable."+id;}
static std::wstring run_value(){return L"\""+(deployment_root()/L"VolumeEdit.exe").wstring()+L"\" --tray";}
static std::wstring get_value(HKEY key,const std::wstring& name){
    DWORD type=0,size=0;auto code=RegQueryValueExW(key,name.c_str(),nullptr,&type,nullptr,&size);if(code==ERROR_FILE_NOT_FOUND)return {};
    if(code!=ERROR_SUCCESS)throw Failure(HRESULT_FROM_WIN32(code));
    if(type!=REG_SZ||size<2||size>65536||size%sizeof(wchar_t))throw std::runtime_error("Unexpected startup registration");
    std::vector<wchar_t> data(size/sizeof(wchar_t));code=RegQueryValueExW(key,name.c_str(),nullptr,&type,reinterpret_cast<BYTE*>(data.data()),&size);
    if(code!=ERROR_SUCCESS)throw Failure(HRESULT_FROM_WIN32(code));if(data.back()!=0)throw std::runtime_error("Invalid startup registration");return data.data();
}
static void check_ownership(const std::wstring& value){
    if(value.empty()||value==run_value())return;
    const auto path=state_directory()/L"startup.txt";refuse_reparse(path);std::ifstream file(path,std::ios::binary);std::string previous;
    while(std::getline(file,previous))if(value==wide(previous))return;
    throw std::runtime_error("Startup registration does not belong to this deployment");
}
bool startup_enabled(){
    const auto name=run_name(false);if(name.empty())return false;
    Key key;const auto code=RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&key.value);
    if(code==ERROR_FILE_NOT_FOUND)return false;if(code!=ERROR_SUCCESS)throw Failure(HRESULT_FROM_WIN32(code));
    const auto value=get_value(key.value,name);check_ownership(value);return !value.empty();
}
void set_startup(bool enabled){
    ensure_state();const auto name=run_name(true);Key key;
    auto code=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key.value,nullptr);
    if(code!=ERROR_SUCCESS)throw Failure(HRESULT_FROM_WIN32(code));const auto existing=get_value(key.value,name);check_ownership(existing);
    if(enabled){
        const auto value=run_value();atomic_text(state_directory()/L"startup.txt",utf8(value)+"\n"+(existing.empty()?"":utf8(existing)+"\n"));
        code=RegSetValueExW(key.value,name.c_str(),0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));
    }else{code=RegDeleteValueW(key.value,name.c_str());if(code==ERROR_FILE_NOT_FOUND)code=ERROR_SUCCESS;}
    if(code!=ERROR_SUCCESS)throw Failure(HRESULT_FROM_WIN32(code));
    if(!enabled)fs::remove(state_directory()/L"startup.txt");
}
void clear_startup(){if(!deployment_identity(false).empty())set_startup(false);}
bool unlink_executable(const fs::path& path){
    Handle file(CreateFileW(path.c_str(),DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr));if(!file)return GetLastError()==ERROR_FILE_NOT_FOUND;
    FILE_DISPOSITION_INFO_EX disposition{FILE_DISPOSITION_FLAG_DELETE|FILE_DISPOSITION_FLAG_POSIX_SEMANTICS|FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE};
    return SetFileInformationByHandle(file.get(),FileDispositionInfoEx,&disposition,sizeof(disposition))!=FALSE;
}
void remove_product_files(const fs::path& root,bool keep_uninstaller){
    verify_product(root);refuse_reparse(root/L"state");
    // Fixed allowlist: preserve source files and user-created files in the folder.
    for(const auto* name:{L"settings.json",L"settings.json.tmp",L"volumes.txt",L"volumes.txt.tmp",L"instance.id",L"instance.id.tmp",L"startup.txt",L"startup.txt.tmp",L"helper.txt",L"helper.txt.tmp"}){
        const auto path=root/L"state"/name;refuse_reparse(path);fs::remove(path);
    }
    if(fs::exists(root/L"state")&&fs::is_empty(root/L"state"))fs::remove(root/L"state");
    for(const auto* name:{L"VolumeEdit.exe",L"uninstall.exe",L"VolumeEditBroker.exe",L"README.md",L"DESIGN.md",L"BUILD_STATUS.md",L"LICENSE",L"SHA256SUMS.txt",L"product.id"}){
        if(keep_uninstaller&&(std::wstring_view(name)==L"uninstall.exe"||std::wstring_view(name)==L"product.id"))continue;
        const auto path=root/name;refuse_reparse(path);if(!fs::exists(path))continue;
        if(path.extension()==L".exe"){if(!unlink_executable(path))throw Failure(HRESULT_FROM_WIN32(GetLastError()));}
        else fs::remove(path);
    }
    if(fs::exists(root)&&fs::is_empty(root))fs::remove(root);
}
void launch_final_cleanup(const fs::path& root,bool notify){
    verify_product(root);
    // Windows can deny deleting a mapped executable. An OS-supplied process
    // waits for us to exit, then removes only two literal product paths.
    // The script exists only in the child process command line, never on disk.
    const std::wstring script=LR"PS(
$ErrorActionPreference='Stop'
$taskRoot=$env:VOLUMEEDIT_CLEANUP_ROOT
$taskPid=[int]$env:VOLUMEEDIT_CLEANUP_PID
$taskNotify=$env:VOLUMEEDIT_CLEANUP_NOTIFY -eq '1'
try {
    $taskParent=Get-Process -Id $taskPid -ErrorAction SilentlyContinue
    if($taskParent -and -not $taskParent.WaitForExit(60000)){throw '卸载窗口尚未退出，请稍后重试。'}
    $taskItem=Get-Item -LiteralPath $taskRoot -Force
    while($taskItem){
        if(($taskItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw '拒绝通过链接目录清理。'}
        $taskItem=$taskItem.Parent
    }
    $taskMarker=Join-Path $taskRoot 'product.id'
    if([IO.File]::ReadAllText($taskMarker).Trim() -ne 'VolumeEdit.Portable.8A7C7476-55F3-4525-82E7-33B2FB781132'){throw '部署目录标记无效。'}
    foreach($taskPath in @((Join-Path $taskRoot 'uninstall.exe'),$taskMarker)){
        $taskItem=Get-Item -LiteralPath $taskPath -Force
        if(($taskItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw '拒绝清理链接文件。'}
        Remove-Item -LiteralPath $taskPath -Force
    }
    if(-not (Get-ChildItem -LiteralPath $taskRoot -Force | Select-Object -First 1)){Remove-Item -LiteralPath $taskRoot}
    $taskMessage='卸载完成。产品文件和开机启动项已清理；目录中的其他文件会保留。'
} catch {$taskMessage='卸载未完成：'+$_.Exception.Message+' 请保留并重新运行 uninstall.exe 重试。'}
if($taskNotify){Add-Type -AssemblyName System.Windows.Forms;[void][Windows.Forms.MessageBox]::Show($taskMessage,'VolumeEdit 卸载')}
)PS";
    const auto* data=reinterpret_cast<const unsigned char*>(script.data());const size_t size=script.size()*sizeof(wchar_t);
    const char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::wstring encoded;
    for(size_t i=0;i<size;i+=3){unsigned value=static_cast<unsigned>(data[i])<<16;if(i+1<size)value|=static_cast<unsigned>(data[i+1])<<8;if(i+2<size)value|=data[i+2];encoded+=table[(value>>18)&63];encoded+=table[(value>>12)&63];encoded+=i+1<size?table[(value>>6)&63]:'=';encoded+=i+2<size?table[value&63]:'=';}
    wchar_t windows[32768];const auto n=GetWindowsDirectoryW(windows,32768);if(!n||n>=32768)win(FALSE);
    const auto exe=fs::path(windows)/L"System32/WindowsPowerShell/v1.0/powershell.exe";
    const auto root_value=fs::absolute(root).wstring(),pid=std::to_wstring(GetCurrentProcessId());
    win(SetEnvironmentVariableW(L"VOLUMEEDIT_CLEANUP_ROOT",root_value.c_str()));win(SetEnvironmentVariableW(L"VOLUMEEDIT_CLEANUP_PID",pid.c_str()));win(SetEnvironmentVariableW(L"VOLUMEEDIT_CLEANUP_NOTIFY",notify?L"1":L"0"));
    std::wstring command=L"\""+exe.wstring()+L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand "+encoded;
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    const BOOL ok=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,windows,&startup,&process);const DWORD error=GetLastError();
    SetEnvironmentVariableW(L"VOLUMEEDIT_CLEANUP_ROOT",nullptr);SetEnvironmentVariableW(L"VOLUMEEDIT_CLEANUP_PID",nullptr);SetEnvironmentVariableW(L"VOLUMEEDIT_CLEANUP_NOTIFY",nullptr);
    if(!ok)throw Failure(HRESULT_FROM_WIN32(error));Handle child(process.hProcess),thread(process.hThread);
}
}
