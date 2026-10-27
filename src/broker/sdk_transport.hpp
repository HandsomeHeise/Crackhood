#pragma once
#include "core/transport.hpp"
namespace crackhood {
Error sdk_error(int32_t hresult);
std::unique_ptr<ITransport> make_sdk_transport(ConsoleId,std::string address);
}
