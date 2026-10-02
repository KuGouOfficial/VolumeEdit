#include "endpoint_backup.h"
#include "backup.h"
#include "portable.h"
#include <fstream>
#include <sstream>
#include <charconv>
#include <iomanip>
#include <cmath>
#include <algorithm>
namespace ve {
bool same_db(float a,float b) noexcept{return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<0.02f;}
bool endpoint_matches(float current,const EndpointRecord& r) noexcept{return same_db(current,r.applied)||same_db(current,r.pending_from);}
float endpoint_target(float baseline,int offset,float minimum,float maximum){
    if(!std::isfinite(baseline)||!std::isfinite(minimum)||!std::isfinite(maximum)||minimum>maximum||offset<MinGain||offset>MaxGain)throw std::invalid_argument("Invalid endpoint volume target");
    return std::clamp(baseline+offset/10.0f,minimum,maximum);
}
static std::string encode(const std::wstring& value){std::string result;for(unsigned char c:utf8(value)){result+="0123456789abcdef"[c>>4];result+="0123456789abcdef"[c&15];}return result;}
static std::wstring decode(const std::string& value){
    if(value.empty()||value.size()>65536||value.size()%2)throw std::invalid_argument("Invalid endpoint identity");
    auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::invalid_argument("Invalid endpoint identity");};
    std::string result;for(size_t i=0;i<value.size();i+=2)result+=static_cast<char>((digit(value[i])<<4)|digit(value[i+1]));auto text=wide(result);
    if(text.find(L'\0')!=std::wstring::npos)throw std::invalid_argument("Invalid endpoint identity");return text;
}
static float number(const std::string& text){float value=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!std::isfinite(value)||std::abs(value)>10000)throw std::invalid_argument("Invalid endpoint dB");return value;
}
EndpointRecords parse_endpoint_records(const std::string& text){
    if(text.size()>1024*1024)throw std::invalid_argument("Endpoint backups too large");std::istringstream input(text);std::string row;EndpointRecords result;
    if(!std::getline(input,row)||row!="VE_ENDPOINT_BACKUP_1")throw std::invalid_argument("Invalid endpoint backups");
    while(std::getline(input,row)){std::string fields[4];size_t begin=0;for(int i=0;i<3;++i){auto end=row.find('\t',begin);if(end==std::string::npos)throw std::invalid_argument("Invalid endpoint row");fields[i]=row.substr(begin,end-begin);begin=end+1;}fields[3]=row.substr(begin);
        EndpointRecord record{number(fields[1]),number(fields[2])};if(fields[3]!="-")record.pending_from=number(fields[3]);
        if(!result.emplace(decode(fields[0]),record).second||result.size()>1024)throw std::invalid_argument("Duplicate or excessive endpoints");
    }return result;
}
std::string serialize_endpoint_records(const EndpointRecords& records){std::ostringstream out;out.imbue(std::locale::classic());out<<"VE_ENDPOINT_BACKUP_1\n"<<std::setprecision(9);
    for(const auto&[id,r]:records){out<<encode(id)<<'\t'<<r.original<<'\t'<<r.applied<<'\t';if(std::isnan(r.pending_from))out<<'-';else out<<r.pending_from;out<<'\n';}auto text=out.str();parse_endpoint_records(text);return text;
}
EndpointRecords load_endpoint_records(){auto path=state_directory()/L"endpoint-volumes.txt";refuse_reparse(path);if(!std::filesystem::exists(path))return {};
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read endpoint backups");std::string text(1024*1024+1,'\0');in.read(text.data(),static_cast<std::streamsize>(text.size()));text.resize(static_cast<size_t>(in.gcount()));return parse_endpoint_records(text);
}
void save_endpoint_records(const EndpointRecords& records){auto path=state_directory()/L"endpoint-volumes.txt";refuse_reparse(path);if(records.empty()){std::filesystem::remove(path);return;}atomic_text(path,serialize_endpoint_records(records));}
}
