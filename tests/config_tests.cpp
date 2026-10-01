#include "common.h"
#include <iostream>
#include <vector>
int main(){
    ve::Settings s;s.source=L"virtual\\\"中文";s.target=L"headphones";
    const auto parsed=ve::parse_settings(ve::serialize(s));
    if(parsed.source!=s.source||parsed.target!=s.target||parsed.gain_db_x10!=0||!parsed.automatic_target)return 1;
    for(const std::string value:{"{}","[]","{\"schema_version\":2}","{\"schema_version\":1,\"schema_version\":1}"}){
        try{ve::parse_settings(value);return 2;}catch(const std::exception&){}
    }
    s.gain_db_x10=-240;
    auto text=ve::serialize(s);text.replace(text.find("-240"),4,"401");try{ve::parse_settings(text);return 3;}catch(const std::exception&){}
    text=ve::serialize(s)+"false";try{ve::parse_settings(text);return 4;}catch(const std::exception&){}
    text=ve::serialize(s);text.replace(text.find("-240"),4,"-0240");try{ve::parse_settings(text);return 5;}catch(const std::exception&){}
    const auto path=ve::executable_directory()/L"config-test.json";
    ve::save_settings(path,s);if(ve::load_settings(path).source!=s.source)return 6;
    std::filesystem::remove(path);
    s.automatic_target=false;if(ve::parse_settings(ve::serialize(s)).automatic_target)return 7;
    text=ve::serialize(s);text.replace(text.find("\"schema_version\": 3"),19,"\"schema_version\": 1");
    const auto field=text.find("  \"automatic_target\"");const auto end=text.find('\n',field);text.erase(field,end-field+1);
    const auto legacy=ve::parse_settings(text);if(legacy.automatic_target||legacy.gain_db_x10!=-240||legacy.target!=s.target)return 8;
    text=ve::serialize(s);const auto mode=text.find("\"automatic_target\": false");text.replace(mode,25,"\"automatic_target\": 1");try{ve::parse_settings(text);return 9;}catch(const std::exception&){}
    s.gain_db_x10=400;ve::save_settings(path,s);if(ve::load_settings(path).gain_db_x10!=400)return 10;std::filesystem::remove(path);
    text=ve::serialize(s);text.replace(text.find("\"gain_db_x10\": 400"),18,"\"gain_db_x10\": -800");
    try{ve::parse_settings(text);return 11;}catch(const std::exception&){}
    text.replace(text.find("\"schema_version\": 3"),19,"\"schema_version\": 2");if(ve::parse_settings(text).gain_db_x10!=-400)return 12;
    std::cout<<"Config: Unicode, strict schema, malformed input, range and atomic save passed\n";
}
