#include "audio.h"
#include "portable.h"
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace ve {
static const GUID Context={0x7cda93b4,0x456c,0x4d42,{0xa5,0x89,0xbb,0xb3,0x15,0x67,0xa6,0x94}};
static Com<IMMDeviceEnumerator> enumerator(){Com<IMMDeviceEnumerator> value;hr(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&value)));return value;}
std::vector<Device> devices(){
    auto e=enumerator();Com<IMMDeviceCollection> collection;hr(e->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&collection));UINT count=0;hr(collection->GetCount(&count));std::vector<Device> out;
    for(UINT i=0;i<count;++i){
        Com<IMMDevice> d;hr(collection->Item(i,&d));LPWSTR id=nullptr;hr(d->GetId(&id));Device item;item.id=id;CoTaskMemFree(id);
        Com<IPropertyStore> store;hr(d->OpenPropertyStore(STGM_READ,&store));PROPVARIANT name;PropVariantInit(&name);
        if(SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName,&name))&&name.vt==VT_LPWSTR)item.name=name.pwszVal;
        PropVariantClear(&name);out.push_back(std::move(item));
    }return out;
}
std::wstring default_output_id(){
    auto e=enumerator();Com<IMMDevice> d;const auto result=e->GetDefaultAudioEndpoint(eRender,eConsole,&d);if(result==E_NOTFOUND)return {};hr(result);
    LPWSTR id=nullptr;hr(d->GetId(&id));std::wstring out=id;CoTaskMemFree(id);return out;
}
std::wstring resolve_target(const std::vector<Device>& active,const std::wstring& current,const std::wstring& manual,bool automatic){
    const auto& id=automatic?current:manual;
    return std::any_of(active.begin(),active.end(),[&](const Device& d){return !id.empty()&&d.id==id;})?id:L"";
}
static bool our_user(IAudioSessionControl2* control){
    if(control->IsSystemSoundsSession()==S_OK)return true;
    DWORD pid=0;if(FAILED(control->GetProcessId(&pid))||!pid)return false;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));if(!process)return false;
    HANDLE a=nullptr,b=nullptr;if(!OpenProcessToken(process.get(),TOKEN_QUERY,&a))return false;Handle other(a);
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&b))return false;Handle self(b);
    DWORD sa=0,sb=0;GetTokenInformation(other.get(),TokenUser,nullptr,0,&sa);GetTokenInformation(self.get(),TokenUser,nullptr,0,&sb);
    std::vector<BYTE> ua(sa),ub(sb);
    if(!GetTokenInformation(other.get(),TokenUser,ua.data(),sa,&sa)||!GetTokenInformation(self.get(),TokenUser,ub.data(),sb,&sb))return false;
    return EqualSid(reinterpret_cast<TOKEN_USER*>(ua.data())->User.Sid,reinterpret_cast<TOKEN_USER*>(ub.data())->User.Sid)!=FALSE;
}
struct Session{VolumeKey key;std::wstring stable;Com<ISimpleAudioVolume> volume;};
static std::vector<Session> sessions(const std::wstring& endpoint){
    auto e=enumerator();Com<IMMDevice> device;hr(e->GetDevice(endpoint.c_str(),&device));Com<IAudioSessionManager2> manager;hr(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,&manager));
    Com<IAudioSessionEnumerator> list;hr(manager->GetSessionEnumerator(&list));int count=0;hr(list->GetCount(&count));std::vector<Session> out;
    for(int i=0;i<count;++i){
        Com<IAudioSessionControl> base;if(FAILED(list->GetSession(i,&base)))continue;Com<IAudioSessionControl2> control;if(FAILED(base.As(&control))||!our_user(control.Get()))continue;
        LPWSTR id=nullptr;if(FAILED(control->GetSessionIdentifier(&id)))continue;Session s;s.stable=id;CoTaskMemFree(id);if(s.stable.empty()||s.stable.find(L'\n')!=std::wstring::npos)continue;
        id=nullptr;if(FAILED(control->GetSessionInstanceIdentifier(&id)))continue;s.key={endpoint,s.stable+L"\n"+id};CoTaskMemFree(id);
        if(SUCCEEDED(base.As(&s.volume)))out.push_back(std::move(s));
    }return out;
}
static void reconcile(VolumeRecords& records,const std::vector<Session>& active){
    // Session identifiers can be shared by simultaneous process instances.
    // Restore exact instances first; migrate a closed instance only when all
    // matching backups agree and just one replacement instance is present.
    std::vector<VolumeKey> keys;for(const auto& session:active)keys.push_back(session.key);reconcile_records(records,keys);
}
unsigned restore_saved_volumes(){
    refuse_reparse(state_directory());auto records=load_records();if(records.empty())return 0;
    std::set<std::wstring> endpoints;for(const auto& [key,value]:records){(void)value;endpoints.insert(key.first);}
    for(const auto& endpoint:endpoints){
        try{const auto active=sessions(endpoint);reconcile(records,active);for(const auto& session:active){
            const auto found=records.find(session.key);if(found==records.end())continue;
            float current=0;if(FAILED(session.volume->GetMasterVolume(&current)))continue;
            // A later application/mixer change belongs to the user. Never undo it.
            if(!record_matches(current,found->second)||SUCCEEDED(session.volume->SetMasterVolume(found->second.original,&Context)))records.erase(found);
        }}catch(const Failure&){}
    }
    save_records(records);return static_cast<unsigned>(records.size());
}
AudioEngine::AudioEngine():stop_(CreateEventW(nullptr,TRUE,FALSE,nullptr)),reconnect_(CreateEventW(nullptr,FALSE,FALSE,nullptr)){
    if(!stop_||!reconnect_)win(FALSE);
    const auto file=state_directory()/L"settings.json";
    if(std::filesystem::exists(file))try{settings_=load_settings(file);settings_.muted=false;}catch(...){saved_=false;ready_=false;settings_.gain_db_x10=0;}
}
AudioEngine::~AudioEngine(){stop();}
void AudioEngine::start(){if(!thread_.joinable()){ResetEvent(stop_.get());thread_=std::thread(&AudioEngine::worker,this);}}
void AudioEngine::stop(){SetEvent(stop_.get());if(thread_.joinable())thread_.join();state_=AudioState::Stopped;}
void AudioEngine::retry(){SetEvent(reconnect_.get());}
Settings AudioEngine::settings(){std::lock_guard lock(mutex_);return settings_;}
AudioStatus AudioEngine::status(){return {state_.load(),error_.load(),sessions_.load(),saved_.load()?1u:0u,pending_.load(),capped_.load()};}
bool AudioEngine::configure(const Settings& input,bool persist){
    if(!valid(input))return false;Settings s=input;s.source.clear();s.muted=false;
    if(!s.automatic_target&&resolve_target(devices(),L"",s.target,false).empty())return false;
    std::lock_guard lock(mutex_);settings_=s;ready_=true;
    if(persist)try{save_settings(state_directory()/L"settings.json",s);saved_=true;}catch(...){saved_=false;}
    SetEvent(reconnect_.get());return true;
}
void AudioEngine::worker(){
    try{
        Apartment apartment;ensure_state();std::wstring previous;int previous_gain=1;
        const HANDLE events[]={stop_.get(),reconnect_.get()};
        while(WaitForSingleObject(stop_.get(),0)!=WAIT_OBJECT_0){
            if(!ready_){state_=AudioState::Waiting;error_=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);if(WaitForMultipleObjects(2,events,FALSE,50)==WAIT_OBJECT_0)break;continue;}
            try{
                const auto config=settings();const auto active=devices();const auto endpoint=resolve_target(active,config.automatic_target?default_output_id():L"",config.target,config.automatic_target);
                if(endpoint!=previous||(config.gain_db_x10==0&&previous_gain!=0)){pending_=restore_saved_volumes();previous=endpoint;}previous_gain=config.gain_db_x10;
                if(endpoint.empty()){state_=AudioState::Waiting;sessions_=0;capped_=0;error_=S_OK;}
                else{
                    auto records=load_records();const auto current_sessions=sessions(endpoint);reconcile(records,current_sessions);sessions_=static_cast<unsigned>(current_sessions.size());
                    struct Operation{Session session;float target;bool had_record;VolumeRecord before;};std::vector<Operation> operations;
                    bool changed=false;HRESULT failure=S_OK;unsigned capped=0;
                    for(const auto& session:current_sessions){
                        float current=0;if(FAILED(session.volume->GetMasterVolume(&current)))continue;
                        const auto old=records.find(session.key);const bool had=old!=records.end();const VolumeRecord before=had?old->second:VolumeRecord{};
                        if(conflicting_alias(records,session.key)){
                            failure=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);continue;
                        }
                        const auto adjustment=adjusted_volume(current,config.gain_db_x10,had?&before:nullptr);const float target=adjustment.applied;
                        if(config.gain_db_x10>0&&target>=1.0f)++capped;
                        if(same_volume(target,current)){if(had&&config.gain_db_x10==0){records.erase(session.key);changed=true;}continue;}
                        records[session.key]=adjustment;changed=true;operations.push_back({session,target,had,before});
                    }
                    if(changed)save_records(records); // Durable backup before touching any session.
                    for(const auto& operation:operations){
                        const auto result=operation.session.volume->SetMasterVolume(operation.target,&Context);
                        if(FAILED(result)){failure=result;if(operation.had_record)records[operation.session.key]=operation.before;else records.erase(operation.session.key);}
                        else if(config.gain_db_x10==0)records.erase(operation.session.key);
                        else records[operation.session.key].pending_from=-1;
                    }
                    if(changed)save_records(records);pending_=static_cast<unsigned>(records.size());capped_=capped;error_=failure;state_=FAILED(failure)?AudioState::Error:AudioState::Running;
                }
            }catch(const Failure& f){state_=AudioState::Error;error_=f.code;}
            catch(...){state_=AudioState::Error;error_=E_FAIL;}
            if(WaitForMultipleObjects(2,events,FALSE,50)==WAIT_OBJECT_0)break;
        }
        try{pending_=restore_saved_volumes();error_=S_OK;}catch(...){pending_=1;saved_=false;error_=E_FAIL;}
    }catch(const Failure& f){state_=AudioState::Error;error_=f.code;}catch(...){state_=AudioState::Error;error_=E_FAIL;}
}
}
