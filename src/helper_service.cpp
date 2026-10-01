#include "helper_internal.h"
#include <shellapi.h>
#include <thread>
#include <mutex>

namespace fs=std::filesystem;
namespace {
ve::Handle stop_event;SERVICE_STATUS_HANDLE status_handle{};SERVICE_STATUS status{};std::wstring identity;std::mutex status_mutex;
void publish(DWORD state,DWORD error=0){
    std::lock_guard lock(status_mutex);
    status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;status.dwCurrentState=state;status.dwWin32ExitCode=error;
    status.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN:0;
    status.dwWaitHint=state==SERVICE_START_PENDING||state==SERVICE_STOP_PENDING?5000:0;
    status.dwCheckPoint=status.dwWaitHint?status.dwCheckPoint+1:0;SetServiceStatus(status_handle,&status);
}
DWORD WINAPI control(DWORD code,DWORD,void*,void*){
    if(code==SERVICE_CONTROL_STOP||code==SERVICE_CONTROL_SHUTDOWN){publish(SERVICE_STOP_PENDING);SetEvent(stop_event.get());}
    else if(code==SERVICE_CONTROL_INTERROGATE){std::lock_guard lock(status_mutex);SetServiceStatus(status_handle,&status);}return NO_ERROR;
}
DWORD authenticate(HANDLE pipe,const ve::helper::Metadata& metadata,DWORD& session){
    ULONG pid=0;if(!GetNamedPipeClientProcessId(pipe,&pid)||!ProcessIdToSessionId(pid,&session))return GetLastError();
    if(!session)return ERROR_ACCESS_DENIED;
    if(!ImpersonateNamedPipeClient(pipe))return GetLastError();
    struct Revert{~Revert(){RevertToSelf();}} revert;
    HANDLE value=nullptr;if(!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&value))return GetLastError();ve::Handle token(value);
    try{if(ve::helper::token_sid(token.get())!=metadata.sid)return ERROR_ACCESS_DENIED;
        DWORD token_session=0,size=0;if(!GetTokenInformation(token.get(),TokenSessionId,&token_session,sizeof(token_session),&size))return GetLastError();
        return token_session==session?ERROR_SUCCESS:ERROR_ACCESS_DENIED;
    }catch(...){return ERROR_ACCESS_DENIED;}
}
ve::helper::Reply handle(const ve::helper::Request& request,HANDLE pipe,const ve::helper::Metadata& metadata){
    ve::helper::Reply reply;DWORD session=0;reply.error=authenticate(pipe,metadata,session);if(reply.error)return reply;
    if(request.magic!=ve::helper::Magic||request.version!=ve::helper::Protocol||request.reserved){reply.error=ERROR_INVALID_DATA;return reply;}
    if(request.operation==static_cast<DWORD>(ve::helper::Operation::Startup)){
        if(request.value>1||request.pid||request.creation){reply.error=ERROR_INVALID_DATA;return reply;}
        ve::helper::ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));
        ve::helper::ServiceHandle service(manager.value?OpenServiceW(manager,ve::helper::service_name(metadata.id).c_str(),SERVICE_CHANGE_CONFIG|SERVICE_QUERY_CONFIG):nullptr);
        if(!service.value){reply.error=GetLastError();return reply;}
        try{ve::helper::verify_config(service,metadata.id);}catch(...){reply.error=ERROR_ACCESS_DENIED;return reply;}
        if(!ChangeServiceConfigW(service,SERVICE_NO_CHANGE,request.value?SERVICE_AUTO_START:SERVICE_DEMAND_START,SERVICE_NO_CHANGE,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr))reply.error=GetLastError();
        return reply;
    }
    if(request.operation!=static_cast<DWORD>(ve::helper::Operation::Owner)||!request.pid||request.value){reply.error=ERROR_INVALID_DATA;return reply;}
    ve::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,request.pid));if(!process){reply.error=GetLastError();return reply;}
    DWORD target=0;if(!ProcessIdToSessionId(request.pid,&target)){reply.error=GetLastError();return reply;}
    try{if(!ve::helper_request_allowed(true,session,target,request.creation,ve::helper::creation_time(process.get()))){reply.error=ERROR_ACCESS_DENIED;return reply;}}
    catch(...){reply.error=ERROR_ACCESS_DENIED;return reply;}
    HANDLE value=nullptr;if(!OpenProcessToken(process.get(),TOKEN_QUERY,&value)){reply.error=GetLastError();return reply;}ve::Handle token(value);
    try{reply.owner=static_cast<DWORD>(ve::helper::token_sid(token.get())==metadata.sid?ve::Ownership::Own:ve::Ownership::Other);}
    catch(...){reply.error=ERROR_ACCESS_DENIED;}return reply;
}
ve::Handle make_pipe(const ve::helper::Metadata& metadata){
    // The client receives read/write access but cannot create a competing server.
    auto descriptor=ve::helper::security(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x0012019b;;;"+metadata.sid+L")");
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor.value,FALSE};
    ve::Handle pipe(CreateNamedPipeW(ve::helper::pipe_name(metadata.id).c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,1000,&attributes));if(!pipe)ve::win(FALSE);return pipe;
}
void serve(const ve::helper::Metadata& metadata){
    auto pipe=make_pipe(metadata);
    publish(SERVICE_RUNNING);
    while(WaitForSingleObject(stop_event.get(),0)!=WAIT_OBJECT_0){
        ve::Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event)ve::win(FALSE);OVERLAPPED connection{};connection.hEvent=event.get();
        if(!ConnectNamedPipe(pipe.get(),&connection)){
            const auto code=GetLastError();if(code==ERROR_PIPE_CONNECTED)SetEvent(event.get());else if(code!=ERROR_IO_PENDING)throw ve::Failure(HRESULT_FROM_WIN32(code));
        }
        const HANDLE events[]={event.get(),stop_event.get()};if(WaitForMultipleObjects(2,events,FALSE,INFINITE)!=WAIT_OBJECT_0){CancelIoEx(pipe.get(),&connection);DWORD bytes=0;GetOverlappedResult(pipe.get(),&connection,&bytes,TRUE);break;}
        ve::helper::Request request;auto error=ve::helper::transfer(pipe.get(),&request,sizeof(request),false,stop_event.get());
        if(!error){auto reply=handle(request,pipe.get(),metadata);error=ve::helper::transfer(pipe.get(),&reply,sizeof(reply),true,stop_event.get());
            // Read an acknowledgement with a timeout instead of blocking FlushFileBuffers.
            if(!error){DWORD ack=0;ve::helper::transfer(pipe.get(),&ack,sizeof(ack),false,stop_event.get(),1000);}}
        DisconnectNamedPipe(pipe.get());
    }
}
int pipe_smoke(){
    ve::Apartment apartment;GUID guid{};ve::hr(CoCreateGuid(&guid));wchar_t text[40];StringFromGUID2(guid,text,40);
    const ve::helper::Metadata metadata{text,ve::helper::current_sid(),L""};auto pipe=make_pipe(metadata);
    DWORD session=0;ve::win(ProcessIdToSessionId(GetCurrentProcessId(),&session));
    const auto creation=ve::helper::creation_time(GetCurrentProcess());
    for(unsigned test=0;test<5;++test){
        ve::helper::Request request;request.operation=static_cast<DWORD>(ve::helper::Operation::Owner);request.pid=GetCurrentProcessId();request.creation=creation;
        if(test==1)request.version=99;if(test==2)++request.creation;if(test==3){request.operation=static_cast<DWORD>(ve::helper::Operation::Startup);request.pid=0;request.creation=0;request.value=2;}
        auto server_metadata=metadata;if(test==4)server_metadata.sid+=L"-9999";
        ve::helper::Reply received;DWORD client_error=0;
        std::thread client([&]{try{
            constexpr DWORD access=(FILE_GENERIC_READ|FILE_GENERIC_WRITE)&~FILE_APPEND_DATA;
            ve::Handle channel(CreateFileW(ve::helper::pipe_name(metadata.id).c_str(),access,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));if(!channel)ve::win(FALSE);
            DWORD mode=PIPE_READMODE_MESSAGE;ve::win(SetNamedPipeHandleState(channel.get(),&mode,nullptr,nullptr));
            client_error=ve::helper::transfer(channel.get(),&request,sizeof(request),true);if(!client_error)client_error=ve::helper::transfer(channel.get(),&received,sizeof(received),false);
            if(!client_error){DWORD ack=ve::helper::Magic;client_error=ve::helper::transfer(channel.get(),&ack,sizeof(ack),true);}
        }catch(...){client_error=ERROR_GEN_FAILURE;}});
        ve::Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED connected{};connected.hEvent=event.get();
        const BOOL immediate=ConnectNamedPipe(pipe.get(),&connected);const auto error=immediate?ERROR_SUCCESS:GetLastError();
        bool okay=immediate||error==ERROR_PIPE_CONNECTED||(error==ERROR_IO_PENDING&&WaitForSingleObject(event.get(),5000)==WAIT_OBJECT_0);
        if(okay){ve::helper::Request inbound;okay=ve::helper::transfer(pipe.get(),&inbound,sizeof(inbound),false)==0;if(okay){auto reply=handle(inbound,pipe.get(),server_metadata);okay=ve::helper::transfer(pipe.get(),&reply,sizeof(reply),true)==0;if(okay){DWORD ack=0;ve::helper::transfer(pipe.get(),&ack,sizeof(ack),false);}}}
        if(!okay){CancelIoEx(pipe.get(),nullptr);if(error==ERROR_IO_PENDING){DWORD bytes=0;GetOverlappedResult(pipe.get(),&connected,&bytes,TRUE);}}
        DisconnectNamedPipe(pipe.get());client.join();if(!okay||client_error||received.magic!=ve::helper::Magic||received.version!=ve::helper::Protocol)return 10+test;
        const DWORD expected=!session||test==4||test==2?ERROR_ACCESS_DENIED:(test==1||test==3?ERROR_INVALID_DATA:ERROR_SUCCESS);
        if(received.error!=expected||(expected==0&&received.owner!=static_cast<DWORD>(ve::Ownership::Own)))return 20+test;
    }return 0;
}
void WINAPI service_main(DWORD,LPWSTR*){
    status_handle=RegisterServiceCtrlHandlerExW(ve::helper::service_name(identity).c_str(),control,nullptr);if(!status_handle)return;publish(SERVICE_START_PENDING);
    DWORD error=0;try{
        ve::Apartment apartment;const auto root=ve::executable_directory();ve::helper::verify_protected_file(root);ve::verify_product(root);
        ve::helper::verify_protected_file(root/L"owner.txt");ve::helper::verify_protected_file(root/L"VolumeEditBroker.exe");const auto metadata=ve::helper::read_metadata(root);
        if(metadata.id!=identity||root!=ve::helper::protected_root(identity))throw std::runtime_error("Service ownership mismatch");serve(metadata);
    }catch(const ve::Failure& f){error=static_cast<DWORD>(f.code)&0xffff;}catch(...){error=ERROR_INVALID_DATA;}publish(SERVICE_STOPPED,error);
}
ve::helper::Metadata installer_context(const fs::path& root,DWORD pid,bool installing){
    ve::verify_product(root);ve::set_deployment_root(root);const auto id=ve::deployment_identity(false);if(id.empty())throw std::runtime_error("Missing deployment identity");
    if(_wcsicmp(ve::executable_directory().c_str(),root.c_str())!=0)throw std::runtime_error("Installer must be launched from this deployment");
    if(root.wstring().find_first_of(L"\r\n\"")!=std::wstring::npos)throw std::runtime_error("Unsupported deployment path");
    ve::Handle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));if(!parent)ve::win(FALSE);
    wchar_t image[32768];DWORD size=32768;ve::win(QueryFullProcessImageNameW(parent.get(),0,image,&size));
    const auto expected=root/(installing?L"VolumeEdit.exe":L"uninstall.exe");if(_wcsicmp(image,expected.c_str())!=0)throw std::runtime_error("Installer caller does not match the deployment");
    HANDLE value=nullptr;ve::win(OpenProcessToken(parent.get(),TOKEN_QUERY,&value));ve::Handle token(value);
    return {id,ve::helper::token_sid(token.get()),root.wstring()};
}
void wait_state(SC_HANDLE service,DWORD wanted){
    const auto deadline=GetTickCount64()+15000;
    while(GetTickCount64()<deadline){SERVICE_STATUS_PROCESS current{};DWORD size=0;ve::win(QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&current),sizeof(current),&size));
        if(current.dwCurrentState==wanted)return;
        if(wanted==SERVICE_RUNNING&&current.dwCurrentState==SERVICE_STOPPED)throw ve::Failure(HRESULT_FROM_WIN32(current.dwWin32ExitCode?current.dwWin32ExitCode:ERROR_SERVICE_NOT_ACTIVE));Sleep(50);
    }throw ve::Failure(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
}
void stop(SC_HANDLE service){
    SERVICE_STATUS_PROCESS before{};DWORD size=0;ve::win(QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&before),sizeof(before),&size));
    ve::Handle process(before.dwProcessId?OpenProcess(SYNCHRONIZE,FALSE,before.dwProcessId):nullptr);
    SERVICE_STATUS status_now{};if(!ControlService(service,SERVICE_CONTROL_STOP,&status_now)&&GetLastError()!=ERROR_SERVICE_NOT_ACTIVE)ve::win(FALSE);wait_state(service,SERVICE_STOPPED);
    if(process&&WaitForSingleObject(process.get(),5000)!=WAIT_OBJECT_0)throw ve::Failure(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
}
ve::Handle create_owned(const fs::path& path){
    ve::refuse_reparse(path);if(fs::exists(path))ve::helper::verify_protected_file(path);
    auto descriptor=ve::helper::security(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x001200a9;;;BU)");
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor.value,FALSE};
    ve::Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,&attributes,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));if(!file)ve::win(FALSE);return file;
}
void write_owned(const fs::path& path,const std::string& text){
    auto file=create_owned(path);DWORD written=0;ve::win(WriteFile(file.get(),text.data(),static_cast<DWORD>(text.size()),&written,nullptr));
    if(written!=text.size())throw ve::Failure(HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));ve::win(FlushFileBuffers(file.get()));
}
void copy_owned(const fs::path& source,const fs::path& destination){
    ve::refuse_reparse(source);ve::Handle input(CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(!input)ve::win(FALSE);
    auto output=create_owned(destination);std::array<BYTE,65536> buffer;
    for(;;){DWORD read=0,written=0;ve::win(ReadFile(input.get(),buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr));if(!read)break;
        ve::win(WriteFile(output.get(),buffer.data(),read,&written,nullptr));if(written!=read)throw ve::Failure(HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));}
    ve::win(FlushFileBuffers(output.get()));
}
void install(const ve::helper::Metadata& metadata,bool automatic){
    const auto root=ve::helper::protected_root(metadata.id);ve::refuse_reparse(root);
    auto descriptor=ve::helper::security(L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x001200a9;;;BU)");SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor.value,FALSE};
    if(!CreateDirectoryW(root.c_str(),&attributes)&&GetLastError()!=ERROR_ALREADY_EXISTS)ve::win(FALSE);ve::helper::verify_protected_file(root);
    if(!fs::is_empty(root)){
        ve::verify_product(root);const auto existing=ve::helper::read_metadata(root);if(existing.id!=metadata.id||existing.sid!=metadata.sid)throw std::runtime_error("Protected helper belongs to another deployment");
        for(const auto& entry:fs::directory_iterator(root))if(entry.path().filename()!=L"VolumeEditBroker.exe"&&entry.path().filename()!=L"product.id"&&entry.path().filename()!=L"owner.txt")throw std::runtime_error("Protected helper contains unknown files");
    }
    ve::helper::ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT|SC_MANAGER_CREATE_SERVICE));if(!manager.value)ve::win(FALSE);
    ve::helper::ServiceHandle existing(OpenServiceW(manager,ve::helper::service_name(metadata.id).c_str(),SERVICE_ALL_ACCESS));
    if(existing.value){ve::helper::verify_config(existing,metadata.id);stop(existing);}else if(GetLastError()!=ERROR_SERVICE_DOES_NOT_EXIST)ve::win(FALSE);
    write_owned(root/L"owner.txt",ve::helper::metadata_text(metadata));write_owned(root/L"product.id",ve::ProductId);
    const auto binary=root/L"VolumeEditBroker.exe";copy_owned(fs::path(metadata.root)/L"VolumeEditBroker.exe",binary);ve::helper::verify_protected_file(binary);
    if(existing.value){ve::win(ChangeServiceConfigW(existing,SERVICE_NO_CHANGE,automatic?SERVICE_AUTO_START:SERVICE_DEMAND_START,SERVICE_NO_CHANGE,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr));}
    const auto display=L"VolumeEdit 权限辅助 "+metadata.id;
    ve::helper::ServiceHandle created(existing.value?nullptr:CreateServiceW(manager,ve::helper::service_name(metadata.id).c_str(),display.c_str(),SERVICE_ALL_ACCESS,SERVICE_WIN32_OWN_PROCESS,automatic?SERVICE_AUTO_START:SERVICE_DEMAND_START,SERVICE_ERROR_NORMAL,ve::helper::command(metadata.id).c_str(),nullptr,nullptr,nullptr,nullptr,nullptr));
    const auto service=existing.value?existing.value:created.value;if(!service)ve::win(FALSE);
    auto service_sd=ve::helper::security(L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;CCLCRP;;;"+metadata.sid+L")");PSID owner=nullptr;PACL acl=nullptr;BOOL ignored=FALSE,present=FALSE;
    ve::win(GetSecurityDescriptorOwner(service_sd.value,&owner,&ignored));ve::win(GetSecurityDescriptorDacl(service_sd.value,&present,&acl,&ignored));
    const auto error=SetSecurityInfo(service,SE_SERVICE,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,owner,nullptr,acl,nullptr);if(error)throw ve::Failure(HRESULT_FROM_WIN32(error));
    if(!StartServiceW(service,0,nullptr)&&GetLastError()!=ERROR_SERVICE_ALREADY_RUNNING)ve::win(FALSE);wait_state(service,SERVICE_RUNNING);
}
void remove(const ve::helper::Metadata& metadata){
    const auto root=ve::helper::protected_root(metadata.id);ve::refuse_reparse(root);
    if(fs::exists(root)){ve::helper::verify_protected_file(root);if(!fs::is_empty(root)){
        ve::verify_product(root);ve::helper::check_metadata(ve::helper::read_metadata(root),metadata);
        for(const auto& entry:fs::directory_iterator(root))if(entry.path().filename()!=L"VolumeEditBroker.exe"&&entry.path().filename()!=L"product.id"&&entry.path().filename()!=L"owner.txt")throw std::runtime_error("Unknown helper files preserved; remove them before retrying cleanup");
    }}
    {
        ve::helper::ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));if(!manager.value)ve::win(FALSE);
        ve::helper::ServiceHandle service(OpenServiceW(manager,ve::helper::service_name(metadata.id).c_str(),SERVICE_STOP|SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG|DELETE));
        if(service.value){ve::helper::verify_config(service,metadata.id);if(!fs::exists(root))throw std::runtime_error("Cannot verify missing service files");stop(service);ve::win(DeleteService(service));}
        else if(GetLastError()!=ERROR_SERVICE_DOES_NOT_EXIST)ve::win(FALSE);
    }
    // Confirm SCM removal before deleting files; another open handle can defer it.
    const auto deadline=GetTickCount64()+10000;bool removed=false;
    while(GetTickCount64()<deadline){ve::helper::ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));if(!manager.value)ve::win(FALSE);
        ve::helper::ServiceHandle service(OpenServiceW(manager,ve::helper::service_name(metadata.id).c_str(),SERVICE_QUERY_STATUS));if(!service.value&&GetLastError()==ERROR_SERVICE_DOES_NOT_EXIST){removed=true;break;}Sleep(50);}
    if(!removed)throw ve::Failure(HRESULT_FROM_WIN32(ERROR_SERVICE_MARKED_FOR_DELETE));
    if(fs::exists(root)){
        for(const auto* name:{L"VolumeEditBroker.exe",L"owner.txt",L"product.id"}){const auto file=root/name;ve::refuse_reparse(file);fs::remove(file);}
        if(!fs::is_empty(root))throw std::runtime_error("Unknown helper files preserved; cleanup incomplete");fs::remove(root);
    }
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);if(!arguments)return ERROR_INVALID_PARAMETER;
    std::vector<std::wstring> args;for(int i=0;i<count;++i)args.emplace_back(arguments[i]);LocalFree(arguments);
    try{
        if(count==2&&args[1]==L"--pipe-smoke")return pipe_smoke();
        if(count==3&&args[1]==L"--service"){
            identity=args[2];if(!ve::helper::valid_guid(identity))return ERROR_INVALID_PARAMETER;
            stop_event.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!stop_event)ve::win(FALSE);
            auto name=ve::helper::service_name(identity);SERVICE_TABLE_ENTRYW table[]={{name.data(),service_main},{nullptr,nullptr}};ve::win(StartServiceCtrlDispatcherW(table));return 0;
        }
        if((count==5&&args[1]==L"--install")||(count==4&&args[1]==L"--remove")){
            ve::Apartment apartment;wchar_t* end=nullptr;const auto pid=std::wcstoul(args[3].c_str(),&end,10);if(!pid||*end)throw std::runtime_error("Invalid installer caller");
            const auto root=fs::absolute(args[2]).lexically_normal();const auto metadata=installer_context(root,pid,args[1]==L"--install");
            if(args[1]==L"--install"){if(args[4]!=L"0"&&args[4]!=L"1")throw std::runtime_error("Invalid startup mode");install(metadata,args[4]==L"1");}else remove(metadata);return 0;
        }
        return ERROR_INVALID_PARAMETER;
    }catch(const ve::Failure& f){if(count<2||args[1]!=L"--pipe-smoke")MessageBoxW(nullptr,ve::error_text(static_cast<DWORD>(f.code)).c_str(),L"VolumeEdit 权限辅助未完成",MB_OK|MB_ICONERROR);return (static_cast<DWORD>(f.code)&0xffff)?(static_cast<DWORD>(f.code)&0xffff):ERROR_GEN_FAILURE;}
    catch(const std::exception& e){if(count<2||args[1]!=L"--pipe-smoke")MessageBoxW(nullptr,ve::wide(e.what()).c_str(),L"VolumeEdit 权限辅助未完成",MB_OK|MB_ICONERROR);return ERROR_INVALID_DATA;}
}
