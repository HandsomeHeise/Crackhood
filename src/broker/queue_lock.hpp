#pragma once
#include "core/model.hpp"
#include <windows.h>
#include <mutex>
namespace crackhood {
inline Result<Unit> acquire_session(std::unique_lock<std::mutex>&lock,HANDLE cancel,DWORD timeout){
    auto deadline=GetTickCount64()+timeout;
    for(;;){if(WaitForSingleObject(cancel,0)==WAIT_OBJECT_0)return Error{ErrorCode::Cancelled,"Queued job cancelled"};if(lock.try_lock())return Unit{};if(GetTickCount64()>=deadline)return Error{ErrorCode::Timeout,"Xbox session is busy"};WaitForSingleObject(cancel,25);}
}
}
