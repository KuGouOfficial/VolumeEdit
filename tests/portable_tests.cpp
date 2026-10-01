#include "portable.h"
#include <fstream>
#include <iostream>
int main(){
    const auto root=ve::executable_directory()/L"portable-test";std::filesystem::create_directory(root);
    try{ve::verify_product(root);return 1;}catch(const std::exception&){}
    {std::ofstream(root/L"product.id")<<ve::ProductId<<'\n';}
    ve::set_deployment_root(root);ve::ensure_state();
    {std::ofstream(ve::state_directory()/L"settings.json")<<"owned";std::ofstream(ve::state_directory()/L"helper.txt")<<"owned";std::ofstream(root/L"README.md")<<"owned";std::ofstream(root/L"VolumeEdit.exe")<<"owned";std::ofstream(root/L"VolumeEditBroker.exe")<<"owned";std::ofstream(root/L"my-notes.txt")<<"user";}
    ve::remove_product_files(root);
    if(!std::filesystem::exists(root/L"my-notes.txt")||std::filesystem::exists(root/L"product.id")||std::filesystem::exists(root/L"state")||std::filesystem::exists(root/L"VolumeEdit.exe")||std::filesystem::exists(root/L"VolumeEditBroker.exe"))return 2;
    std::filesystem::remove(root/L"my-notes.txt");std::filesystem::remove(root);
    std::cout<<"Portable: marker guard, product allowlist cleanup, user-file preservation passed.\n";
}
