#pragma once
#include "common.h"
#include "backup.h"
#include "endpoint_backup.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
namespace ve {
struct Device {std::wstring id,name;};
std::vector<Device> devices();
std::wstring default_output_id();
std::wstring resolve_target(const std::vector<Device>& active,const std::wstring& current_default,const std::wstring& manual,bool automatic);
enum class AudioState:unsigned {Stopped,Waiting,Opening,Running,Error};
struct AudioStatus {AudioState state{};HRESULT error{};unsigned saved=1,pending_restore{},pending_endpoints{};bool limited{},external_change{};float current_db{},baseline_db{},minimum_db{},maximum_db{},increment_db{};};
unsigned restore_saved_volumes(); // Legacy session backups only.
unsigned restore_endpoint_volumes();
class AudioEngine {
public:
    AudioEngine();~AudioEngine();void start();void stop();void retry();
    bool configure(const Settings& settings,bool persist=true);
    Settings settings();AudioStatus status();
private:
    void worker();Handle stop_,reconnect_;std::thread thread_;std::mutex mutex_;Settings settings_;AudioStatus status_;
    std::atomic<unsigned> revision_{};bool ready_=true;
};
}
