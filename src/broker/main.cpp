#include "sdk_transport.hpp"
#include "shell_job.hpp"
#include "jobs.hpp"
#include "request_history.hpp"
#include "cancellation.hpp"
#include "queue_lock.hpp"
#include "profiles.hpp"
#include "ipc/client.hpp"
#include "ipc/changes.hpp"
#include "ipc/win_pipe.hpp"
#include "core/binary.hpp"
#include "core/cache.hpp"
#include "core/unicode.hpp"
#include <filesystem>
#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <atomic>
using namespace crackhood;
static std::wstring cancel_name(uint64_t id){return L"Local\\Crackhood.Cancel."+user_sid()+L"."+std::to_wstring(id);}
static Response execute(const Request&q,const std::string&address,Progress progress={}){
    try{
        auto t=make_sdk_transport(q.console,address);
        if(q.operation=="drives"||q.operation=="list"){auto r=q.operation=="drives"?t->drives():t->list(q.path);if(auto e=std::get_if<Error>(&r))return {q.id,*e};return {q.id,encode_entries(std::get<std::vector<Entry>>(r))};}
        if(q.operation=="stat"){auto r=t->stat(q.path);if(auto e=std::get_if<Error>(&r))return {q.id,*e};return {q.id,encode_entries({std::get<Entry>(r)})};}
        if(q.operation=="disk_space"){auto r=t->disk_space(q.path);if(auto e=std::get_if<Error>(&r))return {q.id,*e};auto d=std::get<DiskSpace>(r);Writer w;w.number(d.available,8);w.number(d.total,8);w.number(d.free,8);return {q.id,w.bytes};}
        Result<Unit> result=Error{ErrorCode::Unsupported,"Unknown action"};
        Cancellation cancellation(q.id);
        Reader r{q.payload};
        if(q.operation=="mkdir")result=t->mkdir(q.path);
        else if(q.operation=="rename")result=t->rename(q.path,{r.text()});
        else if(q.operation=="delete"||q.operation=="job_delete")result=TransferJob(*t).run({q.id,q.console,"delete",{},q.path,{},ConflictPolicy::Ask},cancellation.token(),progress);
        else if(q.operation=="download"||q.operation=="upload"||q.operation=="job_download"||q.operation=="job_download_open"||q.operation=="job_upload"||q.operation=="job_move"||q.operation=="job_upload_many"||q.operation=="job_move_many"||q.operation=="job_delete_many"){
            auto operation=q.operation.starts_with("job_")?q.operation.substr(4):q.operation;
            if(operation=="download_open")operation="download";
            auto argument=r.text();JobSpec job{q.id,q.console,operation,std::filesystem::path(utf16(argument)),q.path,q.path,ConflictPolicy::Ask};if(operation=="move")job.target={argument};
            if(r.offset<r.bytes.size())job.conflict=static_cast<ConflictPolicy>(r.number(1));
            if(operation=="upload_many"||operation=="move_many"||operation=="delete_many"){auto count=r.number(4);if(!count||count>65536)return {q.id,Error{ErrorCode::Protocol,"Invalid batch job"}};job.sources.push_back(argument);while(--count)job.sources.push_back(r.text());}
            if(job.conflict>ConflictPolicy::Cancel||r.offset!=r.bytes.size())return {q.id,Error{ErrorCode::Protocol,"Invalid file job"}};
            result=TransferJob(*t).run(job,cancellation.token(),progress);
        }
        else result=t->action(q.operation,q.path);
        if(auto e=std::get_if<Error>(&result))return {q.id,*e};return {q.id,std::vector<std::byte>{}};
    }catch(const std::exception&e){return {q.id,Error{ErrorCode::Protocol,e.what()}};}
}
static Response isolated(const Request&q,std::function<void(const Response&)> onProgress={}){
    auto name=pipe_name()+L".worker."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(q.id)+L"."+std::to_wstring(GetTickCount64());
    Handle pipe(create_server_pipe(name));if(pipe.value==INVALID_HANDLE_VALUE)return {q.id,Error{ErrorCode::Protocol,"Could not create worker pipe"}};
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
    auto profiles=load_profiles();std::wstring address;
    if(auto rows=std::get_if<std::vector<Profile>>(&profiles))for(auto&p:*rows)if(p.id==q.console)address=utf16(p.address);
    if(address.empty())return {q.id,Error{ErrorCode::NotFound,"Console profile not found"}};
    for(auto c:address)if((c<L'0'||c>L'9')&&c!=L'.')return {q.id,Error{ErrorCode::InvalidName,"Invalid console address"}};
    auto command=L"\""+std::wstring(path)+L"\" --worker \""+name+L"\" "+address;
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return {q.id,Error{ErrorCode::Protocol,"Could not start worker"}};
    Handle process(pi.hProcess);CloseHandle(pi.hThread);
    const DWORD timeout=(q.operation=="download"||q.operation=="upload"||q.operation.starts_with("job_"))?3600000:3000;
    Handle cancel(CreateEventW(nullptr,TRUE,FALSE,cancel_name(q.id).c_str()));
    std::jthread watchdog([&](std::stop_token token){while(!token.stop_requested()){if(WaitForSingleObject(cancel,100)==WAIT_OBJECT_0){if(WaitForSingleObject(process,5000)!=WAIT_OBJECT_0)TerminateProcess(process,ERROR_CANCELLED);break;}}});
    auto b=std::get<std::vector<std::byte>>(encode_request(q));
    Response result{q.id,Error{ErrorCode::Timeout,"XBDM request timed out"}};
    if(connect_server_pipe(pipe,3000)&&write_frame(pipe,b,3000)){
        for(;;){auto raw=read_frame(pipe,timeout);auto decoded=decode_response(raw);if(auto r=std::get_if<Response>(&decoded)){
            if(q.operation.starts_with("job_")&&std::holds_alternative<std::vector<std::byte>>(r->result)){auto&payload=std::get<std::vector<std::byte>>(r->result);if(payload.size()==17&&payload[0]==std::byte(0xf0)){if(onProgress)onProgress(*r);continue;}}
            result=*r;
        }break;}
    }
    if(WaitForSingleObject(cancel,0)==WAIT_OBJECT_0&&std::holds_alternative<Error>(result.result))result={q.id,Error{ErrorCode::Cancelled,"Transfer cancelled; any partial files remain identifiable"}};
    if(WaitForSingleObject(process,200)!=WAIT_OBJECT_0){TerminateProcess(process,3);WaitForSingleObject(process,1000);}
    return result;
}
int wmain(int argc,wchar_t**argv){
    try{
        if(argc==3&&std::wstring(argv[1])==L"--shell-job")return run_native_shell_job(argv[2]);
        if(argc==4&&std::wstring(argv[1])==L"--worker"){
            Handle pipe(CreateFileW(argv[2],GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));if(pipe.value==INVALID_HANDLE_VALUE)return 2;
            auto raw=read_frame(pipe,3000);auto request=decode_request(raw);if(!std::holds_alternative<Request>(request))return 2;
            auto q=std::get<Request>(request);auto response=execute(q,utf8(argv[3]),[&](uint64_t done,uint64_t total){if(!q.operation.starts_with("job_"))return;Writer w;w.number(0xf0,1);w.number(done,8);w.number(total,8);auto encoded=encode_response({q.id,w.bytes});if(auto bytes=std::get_if<std::vector<std::byte>>(&encoded))write_frame(pipe,*bytes,3000);});auto b=encode_response(response);if(auto bytes=std::get_if<std::vector<std::byte>>(&b))write_frame(pipe,*bytes,3000);return 0;
        }
        if(argc>=3&&std::wstring(argv[1])==L"--rpc"){
            Request q{1,GetTickCount64(),{"rgh"},utf8(argv[2]),{}, {}};
            if(argc>3)q.path.value=utf8(argv[3]);Writer w;
            if(q.operation=="delete")w.number(argc>4&&std::wstring(argv[4])==L"directory",1);else if(argc>4)w.text(utf8(argv[4]));q.payload=w.bytes;
            if(argc>5&&(q.operation=="upload"||q.operation=="download"||q.operation=="job_upload"||q.operation=="job_download"||q.operation=="job_move")){w.number(std::wstring(argv[5])==L"replace"?1:2,1);q.payload=w.bytes;}
            auto response=Client{}.call(q,std::chrono::milliseconds(3600000));if(auto e=std::get_if<Error>(&response.result)){std::cerr<<e->message<<" ("<<e->native_code<<")\n";return 1;}
            if(q.operation=="drives"||q.operation=="list"||q.operation=="stat"){
                auto entries=decode_entries(std::get<std::vector<std::byte>>(response.result));if(auto list=std::get_if<std::vector<Entry>>(&entries))for(auto&e:*list)std::cout<<(e.directory?"D":"F")<<'\t'<<e.size<<'\t'<<e.name<<'\n';
            }else if(q.operation=="disk_space"){Reader r{std::get<std::vector<std::byte>>(response.result)};std::cout<<"Available: "<<r.number(8)<<" Total: "<<r.number(8)<<" Free: "<<r.number(8)<<'\n';}else std::cout<<"OK\n";return 0;
        }
        auto mutexName=L"Local\\Crackhood.Broker."+user_sid();Handle singleton(CreateMutexW(nullptr,FALSE,mutexName.c_str()));if(GetLastError()==ERROR_ALREADY_EXISTS)return 0;
        struct Session{std::mutex writes,reads;RequestHistory history;};std::mutex sessionsMutex;std::map<std::string,std::shared_ptr<Session>>sessions;Cache metadata;
        for(;;){auto name=pipe_name();HANDLE raw=create_server_pipe(name);if(raw==INVALID_HANDLE_VALUE){Sleep(100);continue;}
            if(!connect_server_pipe(raw,60000)){CloseHandle(raw);continue;}
            std::thread([raw,&sessions,&sessionsMutex,&metadata]{
                Handle pipe(raw);auto bytes=read_frame(pipe,3000);auto req=decode_request(bytes);if(auto q=std::get_if<Request>(&req)){
                    std::shared_ptr<Session> session;{std::lock_guard lock(sessionsMutex);auto&ptr=sessions[q->console.value];if(!ptr)ptr=std::make_shared<Session>();session=ptr;}
                    bool mutation=q->operation.starts_with("job_")||q->operation=="rename"||q->operation=="mkdir"||q->operation=="delete"||q->operation=="upload"||q->operation.starts_with("reboot")||q->operation=="sync_time"||q->operation=="launch_xex";
                    Handle queuedCancel(CreateEventW(nullptr,TRUE,FALSE,cancel_name(q->id).c_str()));
                    Response response;{std::unique_lock lock(mutation?session->writes:session->reads,std::defer_lock);auto acquired=acquire_session(lock,queuedCancel,q->operation.starts_with("job_")||q->operation=="upload"||q->operation=="download"?3600000:3000);
                        if(auto error=std::get_if<Error>(&acquired))response={q->id,*error};else{
                        auto previous=mutation?session->history.lookup(*q):std::optional<Response>{};
                        auto cached=metadata.get(q->console,q->path);bool listing=q->operation=="list"||q->operation=="drives";
                        if(previous)response=*previous;
                        else if(listing&&q->payload.empty()&&!cached.pending&&!cached.stale)response={q->id,encode_entries(cached.entries)};
                        else{
                            response=isolated(*q,[&](const Response&progress){auto b=encode_response(progress);if(auto out=std::get_if<std::vector<std::byte>>(&b))write_frame(pipe,*out,3000);});
                            if(listing){if(auto payload=std::get_if<std::vector<std::byte>>(&response.result)){auto entries=decode_entries(*payload);if(auto e=std::get_if<std::vector<Entry>>(&entries))metadata.publish(q->console,q->path,cached.generation,*e);}}
                            else if((q->operation=="rename"||q->operation=="mkdir"||q->operation=="delete"||q->operation=="job_delete"||q->operation=="upload"||q->operation=="job_upload"||q->operation=="job_move"||q->operation=="job_upload_many"||q->operation=="job_move_many"||q->operation=="job_delete_many")){
                                metadata.invalidate(q->console,q->path);if(q->operation.ends_with("_many"))notify_mutation(q->console,q->path);auto pos=q->path.value.find_last_of('\\');auto parent=q->path.value.substr(0,pos);if(parent.ends_with(':'))parent+='\\';metadata.invalidate(q->console,{parent});notify_mutation(q->console,{parent});if(q->operation=="rename"||q->operation=="job_move"){try{Reader destination{q->payload};auto target=destination.text();auto separator=target.find_last_of('\\');auto folder=target.substr(0,separator);if(folder.ends_with(':'))folder+='\\';metadata.invalidate(q->console,{folder});notify_mutation(q->console,{folder});}catch(const std::exception&){} }if(q->operation=="job_move_many"){try{Reader reader{q->payload};std::vector<std::string>sources{reader.text()};reader.number(1);auto count=reader.number(4);if(count&&count<=65536)while(--count)sources.push_back(reader.text());for(auto&source:sources){auto parentPath=source.substr(0,source.find_last_of('\\'));if(parentPath.ends_with(':'))parentPath+='\\';metadata.invalidate(q->console,{parentPath});notify_mutation(q->console,{parentPath});}}catch(const std::exception&){} }
                            }
                        }
                        if(mutation&&!previous)session->history.remember(*q,response);
                        }
                    }
                    auto b=encode_response(response);if(auto out=std::get_if<std::vector<std::byte>>(&b))write_frame(pipe,*out,3000);
                }
            }).detach();
        }
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
