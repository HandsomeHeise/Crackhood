#include <windows.h>
#include <xbdm.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <chrono>

template<class F> F api(HMODULE module, const char* name) {
    auto p = GetProcAddress(module, name);
    if (!p) throw std::runtime_error(std::string("SDK export missing: ") + name);
    return reinterpret_cast<F>(p);
}
std::string json(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c >= 32 && c < 127) out += c;
        else { char b[7]; sprintf_s(b,"\\u%04x",c); out += b; }
    }
    return out;
}
int wmain(int argc, wchar_t** argv) {
    try {
        wchar_t env[32768]{};
        GetEnvironmentVariableW(L"XEDK", env, 32768);
        std::filesystem::path sdk(env);
        std::string address;
        bool worker = false, readOnly = false;
        for (int i=1;i<argc;++i) {
            std::wstring arg=argv[i];
            if(arg==L"--worker") worker=true;
            else if(arg==L"--read-only") readOnly=true;
            else if(arg==L"--sdk" && i+1<argc) sdk=argv[++i];
            else if(arg==L"--console" && i+1<argc) {
                std::wstring ip=argv[++i];
                for(auto c:ip) { if(!((c>=L'0' && c<=L'9') || c==L'.')) throw std::runtime_error("Invalid IPv4 address"); address+=static_cast<char>(c); }
            } else throw std::runtime_error("Usage: --console <IPv4> --read-only [--sdk <path>]");
        }
        auto dll=sdk/L"bin"/L"x64"/L"xbdm.dll";
        if(sdk.empty() || !std::filesystem::is_regular_file(dll)) { std::cerr<<"Xbox SDK nicht gefunden\n"; return 2; }
        if(address.empty() || !readOnly) throw std::runtime_error("Explicit --console and --read-only required");
        if(!worker) {
            wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768);
            std::wstring wideAddress(address.begin(),address.end());
            std::wstring cmd=L"\""+std::wstring(executable)+L"\" --worker --read-only --console "+wideAddress+L" --sdk \""+sdk.wstring()+L"\"";
            STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
            si.dwFlags=STARTF_USESTDHANDLES;
            si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
            si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);
            si.hStdError=GetStdHandle(STD_ERROR_HANDLE);
            if(!CreateProcessW(executable,cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)) throw std::runtime_error("Cannot start probe worker");
            CloseHandle(pi.hThread);
            auto waited=WaitForSingleObject(pi.hProcess,3000);
            if(waited!=WAIT_OBJECT_0) {
                TerminateProcess(pi.hProcess,3); WaitForSingleObject(pi.hProcess,1000); CloseHandle(pi.hProcess);
                std::cerr<<"XBDM timeout after 3000 ms\n"; return 3;
            }
            DWORD code=1; GetExitCodeProcess(pi.hProcess,&code); CloseHandle(pi.hProcess); return static_cast<int>(code);
        }
        auto module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module) throw std::runtime_error("Cannot load x64 xbdm.dll");
        auto set=api<decltype(&DmSetXboxNameNoRegister)>(module,"DmSetXboxNameNoRegister");
        auto drivesFn=api<decltype(&DmGetDriveList)>(module,"DmGetDriveList");
        auto walkFn=api<decltype(&DmWalkDir)>(module,"DmWalkDir");
        auto closeFn=api<decltype(&DmCloseDir)>(module,"DmCloseDir");
        auto start=std::chrono::steady_clock::now();
        auto emit=[&](const char* operation,HRESULT hr,const std::string& result) {
            auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
            std::cout<<"{\"operation\":\""<<operation<<"\",\"elapsed_ms\":"<<ms<<",\"hresult\":"<<static_cast<uint32_t>(hr)<<",\"result\":\""<<json(result)<<"\"}\n"<<std::flush;
        };
        auto hr=set(address.c_str()); emit("select",hr,address); if(FAILED(hr)) return 4;
        char drives[256]{}; DWORD size=sizeof(drives);
        hr=drivesFn(drives,&size); emit("drives",hr,std::string(drives,strnlen_s(drives,sizeof(drives)))); if(FAILED(hr)) return 4;
        const std::string driveList(drives,strnlen_s(drives,sizeof(drives)));
        if(!driveList.empty()) {
            std::string path=driveList.substr(0,driveList.find(',')); path+=":\\";
            PDM_WALK_DIR walk=nullptr; DM_FILE_ATTRIBUTES attr{}; unsigned count=0;
            while(SUCCEEDED(hr=walkFn(&walk,path.c_str(),&attr))) {
                emit("entry",hr,path+attr.Name); if(++count==10) break;
            }
            if(walk) closeFn(walk);
            emit("directory",hr,path);
            if(FAILED(hr) && hr!=XBDM_ENDOFLIST) return 4;
        }
        FreeLibrary(module); return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
