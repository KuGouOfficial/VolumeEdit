#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <propkeydef.h>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <utility>

namespace ve {
template<class T> using Com = Microsoft::WRL::ComPtr<T>;
inline constexpr wchar_t ServiceName[] = L"VolumeEditAudio";
// Private INF endpoint property; do not overwrite Windows' endpoint GUID.
inline constexpr PROPERTYKEY EndpointTag={{0x1435a175,0x7244,0x459d,{0x91,0x5a,0xc5,0x1a,0x14,0x34,0xa5,0xc0}},1};
inline constexpr wchar_t EndpointTagValue[]=L"VolumeEdit.Output.v1";
inline constexpr wchar_t HardwareId[] = L"ROOT\\VolumeEditVirtual";
inline std::wstring error_text(DWORD code) {
    wchar_t* text=nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr,code,0,reinterpret_cast<wchar_t*>(&text),0,nullptr);
    std::wstring result=text?text:L"Windows error"; if(text) LocalFree(text);
    return result+L" ("+std::to_wstring(code)+L")";
}
class Failure : public std::runtime_error {
public:
    HRESULT code;
    explicit Failure(HRESULT hr) : std::runtime_error("Windows operation failed"),code(hr) {}
};
inline void hr(HRESULT code) { if(FAILED(code)) throw Failure(code); }
inline void win(BOOL ok) { if(!ok) throw Failure(HRESULT_FROM_WIN32(GetLastError())); }
class Handle {
    HANDLE value_{};
public:
    Handle()=default;
    explicit Handle(HANDLE h):value_(h){}
    ~Handle(){ reset(); }
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
    Handle(Handle&& h) noexcept:value_(std::exchange(h.value_,nullptr)){}
    Handle& operator=(Handle&& h) noexcept { reset(std::exchange(h.value_,nullptr)); return *this; }
    void reset(HANDLE h=nullptr) noexcept { if(value_ && value_!=INVALID_HANDLE_VALUE) CloseHandle(value_); value_=h; }
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_!=INVALID_HANDLE_VALUE; }
};
struct Apartment {
    HRESULT result;
    Apartment():result(CoInitializeEx(nullptr,COINIT_MULTITHREADED)){ hr(result); }
    ~Apartment(){ CoUninitialize(); }
};
inline std::filesystem::path executable_directory(){
    wchar_t path[32768]; const DWORD n=GetModuleFileNameW(nullptr,path,32768);
    if(n==0 || n>=32768) throw Failure(HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
    return std::filesystem::path(path).parent_path();
}
std::filesystem::path deployment_root();
void set_deployment_root(const std::filesystem::path& root);
std::string utf8(const std::wstring& text);
std::wstring wide(const std::string& text);
std::filesystem::path state_directory();
struct Settings {
    int gain_db_x10=0;
    bool muted=false;
    bool automatic_target=true;
    std::wstring source, target;
};
inline constexpr int MinGain=-400,MaxGain=400;
bool valid(const Settings& settings) noexcept;
std::string serialize(const Settings& settings);
Settings parse_settings(const std::string& json);
Settings load_settings(const std::filesystem::path& path);
void save_settings(const std::filesystem::path& path,const Settings& settings);
} // namespace ve
