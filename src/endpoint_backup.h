#pragma once
#include "common.h"
#include <map>
#include <limits>
namespace ve {
struct EndpointRecord {float original{},applied{},pending_from=std::numeric_limits<float>::quiet_NaN();};
using EndpointRecords=std::map<std::wstring,EndpointRecord>;
bool same_db(float left,float right) noexcept;
bool endpoint_matches(float current,const EndpointRecord& record) noexcept;
float endpoint_target(float baseline,int offset,float minimum,float maximum);
EndpointRecords parse_endpoint_records(const std::string& text);
std::string serialize_endpoint_records(const EndpointRecords& records);
EndpointRecords load_endpoint_records();
void save_endpoint_records(const EndpointRecords& records);
}
