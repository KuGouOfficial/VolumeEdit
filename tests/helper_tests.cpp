#include "permission_helper.h"
#include <iostream>
int main(){
    using ve::helper_request_allowed;
    if(!helper_request_allowed(true,1,1,123,123))return 1;
    if(helper_request_allowed(false,1,1,123,123))return 2;
    if(helper_request_allowed(true,0,0,123,123))return 3;
    if(helper_request_allowed(true,1,2,123,123))return 4;
    if(helper_request_allowed(true,1,1,0,0))return 5;
    if(helper_request_allowed(true,1,1,123,124))return 6;
    const auto self=ve::process_ownership(GetCurrentProcessId(),false);
    if(self.owner!=ve::Ownership::Own||self.assisted||self.error)return 7;
    const auto missing=ve::process_ownership(0xffffffff,false);
    if(missing.owner!=ve::Ownership::Unknown||missing.assisted)return 8;
    std::cout<<"Helper: own-user and login-session gate, PID creation-time validation, local owner lookup and unavailable-process refusal passed.\n";
}
