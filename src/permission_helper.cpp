#include "helper_internal.h"
#include "backup.h"
#include <shellapi.h>
#include <map>
#include <mutex>

namespace ve {
bool helper_request_allowed(bool same_user,DWORD client,DWORD target,std::uint64_t expected,std::uint64_t actual) noexcept{
    return same_user&&client!=0&&client==target&&expected!=0&&expected==actual;
}
static bool tracked(){const auto path=state_directory()/L"helper.txt";refuse_reparse(path);return std::filesystem::exists(path);}
static void check_local(const std::wstring& id){
    const auto root=helper::protected_root(id);helper::verify_protected_file(root);verify_product(root);
    helper::verify_protected_file(root/L"owner.txt");helper::verify_protected_file(root/L"VolumeEditBroker.exe");
    helper::check_metadata(helper::read_metadata(root),{id,helper::current_sid(),deployment_root().wstring()});
}
bool helper_installed(){
    if(!tracked())return false;const auto id=deployment_identity(false);if(id.empty())throw std::runtime_error("Helper tracking has no deployment identity");
    helper::ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));if(!manager.value)win(FALSE);
    helper::ServiceHandle service(OpenServiceW(manager,helper::service_name(id).c_str(),SERVICE_QUERY_CONFIG));
    if(!service.value){if(GetLastError()==ERROR_SERVICE_DOES_NOT_EXIST)return false;win(FALSE);}
    helper::verify_config(service,id);check_local(id);return true;
}
void ensure_helper_running(){
    if(!helper_installed())return;const auto id=deployment_identity(false);
    helper::ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));if(!manager.value)win(FALSE);
    helper::ServiceHandle service(OpenServiceW(manager,helper::service_name(id).c_str(),SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_START));if(!service.value)win(FALSE);
    helper::verify_config(service,id);if(!StartServiceW(service,0,nullptr)&&GetLastError()!=ERROR_SERVICE_ALREADY_RUNNING)win(FALSE);
    const auto deadline=GetTickCount64()+5000;
    while(GetTickCount64()<deadline){SERVICE_STATUS_PROCESS status{};DWORD size=0;
        win(QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&size));
        if(status.dwCurrentState==SERVICE_RUNNING)return;
        if(status.dwCurrentState==SERVICE_STOPPED)throw Failure(HRESULT_FROM_WIN32(status.dwWin32ExitCode?status.dwWin32ExitCode:ERROR_SERVICE_NOT_ACTIVE));
        Sleep(50);
    }throw Failure(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
}
static helper::Reply exchange(const helper::Request& request){
    const auto id=deployment_identity(false);if(id.empty()||!tracked())throw Failure(HRESULT_FROM_WIN32(ERROR_SERVICE_DOES_NOT_EXIST));
    helper::ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));if(!manager.value)win(FALSE);
    helper::ServiceHandle service(OpenServiceW(manager,helper::service_name(id).c_str(),SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS));if(!service.value)win(FALSE);
    helper::verify_config(service,id);check_local(id);SERVICE_STATUS_PROCESS status{};DWORD size=0;
    win(QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&size));
    if(status.dwCurrentState!=SERVICE_RUNNING||!status.dwProcessId)throw Failure(HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE));
    constexpr DWORD access=(FILE_GENERIC_READ|FILE_GENERIC_WRITE)&~FILE_APPEND_DATA;
    const auto name=helper::pipe_name(id);
    Handle pipe(CreateFileW(name.c_str(),access,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
    if(!pipe&&GetLastError()==ERROR_PIPE_BUSY){if(WaitNamedPipeW(name.c_str(),1000))pipe.reset(CreateFileW(name.c_str(),access,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));}
    if(!pipe)win(FALSE);
    ULONG server=0;win(GetNamedPipeServerProcessId(pipe.get(),&server));if(server!=status.dwProcessId)throw Failure(E_ACCESSDENIED);
    DWORD mode=PIPE_READMODE_MESSAGE;win(SetNamedPipeHandleState(pipe.get(),&mode,nullptr,nullptr));
    auto outbound=request;auto code=helper::transfer(pipe.get(),&outbound,sizeof(outbound),true);if(code)throw Failure(HRESULT_FROM_WIN32(code));
    helper::Reply reply;code=helper::transfer(pipe.get(),&reply,sizeof(reply),false);if(code)throw Failure(HRESULT_FROM_WIN32(code));
    if(reply.magic!=helper::Magic||reply.version!=helper::Protocol||reply.owner>static_cast<DWORD>(Ownership::Other))throw Failure(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    DWORD ack=helper::Magic;code=helper::transfer(pipe.get(),&ack,sizeof(ack),true);if(code)throw Failure(HRESULT_FROM_WIN32(code));return reply;
}
OwnershipResult process_ownership(DWORD pid,bool assist){
    if(!pid)return {};Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));if(!process)return {Ownership::Unknown,GetLastError()};
    DWORD error=0;HANDLE value=nullptr;
    if(OpenProcessToken(process.get(),TOKEN_QUERY,&value)){
        Handle token(value);try{return {helper::token_sid(token.get())==helper::current_sid()?Ownership::Own:Ownership::Other};}
        catch(const Failure& f){error=static_cast<DWORD>(f.code)&0xffff;}
    }else error=GetLastError();
    if(!assist)return {Ownership::Unknown,error};
    try{
        const auto creation=helper::creation_time(process.get());const auto now=GetTickCount64();
        struct Cache{std::uint64_t creation,time;OwnershipResult result;};static std::mutex mutex;static std::map<DWORD,Cache> cache;std::lock_guard lock(mutex);
        const auto previous=cache.find(pid);if(previous!=cache.end()&&previous->second.creation==creation&&now-previous->second.time<1000)return previous->second.result;
        OwnershipResult result{Ownership::Unknown,error};
        try{helper::Request request;request.operation=static_cast<DWORD>(helper::Operation::Owner);request.pid=pid;request.creation=creation;
            const auto reply=exchange(request);result={static_cast<Ownership>(reply.owner),reply.error,true};
        }catch(...){}
        if(cache.size()>256)cache.clear();cache[pid]={creation,now,result};return result;
    }catch(...){return {Ownership::Unknown,error};}
}
void helper_startup(bool enabled){
    if(!helper_installed())return;ensure_helper_running();helper::Request request;request.operation=static_cast<DWORD>(helper::Operation::Startup);request.value=enabled?1:0;
    const auto reply=exchange(request);if(reply.error)throw Failure(HRESULT_FROM_WIN32(reply.error));
}
static void elevated_helper(HWND parent,bool install){
    const auto root=deployment_root();verify_product(root);const auto executable=root/L"VolumeEditBroker.exe";refuse_reparse(executable);
    if(!std::filesystem::exists(executable))throw std::runtime_error("缺少 VolumeEditBroker.exe，请完整解压服务试验包。");
    std::wstring parameters=(install?L"--install ":L"--remove ")+helper::quote_argument(root.wstring())+L" "+std::to_wstring(GetCurrentProcessId());
    if(install)parameters+=startup_enabled()?L" 1":L" 0";
    SHELLEXECUTEINFOW action{sizeof(action)};action.hwnd=parent;action.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;action.lpVerb=L"runas";action.lpFile=executable.c_str();action.lpParameters=parameters.c_str();action.nShow=SW_HIDE;
    win(ShellExecuteExW(&action));Handle child(action.hProcess);if(WaitForSingleObject(child.get(),30000)!=WAIT_OBJECT_0)throw Failure(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    DWORD exit=0;win(GetExitCodeProcess(child.get(),&exit));if(exit)throw Failure(HRESULT_FROM_WIN32(exit));
}
void install_helper(HWND parent){
    ensure_state();deployment_identity(true);atomic_text(state_directory()/L"helper.txt","VE_PERMISSION_HELPER_1\n");
    elevated_helper(parent,true);ensure_helper_running();
}
void remove_helper(HWND parent){
    if(!tracked())return;
    const auto id=deployment_identity(false);if(id.empty())throw std::runtime_error("Helper tracking has no deployment identity");
    if(helper_installed()||std::filesystem::exists(helper::protected_root(id)))elevated_helper(parent,false);
    std::filesystem::remove(state_directory()/L"helper.txt");
}
}
