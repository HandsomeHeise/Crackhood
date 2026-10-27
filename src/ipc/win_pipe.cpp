#include "win_pipe.hpp"
#include <sddl.h>
#include <cstring>
namespace crackhood {
std::wstring user_sid(){HANDLE raw=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw))throw std::runtime_error("Token query failed");Handle token(raw);DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<std::byte>b(size);if(!GetTokenInformation(token,TokenUser,b.data(),size,&size))throw std::runtime_error("Token user failed");LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(b.data())->User.Sid,&sid))throw std::runtime_error("SID conversion failed");std::wstring result=sid;LocalFree(sid);return result;}
std::wstring pipe_name(){return L"\\\\.\\pipe\\Crackhood.v1."+user_sid();}
bool transfer(HANDLE h,bool write,std::span<std::byte> bytes,DWORD timeout){
    auto deadline=GetTickCount64()+timeout;size_t done=0;
    while(done<bytes.size()){
        if(GetTickCount64()>=deadline)return false;
        Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED ov{};ov.hEvent=event;
        DWORD transferred=0;auto count=static_cast<DWORD>(std::min<size_t>(65536,bytes.size()-done));
        BOOL ok=write?WriteFile(h,bytes.data()+done,count,&transferred,&ov):ReadFile(h,bytes.data()+done,count,&transferred,&ov);
        if(!ok){if(GetLastError()!=ERROR_IO_PENDING)return false;DWORD left=static_cast<DWORD>(deadline-std::min<ULONGLONG>(deadline,GetTickCount64()));if(WaitForSingleObject(event,left)!=WAIT_OBJECT_0){CancelIoEx(h,&ov);GetOverlappedResult(h,&ov,&transferred,TRUE);return false;}if(!GetOverlappedResult(h,&ov,&transferred,FALSE))return false;}
        if(!transferred)return false;done+=transferred;
    }return true;
}
bool write_frame(HANDLE h,std::span<const std::byte>b,DWORD timeout){if(b.empty()||b.size()>1048576)return false;DWORD size=static_cast<DWORD>(b.size());if(!transfer(h,true,{reinterpret_cast<std::byte*>(&size),4},timeout))return false;return transfer(h,true,{const_cast<std::byte*>(b.data()),b.size()},timeout);}
std::vector<std::byte> read_frame(HANDLE h,DWORD timeout){DWORD size=0;if(!transfer(h,false,{reinterpret_cast<std::byte*>(&size),4},timeout)||size==0||size>1048576)return {};std::vector<std::byte>b(size);if(!transfer(h,false,b,timeout))return {};return b;}
HANDLE create_server_pipe(const std::wstring&name){
    auto dacl=L"D:P(A;;GA;;;"+user_sid()+L")(A;;GA;;;SY)";PSECURITY_DESCRIPTOR sd=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(dacl.c_str(),SDDL_REVISION_1,&sd,nullptr))return INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
    auto h=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,64,65536,65536,0,&sa);
    LocalFree(sd);return h;
}
bool connect_server_pipe(HANDLE h,DWORD timeout){Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED ov{};ov.hEvent=event;if(ConnectNamedPipe(h,&ov))return true;auto error=GetLastError();if(error==ERROR_PIPE_CONNECTED)return true;if(error!=ERROR_IO_PENDING)return false;DWORD done=0;if(WaitForSingleObject(event,timeout)!=WAIT_OBJECT_0){CancelIoEx(h,&ov);GetOverlappedResult(h,&ov,&done,TRUE);return false;}return GetOverlappedResult(h,&ov,&done,FALSE)!=FALSE;}
}
