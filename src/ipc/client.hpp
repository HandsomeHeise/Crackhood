#pragma once
#include "protocol.hpp"
#include <chrono>
#include <future>
#include <functional>
namespace crackhood {
class Client {
public:
    std::future<Response> send(Request,std::chrono::milliseconds timeout);
    Response call(Request,std::chrono::milliseconds timeout,std::function<void(uint64_t,uint64_t)> progress={});
};
}
