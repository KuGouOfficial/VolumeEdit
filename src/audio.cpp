#include "audio.h"
#include "portable.h"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <algorithm>
#include <cmath>
namespace ve {
static const GUID Context={0xe3a3d3d9,0x25a4,0x4261,{0xaa,0x49,0x39,0xef,0xa1,0x29,0x7b,0xd5}};
static Com<IMMDeviceEnumerator> enumerator(){Com<IMMDeviceEnumerator> value;hr(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&value)));return value;}
std::vector<Device> devices(){auto e=enumerator();Com<IMMDeviceCollection> all;hr(e->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&all));UINT count=0;hr(all->GetCount(&count));std::vector<Device> out;
    for(UINT i=0;i<count;++i){Com<IMMDevice> d;hr(all->Item(i,&d));LPWSTR id=nullptr;hr(d->GetId(&id));Device item;item.id=id;CoTaskMemFree(id);Com<IPropertyStore> store;hr(d->OpenPropertyStore(STGM_READ,&store));PROPVARIANT name;PropVariantInit(&name);
        if(SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName,&name))&&name.vt==VT_LPWSTR)item.name=name.pwszVal;PropVariantClear(&name);out.push_back(std::move(item));}return out;
}
std::wstring default_output_id(){auto e=enumerator();Com<IMMDevice> d;auto result=e->GetDefaultAudioEndpoint(eRender,eConsole,&d);if(result==E_NOTFOUND)return {};hr(result);LPWSTR id=nullptr;hr(d->GetId(&id));std::wstring out=id;CoTaskMemFree(id);return out;}
std::wstring resolve_target(const std::vector<Device>& active,const std::wstring& current,const std::wstring& manual,bool automatic){const auto& id=automatic?current:manual;return std::any_of(active.begin(),active.end(),[&](const Device& d){return !id.empty()&&d.id==id;})?id:L"";}
static Com<IAudioEndpointVolume> endpoint_volume(const std::wstring& id){auto e=enumerator();Com<IMMDevice> d;hr(e->GetDevice(id.c_str(),&d));Com<IAudioEndpointVolume> volume;hr(d->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,&volume));return volume;}
unsigned restore_endpoint_volumes(){auto records=load_endpoint_records();
    for(auto it=records.begin();it!=records.end();){try{auto volume=endpoint_volume(it->first);float current=0;hr(volume->GetMasterVolumeLevel(&current));
        if(endpoint_matches(current,it->second))hr(volume->SetMasterVolumeLevel(it->second.original,&Context));it=records.erase(it);
    }catch(const Failure&){++it;} }save_endpoint_records(records);return static_cast<unsigned>(records.size());
}
AudioEngine::AudioEngine():stop_(CreateEventW(nullptr,TRUE,FALSE,nullptr)),reconnect_(CreateEventW(nullptr,FALSE,FALSE,nullptr)){
    if(!stop_||!reconnect_)win(FALSE);auto path=state_directory()/L"settings.json";
    if(std::filesystem::exists(path))try{settings_=load_settings(path);settings_.muted=false;}catch(...){status_.saved=0;ready_=false;}
}
AudioEngine::~AudioEngine(){stop();}
void AudioEngine::start(){if(!thread_.joinable()){ResetEvent(stop_.get());thread_=std::thread(&AudioEngine::worker,this);}}
void AudioEngine::stop(){SetEvent(stop_.get());if(thread_.joinable())thread_.join();std::lock_guard lock(mutex_);status_.state=AudioState::Stopped;}
void AudioEngine::retry(){SetEvent(reconnect_.get());}
Settings AudioEngine::settings(){std::lock_guard lock(mutex_);return settings_;}
AudioStatus AudioEngine::status(){std::lock_guard lock(mutex_);return status_;}
bool AudioEngine::configure(const Settings& input,bool persist){if(!valid(input))return false;Settings value=input;value.source.clear();value.muted=false;
    if(!value.automatic_target&&resolve_target(devices(),L"",value.target,false).empty())return false;std::lock_guard lock(mutex_);settings_=value;ready_=true;
    if(persist)try{save_settings(state_directory()/L"settings.json",value);status_.saved=1;}catch(...){status_.saved=0;}
    ++revision_;SetEvent(reconnect_.get());return true;
}
void AudioEngine::worker(){try{
    Apartment apartment;ensure_state();std::wstring selected;Com<IAudioEndpointVolume> volume;float baseline=0,last=0,minimum=0,maximum=0,increment=0;bool have_last=false;
    unsigned seen_revision=revision_;ULONGLONG legacy_check=0;AudioStatus published;const HANDLE events[]={stop_.get(),reconnect_.get()};
    while(WaitForSingleObject(stop_.get(),0)!=WAIT_OBJECT_0){try{
        Settings config;bool ready;unsigned revision;{std::lock_guard lock(mutex_);config=settings_;ready=ready_;published.saved=status_.saved;revision=revision_;}
        if(!ready){published.state=AudioState::Waiting;published.error=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);}
        else{
            if(GetTickCount64()>=legacy_check){published.pending_restore=restore_saved_volumes();legacy_check=GetTickCount64()+3000;}
            const auto id=resolve_target(devices(),config.automatic_target?default_output_id():L"",config.target,config.automatic_target);const bool configured=revision!=seen_revision;
            auto records=load_endpoint_records();
            if(id!=selected||!volume){if(!selected.empty())published.pending_endpoints=restore_endpoint_volumes();records=load_endpoint_records();volume.Reset();selected=id;have_last=false;
                if(!id.empty()){volume=endpoint_volume(id);hr(volume->GetVolumeRange(&minimum,&maximum,&increment));hr(volume->GetMasterVolumeLevel(&baseline));last=baseline;
                    auto old=records.find(id);if(old!=records.end()){if(endpoint_matches(baseline,old->second)){baseline=old->second.original;last=baseline;hr(volume->GetMasterVolumeLevel(&last));have_last=true;}else{records.erase(old);save_endpoint_records(records);}}
                }
            }
            if(!volume){published.state=AudioState::Waiting;published.error=S_OK;}
            else{
                float current=0;hr(volume->GetMasterVolumeLevel(&current));
                // A Windows slider/device-key change takes priority over this tool.
                if(!configured&&((have_last&&!same_db(current,last))||(!have_last&&!same_db(current,baseline)))){
                    baseline=current;last=current;have_last=false;records.erase(id);save_endpoint_records(records);config.gain_db_x10=0;
                    {std::lock_guard lock(mutex_);if(revision_==revision){settings_.gain_db_x10=0;try{save_settings(state_directory()/L"settings.json",settings_);published.saved=1;}catch(...){published.saved=0;}}}
                    published.external_change=true;
                }else if(configured)published.external_change=false;
                const auto desired=endpoint_target(baseline,config.gain_db_x10,minimum,maximum);
                if(!same_db(current,desired)){
                    auto previous=records;records[id]={baseline,desired,current};save_endpoint_records(records);
                    const auto result=volume->SetMasterVolumeLevel(desired,&Context);
                    if(FAILED(result)){save_endpoint_records(previous);hr(result);}
                    hr(volume->GetMasterVolumeLevel(&current));records[id]={baseline,current};save_endpoint_records(records);last=current;have_last=true;
                }
                if(config.gain_db_x10==0&&records.contains(id)){records.erase(id);save_endpoint_records(records);have_last=false;last=current;}
                published.state=AudioState::Running;published.error=S_OK;published.current_db=current;published.baseline_db=baseline;published.minimum_db=minimum;published.maximum_db=maximum;published.increment_db=increment;
                published.limited=!same_db(desired,baseline+config.gain_db_x10/10.0f);published.pending_endpoints=static_cast<unsigned>(records.size());
            }seen_revision=revision;
        }
    }catch(const Failure& f){published.state=AudioState::Error;published.error=f.code;volume.Reset();}
     catch(...){published.state=AudioState::Error;published.error=E_FAIL;volume.Reset();}
        {std::lock_guard lock(mutex_);status_=published;}
        if(WaitForMultipleObjects(2,events,FALSE,100)==WAIT_OBJECT_0)break;
    }
    try{published.pending_endpoints=restore_endpoint_volumes();published.pending_restore=restore_saved_volumes();published.error=S_OK;}catch(const Failure& f){published.error=f.code;}catch(...){published.error=E_FAIL;}
    {std::lock_guard lock(mutex_);status_=published;}
}catch(const Failure& f){std::lock_guard lock(mutex_);status_.state=AudioState::Error;status_.error=f.code;}catch(...){std::lock_guard lock(mutex_);status_.state=AudioState::Error;status_.error=E_FAIL;}}
}
