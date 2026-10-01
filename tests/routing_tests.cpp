#include "audio.h"
#include <iostream>
int main(){
    const std::vector<ve::Device> active={{L"headphones",L"耳机"},{L"speakers",L"扬声器"}};
    if(ve::resolve_target(active,L"headphones",L"",true)!=L"headphones")return 1;
    if(ve::resolve_target(active,L"speakers",L"headphones",true)!=L"speakers")return 2;
    if(!ve::resolve_target(active,L"",L"headphones",true).empty())return 3;
    if(!ve::resolve_target(active,L"disconnected",L"headphones",true).empty())return 4;
    if(ve::resolve_target(active,L"speakers",L"headphones",false)!=L"headphones")return 5;
    if(!ve::resolve_target(active,L"speakers",L"disconnected",false).empty())return 6;
    if(!ve::resolve_target({},L"speakers",L"",true).empty())return 7;
    const ve::Settings defaults;if(defaults.gain_db_x10!=0||!defaults.automatic_target)return 8;
    ve::Settings s;for(const int gain:{-400,0,400}){s.gain_db_x10=gain;if(!ve::valid(s))return 9;}
    for(const int gain:{-401,401}){s.gain_db_x10=gain;if(ve::valid(s))return 10;}
    std::cout<<"Routing: default change, disconnection, manual override, portable gain bounds passed.\n";
}
