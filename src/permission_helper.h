#pragma once
#include "common.h"
#include <cstdint>

namespace ve {
// Legacy compatibility only; endpoint volume control does not use this helper.
enum class Ownership : unsigned {Unknown, Own, Other};
struct OwnershipResult {Ownership owner=Ownership::Unknown;DWORD error{};bool assisted=false;};
OwnershipResult process_ownership(DWORD pid, bool assist=true);
bool helper_installed();
void ensure_helper_running();
void install_helper(HWND parent);
void remove_helper(HWND parent);
void helper_startup(bool enabled);
// A pure policy gate shared by the service and its security tests.
bool helper_request_allowed(bool same_user, DWORD client_session, DWORD target_session,
                            std::uint64_t expected_creation, std::uint64_t actual_creation) noexcept;
}
