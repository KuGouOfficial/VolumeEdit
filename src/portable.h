#pragma once
#include "common.h"
namespace ve {
inline constexpr char ProductId[]="VolumeEdit.Portable.8A7C7476-55F3-4525-82E7-33B2FB781132";
void ensure_state();
bool startup_enabled();
void set_startup(bool enabled);
void clear_startup();
void verify_product(const std::filesystem::path& root);
void refuse_reparse(const std::filesystem::path& path);
bool unlink_executable(const std::filesystem::path& path);
void remove_product_files(const std::filesystem::path& root,bool keep_uninstaller=false);
void launch_final_cleanup(const std::filesystem::path& root,bool notify);
}
