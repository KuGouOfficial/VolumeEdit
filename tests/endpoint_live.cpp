#include "audio.h"
#include "portable.h"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <iostream>
#include <fstream>
#include <functional>
void wait_for(const std::function<bool()>& condition){auto deadline=GetTickCount64()+5000;while(GetTickCount64()<deadline){if(condition())return;Sleep(50);}throw std::runtime_error("Endpoint integration timed out");}
int wmain(int argc,wchar_t**argv){try{
    if(argc!=3||std::wstring(argv[1])!=L"--confirm-real-audio")return 2;ve::Apartment apartment;std::filesystem::path root=std::filesystem::absolute(argv[2]).lexically_normal();if(root.parent_path()!=ve::executable_directory()||!root.filename().wstring().starts_with(L"endpoint-live-"))throw std::runtime_error("Unsafe live-test fixture");ve::refuse_reparse(root);if(std::filesystem::exists(root))throw std::runtime_error("Test directory already exists");std::filesystem::create_directory(root);{std::ofstream(root/L"product.id")<<ve::ProductId;}ve::set_deployment_root(root);ve::ensure_state();
    ve::Com<IMMDeviceEnumerator> e;ve::hr(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e)));ve::Com<IMMDevice> d;ve::hr(e->GetDefaultAudioEndpoint(eRender,eConsole,&d));ve::Com<IAudioEndpointVolume> v;ve::hr(d->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,&v));
    float original=0,minimum=0,maximum=0,increment=0;BOOL mute=FALSE;ve::hr(v->GetMasterVolumeLevel(&original));ve::hr(v->GetMute(&mute));ve::hr(v->GetVolumeRange(&minimum,&maximum,&increment));
    struct Restore{IAudioEndpointVolume* v;float db;BOOL mute;~Restore(){v->SetMasterVolumeLevel(db,nullptr);v->SetMute(mute,nullptr);}} restore{v.Get(),original,mute};ve::hr(v->SetMute(TRUE,nullptr));const float baseline=std::min(maximum,minimum+5);ve::hr(v->SetMasterVolumeLevel(baseline,nullptr));
    ve::AudioEngine engine;engine.start();wait_for([&]{return engine.status().state==ve::AudioState::Running;});ve::Settings settings=engine.settings();settings.gain_db_x10=-10;engine.configure(settings);
    wait_for([&]{float value=0;v->GetMasterVolumeLevel(&value);return ve::same_db(value,baseline-1);});std::cout<<"master_adjustment=passed\n";
    settings.gain_db_x10=0;engine.configure(settings);wait_for([&]{float value=0;v->GetMasterVolumeLevel(&value);return ve::same_db(value,baseline)&&ve::load_endpoint_records().empty();});std::cout<<"reset=passed\n";
    settings.gain_db_x10=-10;engine.configure(settings);wait_for([&]{float value=0;v->GetMasterVolumeLevel(&value);return ve::same_db(value,baseline-1);});engine.stop();float value=0;ve::hr(v->GetMasterVolumeLevel(&value));if(!ve::same_db(value,baseline)||!ve::load_endpoint_records().empty())throw std::runtime_error("Exit did not restore baseline");std::cout<<"exit_restore=passed\n";
    engine.start();wait_for([&]{float level=0;v->GetMasterVolumeLevel(&level);return ve::same_db(level,baseline-1);});ve::hr(v->SetMasterVolumeLevel(baseline-2,nullptr));wait_for([&]{return engine.settings().gain_db_x10==0;});engine.stop();ve::hr(v->GetMasterVolumeLevel(&value));if(!ve::same_db(value,baseline-2))throw std::runtime_error("External change was overwritten");std::cout<<"external_change_priority=passed\n";
    ve::save_settings(ve::state_directory()/L"settings.json",ve::Settings{});std::cout<<"original_master_and_mute_restored=on_scope_exit\n";return 0;
}catch(const ve::Failure& f){std::cerr<<"HRESULT="<<std::hex<<static_cast<unsigned>(f.code)<<"\n";return 3;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 4;}}
