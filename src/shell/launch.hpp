#pragma once
#include "ipc/protocol.hpp"
#include <windows.h>
namespace crackhood { HRESULT LaunchRequest(Request,bool promptForFolder=true);HRESULT LaunchSettings(); }
