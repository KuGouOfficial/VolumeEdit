#pragma once
#include "permission_helper.h"
#include "portable.h"
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <fstream>
#include <vector>
#include <array>

namespace ve::helper {
inline constexpr DWORD Magic=0x56455048, Protocol=1;
enum class Operation:DWORD {Owner=1, Startup=2};
struct Request {DWORD magic=Magic,version=Protocol,operation{},pid{},value{},reserved{};std::uint64_t creation{};};
struct Reply {DWORD magic=Magic,version=Protocol,error{},owner{};};
static_assert(sizeof(Request)==32 && sizeof(Reply)==16);
struct ServiceHandle {
    SC_HANDLE value{};
    explicit ServiceHandle(SC_HANDLE h=nullptr):value(h){}
    ~ServiceHandle(){if(value)CloseServiceHandle(value);}
    ServiceHandle(const ServiceHandle&)=delete;
    operator SC_HANDLE() const{return value;}
};
struct LocalMemory {
    void* value{};
    ~LocalMemory(){if(value)LocalFree(value);}
};
inline std::vector<BYTE> token_user(HANDLE token){
    DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);
    if(!size)win(FALSE);std::vector<BYTE> bytes(size);win(GetTokenInformation(token,TokenUser,bytes.data(),size,&size));return bytes;
}
inline std::wstring token_sid(HANDLE token){
    const auto bytes=token_user(token);LPWSTR text=nullptr;
    win(ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(bytes.data())->User.Sid,&text));
    LocalMemory memory{text};return text;
}
inline std::wstring current_sid(){HANDLE value=nullptr;win(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&value));Handle token(value);return token_sid(token.get());}
inline bool valid_guid(const std::wstring& id){GUID value{};return id.size()==38&&SUCCEEDED(CLSIDFromString(id.c_str(),&value));}
inline std::wstring service_name(const std::wstring& id){if(!valid_guid(id))throw std::runtime_error("Invalid helper identity");return L"VolumeEdit.Permission."+id;}
inline std::wstring pipe_name(const std::wstring& id){return L"\\\\.\\pipe\\"+service_name(id);}
inline std::filesystem::path protected_root(const std::wstring& id){
    if(!valid_guid(id))throw std::runtime_error("Invalid helper identity");PWSTR path=nullptr;
    hr(SHGetKnownFolderPath(FOLDERID_ProgramFiles,KF_FLAG_DEFAULT,nullptr,&path));
    const std::filesystem::path root=std::filesystem::path(path)/(L"VolumeEditPermission-"+id.substr(1,36));CoTaskMemFree(path);return root;
}
inline std::wstring command(const std::wstring& id){return L"\""+(protected_root(id)/L"VolumeEditBroker.exe").wstring()+L"\" --service "+id;}
inline std::wstring quote_argument(const std::wstring& text){
    std::wstring out=L"\"";unsigned slashes=0;
    for(const auto ch:text){if(ch==L'\\'){++slashes;continue;}if(ch==L'\"'){out.append(slashes*2+1,L'\\');out+=ch;}else{out.append(slashes,L'\\');out+=ch;}slashes=0;}
    out.append(slashes*2,L'\\');out+=L'\"';return out;
}
inline std::uint64_t creation_time(HANDLE process){FILETIME created{},exited{},kernel{},user{};win(GetProcessTimes(process,&created,&exited,&kernel,&user));return (static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;}
struct Metadata {std::wstring id,sid,root;};
inline std::string metadata_text(const Metadata& m){return "VE_PERMISSION_HELPER_1\n"+utf8(m.id)+"\n"+utf8(m.sid)+"\n"+utf8(m.root)+"\n";}
inline Metadata read_metadata(const std::filesystem::path& root){
    refuse_reparse(root/L"owner.txt");std::ifstream input(root/L"owner.txt",std::ios::binary);if(!input)throw std::runtime_error("Cannot verify helper ownership");
    std::array<std::string,4> lines;for(auto& line:lines)if(!std::getline(input,line)||line.size()>32768)throw std::runtime_error("Invalid helper ownership");
    std::string extra;if(std::getline(input,extra)||lines[0]!="VE_PERMISSION_HELPER_1")throw std::runtime_error("Invalid helper ownership");
    Metadata m{wide(lines[1]),wide(lines[2]),wide(lines[3])};if(!valid_guid(m.id)||m.root.empty())throw std::runtime_error("Invalid helper ownership");
    LocalMemory sid;win(ConvertStringSidToSidW(m.sid.c_str(),&sid.value));return m;
}
inline void check_metadata(const Metadata& actual,const Metadata& wanted){
    if(actual.id!=wanted.id||actual.sid!=wanted.sid||_wcsicmp(actual.root.c_str(),wanted.root.c_str())!=0)throw std::runtime_error("Helper belongs to another deployment or user");
}
inline void verify_config(SC_HANDLE service,const std::wstring& id){
    DWORD size=0;QueryServiceConfigW(service,nullptr,0,&size);if(!size)win(FALSE);
    std::vector<BYTE> bytes(size);const auto config=reinterpret_cast<QUERY_SERVICE_CONFIGW*>(bytes.data());win(QueryServiceConfigW(service,config,size,&size));
    if(config->dwServiceType!=SERVICE_WIN32_OWN_PROCESS||_wcsicmp(config->lpBinaryPathName,command(id).c_str())!=0||_wcsicmp(config->lpServiceStartName,L"LocalSystem")!=0)throw std::runtime_error("Refusing an unrelated service");
}
inline LocalMemory security(const std::wstring& sddl){
    PSECURITY_DESCRIPTOR value=nullptr;win(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&value,nullptr));return LocalMemory{value};
}
inline void protect_file(const std::filesystem::path& path){
    auto descriptor=security(L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x001200a9;;;BU)");
    PSID owner=nullptr;PACL acl=nullptr;BOOL ignored=FALSE,present=FALSE;
    win(GetSecurityDescriptorOwner(descriptor.value,&owner,&ignored));win(GetSecurityDescriptorDacl(descriptor.value,&present,&acl,&ignored));
    const auto code=SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,owner,nullptr,acl,nullptr);
    if(code)throw Failure(HRESULT_FROM_WIN32(code));
}
inline void verify_protected_file(const std::filesystem::path& path){
    refuse_reparse(path);PSID owner=nullptr;PACL acl=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
    const auto code=GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,&descriptor);
    if(code)throw Failure(HRESULT_FROM_WIN32(code));LocalMemory memory{descriptor};
    BYTE admin[SECURITY_MAX_SID_SIZE],system[SECURITY_MAX_SID_SIZE],users[SECURITY_MAX_SID_SIZE];DWORD a=sizeof(admin),b=sizeof(system),c=sizeof(users);
    win(CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admin,&a));win(CreateWellKnownSid(WinLocalSystemSid,nullptr,system,&b));win(CreateWellKnownSid(WinBuiltinUsersSid,nullptr,users,&c));
    if(!owner||!EqualSid(owner,admin)||!acl||acl->AceCount!=3)throw std::runtime_error("Helper files are not protected against user writes");
    unsigned seen=0;for(DWORD i=0;i<acl->AceCount;++i){void* value=nullptr;win(GetAce(acl,i,&value));const auto ace=static_cast<const ACCESS_ALLOWED_ACE*>(value);
        if(ace->Header.AceType!=ACCESS_ALLOWED_ACE_TYPE)throw std::runtime_error("Unexpected helper file ACL");
        const auto sid=const_cast<DWORD*>(&ace->SidStart);
        if(EqualSid(sid,admin)&&ace->Mask==FILE_ALL_ACCESS)seen|=1;
        else if(EqualSid(sid,system)&&ace->Mask==FILE_ALL_ACCESS)seen|=2;
        else if(EqualSid(sid,users)&&ace->Mask==(FILE_GENERIC_READ|FILE_GENERIC_EXECUTE))seen|=4;
        else throw std::runtime_error("Unexpected helper file permissions");
    }if(seen!=7)throw std::runtime_error("Incomplete helper file permissions");
}
// Bounded overlapped I/O: a client cannot hold the service or the GUI forever.
inline DWORD transfer(HANDLE pipe,void* data,DWORD size,bool write,HANDLE stop=nullptr,DWORD timeout=1000){
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event)win(FALSE);OVERLAPPED operation{};operation.hEvent=event.get();DWORD count=0;
    const BOOL ok=write?WriteFile(pipe,data,size,&count,&operation):ReadFile(pipe,data,size,&count,&operation);
    if(!ok){const auto error=GetLastError();if(error!=ERROR_IO_PENDING)return error;
        const HANDLE events[]={event.get(),stop};const auto result=WaitForMultipleObjects(stop?2:1,events,FALSE,timeout);
        if(result!=WAIT_OBJECT_0){CancelIoEx(pipe,&operation);GetOverlappedResult(pipe,&operation,&count,TRUE);return result==WAIT_OBJECT_0+1?ERROR_OPERATION_ABORTED:ERROR_TIMEOUT;}
        if(!GetOverlappedResult(pipe,&operation,&count,FALSE))return GetLastError();
    }return count==size?ERROR_SUCCESS:ERROR_INVALID_DATA;
}
}
