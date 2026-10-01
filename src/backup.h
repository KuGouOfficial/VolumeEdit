#pragma once
#include "common.h"
#include <map>
#include <vector>

namespace ve {
struct VolumeRecord {float original{},applied{},pending_from=-1;};
using VolumeKey=std::pair<std::wstring,std::wstring>; // endpoint + stable ID '\n' instance ID
using VolumeRecords=std::map<VolumeKey,VolumeRecord>;
VolumeRecords parse_records(const std::string& text);
std::string serialize_records(const VolumeRecords& records);
VolumeRecords load_records();
void save_records(const VolumeRecords& records);
bool same_volume(float left,float right) noexcept;
bool record_matches(float current,const VolumeRecord& record) noexcept;
VolumeRecord adjusted_volume(float current,int gain_db_x10,const VolumeRecord* record);
void reconcile_records(VolumeRecords& records,const std::vector<VolumeKey>& active);
bool conflicting_alias(const VolumeRecords& records,const VolumeKey& key);
void atomic_text(const std::filesystem::path& path,const std::string& text);
}
