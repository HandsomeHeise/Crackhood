#pragma once
#include "model.hpp"
#include <map>
#include <mutex>
#include <chrono>
namespace crackhood {
struct Snapshot { std::vector<Entry> entries; bool pending=true; bool stale=true; uint64_t generation=0; };
class Cache {
    struct State { Snapshot snapshot; std::chrono::steady_clock::time_point updated{}; };
    std::mutex mutex_; std::map<std::pair<ConsoleId,RemotePath>,State> states_;
public:
    Snapshot get(ConsoleId,RemotePath);
    void invalidate(ConsoleId,RemotePath);
    bool publish(ConsoleId,RemotePath,uint64_t,std::vector<Entry>);
};
}
