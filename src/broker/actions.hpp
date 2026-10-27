#pragma once
#include "core/model.hpp"
#include <cstdio>
namespace crackhood {
inline std::string system_time_command(uint32_t high,uint32_t low){char command[96]{};sprintf_s(command,"setsystime clockhi=0x%08x clocklo=0x%08x",high,low);return command;}
}
