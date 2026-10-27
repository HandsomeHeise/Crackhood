#include "launch.hpp"
#include <objbase.h>
#include "broker/profiles.hpp"
#include <fstream>
namespace crackhood {
static HRESULT launch(const std::wstring&argument,bool nativeJob=false){HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&launch),&module);wchar_t path[32768]{};GetModuleFileNameW(module,path,32768);auto exe=std::filesystem::path(path).parent_path()/(nativeJob?L"crackhood_broker.exe":L"crackhood_ui.exe");if(!std::filesystem::is_regular_file(exe))return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);auto command=L"\""+exe.wstring()+L"\" "+argument;STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,nativeJob?CREATE_NO_WINDOW:0,nullptr,nullptr,&si,&pi))return HRESULT_FROM_WIN32(GetLastError());CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return S_OK;}
HRESULT LaunchSettings(){return launch(L"--settings");}
HRESULT LaunchRequest(Request q,bool promptForFolder){try{GUID guid{};if(FAILED(CoCreateGuid(&guid)))return E_FAIL;wchar_t name[64]{};StringFromGUID2(guid,name,64);q.id=(uint64_t(guid.Data1)<<32)|(uint64_t(guid.Data2)<<16)|guid.Data3;auto root=user_root()/L"Jobs";std::filesystem::create_directories(root);auto path=root/(std::wstring(name)+L".request");auto encoded=encode_request(q);auto bytes=std::get_if<std::vector<std::byte>>(&encoded);if(!bytes)return E_INVALIDARG;{std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes->data()),bytes->size());if(!f)return E_FAIL;}bool nativeJob=q.operation!="mkdir"||!promptForFolder;return launch(std::wstring(nativeJob?L"--shell-job \"":L"--request \"")+path.wstring()+L"\"",nativeJob);}catch(...){return E_FAIL;}}
}
