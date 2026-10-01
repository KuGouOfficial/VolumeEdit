#include "backup.h"
#include <iostream>
#include <vector>
int main(){
    ve::VolumeRecords records;records[{L"耳机",L"app\ninstance"}]={0.8f,0.04f};
    const auto text=ve::serialize_records(records);const auto parsed=ve::parse_records(text);
    if(parsed.size()!=1||!ve::same_volume(parsed.begin()->second.original,0.8f)||parsed.begin()->first!=records.begin()->first)return 1;
    for(const std::string& bad:std::vector<std::string>{"bad","VE_VOLUME_BACKUP_1\n00\t61\t0.4\t0.3\n","VE_VOLUME_BACKUP_1\n61\t62\tnan\t0.3\n","VE_VOLUME_BACKUP_1\n61\t62\t1.1\t0.3\n",text+text.substr(text.find('\n')+1)}){
        try{ve::parse_records(bad);return 2;}catch(const std::exception&){}
    }
    const auto folder=ve::executable_directory()/L"backup-test";std::filesystem::create_directory(folder);ve::set_deployment_root(folder);std::filesystem::create_directory(ve::state_directory());
    ve::save_records(records);if(ve::load_records().size()!=1)return 3;ve::save_records({});if(std::filesystem::exists(ve::state_directory()/L"volumes.txt"))return 4;
    std::filesystem::remove(ve::state_directory());std::filesystem::remove(folder);
    auto adjusted=ve::adjusted_volume(0.8f,-200,nullptr);if(!ve::same_volume(adjusted.applied,0.08f))return 5;
    adjusted.pending_from=-1;const auto repeat=ve::adjusted_volume(adjusted.applied,-200,&adjusted);if(!ve::same_volume(repeat.applied,0.08f))return 6;
    const auto reset=ve::adjusted_volume(adjusted.applied,0,&adjusted);if(!ve::same_volume(reset.applied,0.8f)||!ve::record_matches(0.08f,reset)||!ve::record_matches(0.8f,reset))return 7;
    const auto external=ve::adjusted_volume(0.6f,-200,&adjusted);if(!ve::same_volume(external.original,0.6f)||!ve::same_volume(external.applied,0.06f)||ve::record_matches(0.6f,adjusted))return 8;
    records.clear();const ve::VolumeKey a{L"e",L"app\na"},b{L"e",L"app\nb"},c{L"e",L"app\nc"};records[a]=adjusted;
    ve::reconcile_records(records,{b,c});if(!records.contains(a)||!ve::conflicting_alias(records,b))return 9;
    ve::reconcile_records(records,{b});if(records.contains(a)||!records.contains(b)||ve::conflicting_alias(records,b))return 10;
    records[a]={0.2f,0.02f};ve::reconcile_records(records,{c});if(records.contains(c)||!ve::conflicting_alias(records,c))return 11;
    records.clear();records[a]=reset;const auto recovery=ve::parse_records(ve::serialize_records(records)).at(a);if(!ve::record_matches(0.08f,recovery)||!ve::record_matches(0.8f,recovery))return 12;
    auto boosted=ve::adjusted_volume(0.001f,400,nullptr);if(!ve::same_volume(boosted.applied,0.1f))return 13;
    boosted.pending_from=-1;if(!ve::same_volume(ve::adjusted_volume(boosted.applied,400,&boosted).applied,0.1f))return 14;
    auto capped=ve::adjusted_volume(0.8f,400,nullptr);if(capped.applied!=1.0f||capped.original!=0.8f)return 15;
    capped.pending_from=-1;if(ve::adjusted_volume(1.0f,0,&capped).applied!=0.8f||!ve::same_volume(ve::adjusted_volume(1.0f,-200,&capped).applied,0.08f))return 16;
    if(ve::adjusted_volume(0,400,nullptr).applied!=0||!ve::same_volume(ve::adjusted_volume(1,-400,nullptr).applied,0.01f))return 17;
    for(const int gain:{-401,401})try{ve::adjusted_volume(0.5f,gain,nullptr);return 18;}catch(const std::exception&){}
    std::cout<<"Backup: Unicode keys, exact instance identity, invalid input, duplicate receipts, atomic storage passed.\n";
}
