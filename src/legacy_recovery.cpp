#include "audio.h"
#include "permission_helper.h"
#include "portable.h"
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <set>
namespace ve {
// Compatibility recovery only. New volume control never enumerates sessions.
unsigned restore_saved_volumes(){
    refuse_reparse(state_directory());auto records=load_records();if(records.empty())return 0;Com<IMMDeviceEnumerator> enumerator;
    hr(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)));std::set<std::wstring> endpoints;
    for(const auto&[key,record]:records){(void)record;endpoints.insert(key.first);}
    const GUID context={0x7cda93b4,0x456c,0x4d42,{0xa5,0x89,0xbb,0xb3,0x15,0x67,0xa6,0x94}};
    for(const auto& endpoint:endpoints){try{
        Com<IMMDevice> device;hr(enumerator->GetDevice(endpoint.c_str(),&device));Com<IAudioSessionManager2> manager;hr(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,&manager));
        Com<IAudioSessionEnumerator> list;hr(manager->GetSessionEnumerator(&list));int count=0;hr(list->GetCount(&count));
        struct Session{VolumeKey key;Com<ISimpleAudioVolume> volume;};std::vector<Session> active;std::vector<VolumeKey> keys;
        for(int i=0;i<count;++i){Com<IAudioSessionControl> base;if(FAILED(list->GetSession(i,&base)))continue;Com<IAudioSessionControl2> control;if(FAILED(base.As(&control)))continue;
            DWORD pid=0;if(control->IsSystemSoundsSession()!=S_OK&&(FAILED(control->GetProcessId(&pid))||process_ownership(pid).owner!=Ownership::Own))continue;
            LPWSTR id=nullptr;if(FAILED(control->GetSessionIdentifier(&id)))continue;std::wstring stable=id;CoTaskMemFree(id);if(stable.empty()||stable.find(L'\n')!=std::wstring::npos)continue;
            if(FAILED(control->GetSessionInstanceIdentifier(&id)))continue;Session session;session.key={endpoint,stable+L"\n"+id};CoTaskMemFree(id);
            if(SUCCEEDED(base.As(&session.volume))){keys.push_back(session.key);active.push_back(std::move(session));}
        }
        reconcile_records(records,keys);
        for(const auto& session:active){auto found=records.find(session.key);if(found==records.end())continue;float current=0;if(FAILED(session.volume->GetMasterVolume(&current)))continue;
            if(!record_matches(current,found->second)||SUCCEEDED(session.volume->SetMasterVolume(found->second.original,&context)))records.erase(found);
        }
    }catch(const Failure&){} }
    save_records(records);return static_cast<unsigned>(records.size());
}
}
