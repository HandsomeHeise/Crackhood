#pragma once
#include "ipc/win_pipe.hpp"
#include <thread>
namespace crackhood {
class Cancellation {
    Handle event_;std::stop_source stop_;std::jthread watcher_;
public:
    explicit Cancellation(uint64_t id):event_(CreateEventW(nullptr,TRUE,FALSE,(L"Local\\Crackhood.Cancel."+user_sid()+L"."+std::to_wstring(id)).c_str())),watcher_([this](std::stop_token token){while(!token.stop_requested()){if(WaitForSingleObject(event_,25)==WAIT_OBJECT_0){stop_.request_stop();break;}}}){if(WaitForSingleObject(event_,0)==WAIT_OBJECT_0)stop_.request_stop();}
    std::stop_token token()const{return stop_.get_token();}
};
}
