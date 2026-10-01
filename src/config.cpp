#include "common.h"
#include "portable.h"
#include "backup.h"
#include <shlobj.h>
#include <fstream>
#include <map>
#include <variant>
#include <charconv>
#include <sstream>

namespace ve {
static std::filesystem::path root_override;
std::filesystem::path deployment_root(){return root_override.empty()?executable_directory():root_override;}
void set_deployment_root(const std::filesystem::path& root){root_override=std::filesystem::absolute(root);}
std::string utf8(const std::wstring& s) {
    if(s.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    if(!n) throw Failure(HRESULT_FROM_WIN32(GetLastError()));
    std::string out(n,'\0'); WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr); return out;
}
std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n) throw Failure(HRESULT_FROM_WIN32(GetLastError()));
    std::wstring out(n,L'\0'); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n); return out;
}
std::filesystem::path state_directory() {
    return deployment_root()/L"state";
}
bool valid(const Settings& s) noexcept {
    return s.gain_db_x10>=MinGain && s.gain_db_x10<=MaxGain && s.source.size()<512 && s.target.size()<512
        && (s.source.empty() || s.source!=s.target) && s.source.find(L'\0')==std::wstring::npos && s.target.find(L'\0')==std::wstring::npos;
}
static std::string quote(const std::wstring& text) {
    std::string out="\"";
    for(unsigned char c:utf8(text)) {
        if(c=='"' || c=='\\') {out+='\\'; out+=static_cast<char>(c);}
        else if(c<32) { const char* hex="0123456789abcdef"; out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else out+=static_cast<char>(c);
    }
    return out+'"';
}
std::string serialize(const Settings& s) {
    if(!valid(s)) throw std::invalid_argument("Invalid settings");
    return "{\n  \"schema_version\": 3,\n  \"gain_db_x10\": "+std::to_string(s.gain_db_x10)
        +",\n  \"muted\": "+(s.muted?"true":"false")+",\n  \"source_endpoint_id\": "+quote(s.source)
        +",\n  \"automatic_target\": "+(s.automatic_target?"true":"false")
        +",\n  \"target_endpoint_id\": "+quote(s.target)+"\n}\n";
}
// Small, strict parser for the flat product schema. Reject duplicates, unknown
// fields, wrong types, trailing input, malformed numbers and Unicode escapes.
class Parser {
    const std::string& s; std::size_t pos{};
    [[noreturn]] void bad(){ throw std::invalid_argument("Malformed settings JSON"); }
public:
    explicit Parser(const std::string& text):s(text){ if(s.size()>16384) bad(); }
    void ws(){while(pos<s.size() && (s[pos]==' '||s[pos]=='\n'||s[pos]=='\r'||s[pos]=='\t')) ++pos;}
    bool take(char c){ws(); if(pos<s.size()&&s[pos]==c){++pos;return true;}return false;}
    void need(char c){if(!take(c))bad();}
    std::wstring string(){
        need('"'); std::string bytes;
        while(pos<s.size()){
            const unsigned char c=s[pos++];
            if(c=='"') return wide(bytes);
            if(c<32) bad();
            if(c!='\\'){bytes+=static_cast<char>(c);continue;}
            if(pos==s.size())bad(); const char e=s[pos++];
            if(e=='"'||e=='\\'||e=='/')bytes+=e;
            else if(e=='b')bytes+='\b'; else if(e=='f')bytes+='\f'; else if(e=='n')bytes+='\n';else if(e=='r')bytes+='\r';else if(e=='t')bytes+='\t';
            else if(e=='u'){
                unsigned v=hex4(); std::wstring decoded;
                if(v>=0xD800&&v<=0xDBFF){ if(pos+2>s.size()||s[pos++]!='\\'||s[pos++]!='u')bad(); const auto low=hex4(); if(low<0xDC00||low>0xDFFF)bad(); decoded+=static_cast<wchar_t>(v);decoded+=static_cast<wchar_t>(low);}
                else {if(v>=0xDC00&&v<=0xDFFF)bad();decoded+=static_cast<wchar_t>(v);}
                bytes+=utf8(decoded);
            }else bad();
        } bad();
    }
    unsigned hex4(){unsigned v=0;for(int i=0;i<4;++i){if(pos==s.size())bad();char c=s[pos++];unsigned n;if(c>='0'&&c<='9')n=c-'0';else if(c>='a'&&c<='f')n=c-'a'+10;else if(c>='A'&&c<='F')n=c-'A'+10;else bad();v=v*16+n;}return v;}
    int number(){ws();const auto start=pos;if(pos<s.size()&&s[pos]=='-')++pos;const auto digits=pos;while(pos<s.size()&&s[pos]>='0'&&s[pos]<='9')++pos;if(pos==digits || (pos-digits>1&&s[digits]=='0'))bad();int v{};const auto r=std::from_chars(s.data()+start,s.data()+pos,v);if(r.ec!=std::errc{})bad();return v;}
    bool boolean(){ws();if(s.compare(pos,4,"true")==0){pos+=4;return true;}if(s.compare(pos,5,"false")==0){pos+=5;return false;}bad();}
    void end(){ws();if(pos!=s.size())bad();}
};
Settings parse_settings(const std::string& text) {
    Parser p(text); p.need('{'); Settings out; unsigned seen=0;int version=0;
    do {
        const auto key=p.string(); p.need(':'); unsigned bit=0;
        if(key==L"schema_version"){bit=1;version=p.number();if(version!=1&&version!=2&&version!=3)throw std::invalid_argument("Unsupported schema");}
        else if(key==L"gain_db_x10"){bit=2;out.gain_db_x10=p.number();}
        else if(key==L"muted"){bit=4;out.muted=p.boolean();}
        else if(key==L"source_endpoint_id"){bit=8;out.source=p.string();}
        else if(key==L"target_endpoint_id"){bit=16;out.target=p.string();}
        else if(key==L"automatic_target"){bit=32;out.automatic_target=p.boolean();}
        else throw std::invalid_argument("Unknown settings field");
        if(seen&bit)throw std::invalid_argument("Duplicate field"); seen|=bit;
    }while(p.take(','));
    p.need('}');p.end();
    // Older portable builds allowed attenuation down to -80 dB. Migrate a
    // valid old value to the new minimum rather than losing the saved setup.
    if(version<3&&out.gain_db_x10>=-800&&out.gain_db_x10<MinGain)out.gain_db_x10=MinGain;
    if(seen!=(version==1?31u:63u)||!valid(out))throw std::invalid_argument("Invalid settings");
    if(version==1)out.automatic_target=false; // Preserve the previous user's explicit routing.
    return out;
}
Settings load_settings(const std::filesystem::path& path) {
    refuse_reparse(path);
    std::ifstream in(path,std::ios::binary); if(!in)throw std::runtime_error("Settings missing");
    std::string data(16385,'\0');in.read(data.data(),static_cast<std::streamsize>(data.size()));data.resize(static_cast<std::size_t>(in.gcount()));return parse_settings(data);
}
void save_settings(const std::filesystem::path& path,const Settings& s) {
    atomic_text(path,serialize(s));
}
} // namespace ve
