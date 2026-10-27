#include "cache.hpp"
namespace crackhood {
Snapshot Cache::get(ConsoleId id,RemotePath path){std::lock_guard lock(mutex_);if(!states_.contains({id,path})&&states_.size()>=256)states_.erase(states_.begin());auto&state=states_[{id,path}];auto s=state.snapshot;if(std::chrono::steady_clock::now()-state.updated>std::chrono::seconds(5))s.stale=true;return s;}
void Cache::invalidate(ConsoleId id,RemotePath path){std::lock_guard lock(mutex_);auto&s=states_[{id,path}].snapshot;++s.generation;s.pending=true;s.stale=true;}
bool Cache::publish(ConsoleId id,RemotePath path,uint64_t generation,std::vector<Entry> entries){
    std::lock_guard lock(mutex_);auto&state=states_[{id,path}];if(state.snapshot.generation!=generation)return false;
    state.snapshot.entries=std::move(entries);state.snapshot.pending=false;state.snapshot.stale=false;state.updated=std::chrono::steady_clock::now();return true;
}
}
