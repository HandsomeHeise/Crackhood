#pragma once
#include "core/transport.hpp"
namespace crackhood {
Result<Unit> receive_xbdm_file(const std::string&,const RemotePath&,const std::filesystem::path&,uint64_t,std::stop_token,Progress);
Result<Unit> send_xbdm_file(const std::string&,const RemotePath&,const std::filesystem::path&,uint64_t,std::stop_token,Progress);
}
