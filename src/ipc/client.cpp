#include "client.hpp"
#include "win_pipe.hpp"
#include "core/binary.hpp"
#include <filesystem>
namespace crackhood {
static void start_broker(){
    HMODULE self=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&start_broker),&self);
    wchar_t path[32768]{};GetModuleFileNameW(self,path,32768);auto exe=std::filesystem::path(path).parent_path()/L"crackhood_broker.exe";
    if(!std::filesystem::is_regular_file(exe))return;
    auto command=L"\""+exe.wstring()+L"\"";STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
}
Response Client::call(Request q,std::chrono::milliseconds timeout,std::function<void(uint64_t,uint64_t)> progress){
    Response error{q.id,Error{ErrorCode::Timeout,"Xbox request timed out"}};
    auto count=static_cast<DWORD>(std::clamp<int64_t>(timeout.count(),1,3600000));auto name=pipe_name();auto deadline=GetTickCount64()+count;
    Handle pipe(CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));
    if(pipe.value==INVALID_HANDLE_VALUE){start_broker();while(GetTickCount64()<deadline){WaitNamedPipeW(name.c_str(),50);pipe.value=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);if(pipe.value!=INVALID_HANDLE_VALUE)break;Sleep(10);}}
    if(pipe.value==INVALID_HANDLE_VALUE)return error;
    auto encoded=encode_request(q);auto bytes=std::get_if<std::vector<std::byte>>(&encoded);if(!bytes)return {q.id,std::get<Error>(encoded)};
    auto remaining=[&]{return static_cast<DWORD>(deadline-std::min<ULONGLONG>(deadline,GetTickCount64()));};
    if(!write_frame(pipe,*bytes,remaining()))return error;
    for(;;){auto raw=read_frame(pipe,remaining());auto decoded=decode_response(raw);if(auto r=std::get_if<Response>(&decoded)){
        if(r->id!=q.id)return {q.id,Error{ErrorCode::Protocol,"Incorrect response ID"}};
        if(q.operation.starts_with("job_")&&std::holds_alternative<std::vector<std::byte>>(r->result)){
            auto&b=std::get<std::vector<std::byte>>(r->result);if(b.size()==17&&b[0]==std::byte(0xf0)){Reader reader{b};reader.number(1);auto done=reader.number(8),total=reader.number(8);if(progress)progress(done,total);continue;}
        }
        return *r;
    }return {q.id,std::get<Error>(decoded)};}
}
std::future<Response> Client::send(Request q,std::chrono::milliseconds timeout){return std::async(std::launch::async,[q=std::move(q),timeout]{return Client{}.call(q,timeout);});}
}
