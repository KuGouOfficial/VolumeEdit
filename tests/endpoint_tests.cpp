#include "endpoint_backup.h"
#include "portable.h"
#include <iostream>
#include <cmath>
int main(){
    if(!ve::same_db(ve::endpoint_target(-21.6f,-100,-45,0),-31.6f))return 1;
    if(ve::endpoint_target(-21.6f,-400,-45,0)!=-45||ve::endpoint_target(-21.6f,400,-45,0)!=0)return 2;
    if(!ve::same_db(ve::endpoint_target(-21.6f,0,-45,0),-21.6f))return 3;
    for(int value:{-401,401})try{ve::endpoint_target(-20,value,-45,0);return 4;}catch(const std::exception&){}
    ve::EndpointRecord record{-21.6f,-31.6f,-21.6f};if(!ve::endpoint_matches(-31.6f,record)||!ve::endpoint_matches(-21.6f,record)||ve::endpoint_matches(-25,record))return 5;
    record.pending_from=std::numeric_limits<float>::quiet_NaN();if(ve::endpoint_matches(-21.6f,record))return 6;
    ve::EndpointRecords records;records[L"耳机/endpoint"]=record;auto text=ve::serialize_endpoint_records(records);auto parsed=ve::parse_endpoint_records(text);
    if(parsed.size()!=1||!ve::same_db(parsed.begin()->second.original,-21.6f)||!std::isnan(parsed.begin()->second.pending_from))return 7;
    for(auto bad:{std::string("bad"),text+text.substr(text.find('\n')+1),std::string("VE_ENDPOINT_BACKUP_1\n61\tnan\t-20\t-\n")})try{ve::parse_endpoint_records(bad);return 8;}catch(const std::exception&){}
    auto folder=ve::executable_directory()/L"endpoint-test";std::filesystem::create_directory(folder);ve::set_deployment_root(folder);std::filesystem::create_directory(ve::state_directory());
    ve::save_endpoint_records(records);if(ve::load_endpoint_records().size()!=1)return 9;ve::save_endpoint_records({});if(std::filesystem::exists(ve::state_directory()/L"endpoint-volumes.txt"))return 10;
    std::filesystem::remove(ve::state_directory());std::filesystem::remove(folder);
    std::cout<<"Endpoint: relative dB, hardware limits, interruption recovery, external edits, strict durable backups passed.\n";
}
