#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
namespace crackhood {
inline void trace_shell(const char*method,REFIID iid=IID_IUnknown) noexcept {
    try{DWORD enabled=0,size=sizeof(enabled);if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Crackhood",L"Diagnostics",RRF_RT_REG_DWORD,nullptr,&enabled,&size)!=ERROR_SUCCESS||!enabled)return;
        wchar_t local[32768]{};if(!GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768))return;auto root=std::filesystem::path(local)/L"Crackhood"/L"Diagnostics";std::filesystem::create_directories(root);
        static std::mutex mutex;std::lock_guard lock(mutex);std::ofstream file(root/(std::to_wstring(GetCurrentProcessId())+L".log"),std::ios::app);wchar_t guid[64]{};StringFromGUID2(iid,guid,64);std::string text;for(auto c:guid){if(!c)break;text.push_back(static_cast<char>(c));}file<<GetTickCount64()<<' '<<method<<' '<<text<<'\n';
    }catch(...){}
}
}
