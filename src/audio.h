#pragma once
#include "common.h"
#include "backup.h"
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
struct AudioStatus {AudioState state{};HRESULT error{};unsigned sessions{},saved=1,pending_restore{},capped_sessions{},unverified_sessions{},assisted_sessions{};};
// Restores only recorded changes that still match our last applied volume.
// Remaining records are retained if their device/session cannot be reached.
unsigned restore_saved_volumes();
class AudioEngine {
public:
    AudioEngine();~AudioEngine();
    void start();void stop();void retry();
    bool configure(const Settings& settings,bool persist=true);
    Settings settings();AudioStatus status();
private:
    void worker();
    Handle stop_,reconnect_;std::thread thread_;std::mutex mutex_;Settings settings_;
    std::atomic<AudioState> state_{AudioState::Stopped};std::atomic<HRESULT> error_{S_OK};
    std::atomic<unsigned> sessions_{},pending_{},capped_{},unverified_{},assisted_{};std::atomic<bool> saved_{true},ready_{true};
};
}
