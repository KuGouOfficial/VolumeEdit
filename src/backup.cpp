#include "backup.h"
#include "portable.h"
#include <fstream>
#include <sstream>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <algorithm>

namespace ve {
bool same_volume(float a,float b) noexcept{return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<0.000001f;}
bool record_matches(float current,const VolumeRecord& record) noexcept{return same_volume(current,record.applied)||(record.pending_from>=0&&same_volume(current,record.pending_from));}
VolumeRecord adjusted_volume(float current,int gain_db_x10,const VolumeRecord* record){
    if(!std::isfinite(current)||current<0||current>1||gain_db_x10 < MinGain||gain_db_x10>MaxGain)throw std::invalid_argument("Invalid session volume adjustment");
    const float baseline=record&&record_matches(current,*record)?record->original:current;
    return {baseline,std::clamp(baseline*std::pow(10.0f,gain_db_x10/200.0f),0.0f,1.0f),current};
}
static std::wstring stable(const VolumeKey& key){return key.second.substr(0,key.second.find(L'\n'));}
bool conflicting_alias(const VolumeRecords& records,const VolumeKey& key){
    return !records.contains(key)&&std::any_of(records.begin(),records.end(),[&](const auto& pair){return pair.first.first==key.first&&stable(pair.first)==stable(key);});
}
void reconcile_records(VolumeRecords& records,const std::vector<VolumeKey>& active){
    for(const auto& key:active){
        if(records.contains(key))continue;
        if(std::count_if(active.begin(),active.end(),[&](const VolumeKey& k){return k.first==key.first&&stable(k)==stable(key);})!=1)continue;
        std::vector<VolumeKey> aliases;VolumeRecord value{};bool agree=true;
        for(const auto& [k,record]:records){
            if(k.first!=key.first||stable(k)!=stable(key))continue;
            if(!aliases.empty()&&(!same_volume(value.original,record.original)||!same_volume(value.applied,record.applied)||!same_volume(value.pending_from,record.pending_from)))agree=false;
            value=record;aliases.push_back(k);
        }
        if(agree&&!aliases.empty()){for(const auto& k:aliases)records.erase(k);records[key]=value;}
    }
}
VolumeRecords merge_legacy_records(const VolumeRecords& latest,const VolumeRecords& earlier){
    auto result=latest;
    for(const auto&[key,old]:earlier){
        if(result.contains(key)){
            const auto& current=result.at(key);
            if(!same_volume(current.original,old.original)||!same_volume(current.applied,old.applied)||!same_volume(current.pending_from,old.pending_from))throw std::runtime_error("Conflicting recovery records for the same instance");
            continue;
        }
        auto selected=result.end();unsigned aliases=0;
        for(auto it=result.begin();it!=result.end();++it)if(it->first.first==key.first&&stable(it->first)==stable(key)){++aliases;selected=it;}
        const auto old_count=std::count_if(earlier.begin(),earlier.end(),[&](const auto& pair){return pair.first.first==key.first&&stable(pair.first)==stable(key);});
        if(aliases==1&&old_count==1&&old.pending_from<0&&selected->second.pending_from<0&&same_volume(selected->second.original,old.applied))selected->second.original=old.original;
        else result.emplace(key,old);
    }return result;
}
void atomic_text(const std::filesystem::path& path,const std::string& text){
    const auto temporary=path.wstring()+L".tmp";
    refuse_reparse(path);refuse_reparse(temporary);
    Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr));if(!file)win(FALSE);
    DWORD count=0;win(WriteFile(file.get(),text.data(),static_cast<DWORD>(text.size()),&count,nullptr));if(count!=text.size())throw Failure(E_FAIL);
    win(FlushFileBuffers(file.get()));file.reset();win(MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH));
}
static std::string hex(const std::wstring& text){
    std::string out;const char digits[]="0123456789abcdef";
    for(unsigned char c:utf8(text)){out+=digits[c>>4];out+=digits[c&15];}return out;
}
static std::wstring unhex(const std::string& text){
    if(text.empty()||text.size()>65536||text.size()%2)throw std::invalid_argument("Invalid volume backup identity");
    auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::invalid_argument("Invalid volume backup encoding");};
    std::string bytes;for(size_t i=0;i<text.size();i+=2)bytes+=static_cast<char>((digit(text[i])<<4)|digit(text[i+1]));
    auto out=wide(bytes);if(out.find(L'\0')!=std::wstring::npos)throw std::invalid_argument("Invalid volume backup identity");return out;
}
static float number(const std::string& text){
    float value=0;const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size()||!std::isfinite(value)||value<0||value>1)throw std::invalid_argument("Invalid volume backup level");return value;
}
VolumeRecords parse_records(const std::string& text){
    if(text.size()>1024*1024)throw std::invalid_argument("Volume backup is too large");
    std::istringstream stream(text);std::string line;if(!std::getline(stream,line)||(line!="VE_VOLUME_BACKUP_1"&&line!="VE_VOLUME_BACKUP_2"))throw std::invalid_argument("Invalid volume backup header");
    const int count=line=="VE_VOLUME_BACKUP_2"?5:4;
    VolumeRecords out;
    while(std::getline(stream,line)){
        std::string fields[5];size_t begin=0;
        for(int i=0;i<count-1;++i){const auto end=line.find('\t',begin);if(end==std::string::npos)throw std::invalid_argument("Invalid volume backup row");fields[i]=line.substr(begin,end-begin);begin=end+1;}
        fields[count-1]=line.substr(begin);VolumeKey key{unhex(fields[0]),unhex(fields[1])};
        const float pending=count==4||fields[4]=="-1"?-1:number(fields[4]);
        if(!out.emplace(std::move(key),VolumeRecord{number(fields[2]),number(fields[3]),pending}).second||out.size()>1024)throw std::invalid_argument("Duplicate or excessive volume backups");
    }return out;
}
std::string serialize_records(const VolumeRecords& records){
    std::ostringstream out;out.imbue(std::locale::classic());out<<"VE_VOLUME_BACKUP_2\n"<<std::setprecision(9);
    for(const auto& [key,value]:records)out<<hex(key.first)<<'\t'<<hex(key.second)<<'\t'<<value.original<<'\t'<<value.applied<<'\t'<<value.pending_from<<'\n';
    const auto text=out.str();parse_records(text);return text;
}
VolumeRecords load_records(){
    const auto path=state_directory()/L"volumes.txt";refuse_reparse(path);if(!std::filesystem::exists(path))return {};
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Cannot read volume backups");
    std::string text(1024*1024+1,'\0');input.read(text.data(),static_cast<std::streamsize>(text.size()));text.resize(static_cast<size_t>(input.gcount()));return parse_records(text);
}
void save_records(const VolumeRecords& records){
    const auto path=state_directory()/L"volumes.txt";
    refuse_reparse(path);
    if(records.empty()){std::error_code error;std::filesystem::remove(path,error);if(error)throw std::system_error(error);return;}
    atomic_text(path,serialize_records(records));
}
}
