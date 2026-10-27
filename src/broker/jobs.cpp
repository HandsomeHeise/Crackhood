#include "jobs.hpp"
#include "core/path.hpp"
#include "core/unicode.hpp"
#include "core/binary.hpp"
#include <fstream>
#include <objbase.h>
namespace crackhood {
static bool failed(const Result<Unit>&r){return std::holds_alternative<Error>(r);}
static RemotePath child(const RemotePath&p,const std::string&name){return {p.value+(p.value.back()=='\\'?"":"\\")+name};}
Result<Unit> TransferJob::run(const JobSpec&input,std::stop_token stop,Progress progress){
    auto q=input;for(auto p:{&q.source,&q.target})if(!p->value.empty()){auto normalized=normalize_remote(p->value);if(auto e=std::get_if<Error>(&normalized))return *e;*p=std::get<RemotePath>(normalized);}
    auto cancelled=[&]()->Result<Unit>{return Error{ErrorCode::Cancelled,"Transfer cancelled"};};
    auto conflict=[&]()->Result<Unit>{return Error{ErrorCode::Conflict,"Destination file already exists"};};
    if(stop.stop_requested()||q.conflict==ConflictPolicy::Cancel)return cancelled();
    auto suffix=".crackhood-"+std::to_string(q.id);
    std::function<Result<Unit>(const std::filesystem::path&,const RemotePath&,unsigned)> upload;
    std::function<Result<Unit>(const RemotePath&,const std::filesystem::path&,unsigned)> download;
    std::function<Result<Unit>(const RemotePath&,unsigned)> remove;
    uint64_t removedItems=0;
    upload=[&](const std::filesystem::path&local,const RemotePath&remote,unsigned depth)->Result<Unit>{
        if(stop.stop_requested())return cancelled();if(depth>64)return Error{ErrorCode::Unsupported,"Directory nesting is too deep"};
        if(std::holds_alternative<Error>(normalize_remote(remote.value)))return Error{ErrorCode::InvalidName,"Unsupported Xbox filename"};
        std::error_code ec;auto attributes=std::filesystem::symlink_status(local,ec);auto native=GetFileAttributesW(local.c_str());if(ec||std::filesystem::is_symlink(attributes)||native==INVALID_FILE_ATTRIBUTES||(native&FILE_ATTRIBUTE_REPARSE_POINT))return Error{ErrorCode::InvalidName,"Source is missing or is a symbolic link"};
        auto existing=transport_.stat(remote);auto target=std::get_if<Entry>(&existing);
        if(auto e=std::get_if<Error>(&existing);e&&e->code!=ErrorCode::NotFound)return *e;
        if(std::filesystem::is_directory(attributes)){
            if(target&&!target->directory)return conflict();
            if(!target){auto r=transport_.mkdir(remote);if(failed(r))return r;}
            for(auto&file:std::filesystem::directory_iterator(local)){auto r=upload(file.path(),child(remote,utf8(file.path().filename().wstring())),depth+1);if(failed(r))return r;}
            return Unit{};
        }
        if(!std::filesystem::is_regular_file(attributes))return Error{ErrorCode::InvalidName,"Not a regular file"};
        if(target){if(q.conflict==ConflictPolicy::Skip)return Unit{};if(q.conflict==ConflictPolicy::Ask||target->directory)return conflict();}
        auto parent=remote.value.substr(0,remote.value.find_last_of('\\')+1);
        RemotePath temp{parent+".CH"+std::to_string(q.id)+".part"};auto temporary=transport_.stat(temp);if(std::holds_alternative<Entry>(temporary))return Error{ErrorCode::AccessDenied,"A temporary file from another or previous job already exists; inspect it before retrying"};if(std::get<Error>(temporary).code!=ErrorCode::NotFound)return std::get<Error>(temporary);auto result=transport_.upload(local,temp,stop,progress);
        if(failed(result)){auto e=std::get<Error>(result);if(e.code==ErrorCode::Offline)e.code=ErrorCode::Uncertain;return e;}
        if(stop.stop_requested())return cancelled();
        auto uploaded=transport_.stat(temp);auto size=std::filesystem::file_size(local);
        if(!std::holds_alternative<Entry>(uploaded)||std::get<Entry>(uploaded).size!=size)return Error{ErrorCode::Uncertain,"Upload destination size could not be verified"};
        RemotePath backup{parent+".CH"+std::to_string(q.id)+".backup"};
        if(target){auto r=transport_.rename(remote,backup);if(failed(r))return r;}
        auto r=transport_.rename(temp,remote);
        if(failed(r)){if(target){auto rollback=transport_.rename(backup,remote);if(failed(rollback))return Error{ErrorCode::Uncertain,"Could not replace or restore destination; backup was kept"};}return r;}
        if(target){auto clean=transport_.remove(backup,false);if(failed(clean))return Error{ErrorCode::Uncertain,"New file is complete; could not remove backup"};}
        return Unit{};
    };
    download=[&](const RemotePath&remote,const std::filesystem::path&local,unsigned depth)->Result<Unit>{
        if(stop.stop_requested())return cancelled();if(depth>64)return Error{ErrorCode::Unsupported,"Directory nesting is too deep"};
        if(!local.filename().empty()&&!valid_local_component(local.filename().wstring()))return Error{ErrorCode::InvalidName,"Unsupported Windows filename"};
        auto native=GetFileAttributesW(local.c_str());if(native!=INVALID_FILE_ATTRIBUTES&&(native&FILE_ATTRIBUTE_REPARSE_POINT))return Error{ErrorCode::InvalidName,"Download destination is a symbolic link"};
        auto metadata=transport_.stat(remote);auto entry=std::get_if<Entry>(&metadata);if(!entry)return std::get<Error>(metadata);
        if(entry->directory){
            if(std::filesystem::exists(local)&&!std::filesystem::is_directory(local))return conflict();
            std::filesystem::create_directories(local);auto contents=transport_.list(remote);auto list=std::get_if<std::vector<Entry>>(&contents);if(!list)return std::get<Error>(contents);
            for(auto&e:*list){if(e.name=="."||e.name==".."||e.name.find_first_of("\\/:")!=std::string::npos)return Error{ErrorCode::InvalidName,"Unsafe remote filename"};auto r=download(child(remote,e.name),local/utf16(e.name),depth+1);if(failed(r))return r;}return Unit{};
        }
        if(std::filesystem::exists(local)){if(q.conflict==ConflictPolicy::Skip)return Unit{};if(q.conflict==ConflictPolicy::Ask)return conflict();}
        auto temp=std::filesystem::path(local.wstring()+utf16(suffix+".part"));if(std::filesystem::exists(temp))return Error{ErrorCode::AccessDenied,"Temporary destination already exists; inspect it before retrying"};
        auto r=transport_.download(remote,temp,stop,progress);if(failed(r))return r;
        if(stop.stop_requested())return cancelled();
        if(std::filesystem::file_size(temp)!=entry->size)return Error{ErrorCode::Protocol,"Download size does not match"};
        if(!MoveFileExW(temp.c_str(),local.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return Error{ErrorCode::AccessDenied,"Could not replace local destination file",static_cast<int32_t>(GetLastError())};
        return Unit{};
    };
    remove=[&](const RemotePath&remote,unsigned depth)->Result<Unit>{
        if(stop.stop_requested())return cancelled();if(depth>64||remote.value.size()<4||remote.value.ends_with(":\\"))return Error{ErrorCode::InvalidName,"A drive root cannot be deleted"};
        auto s=transport_.stat(remote);auto e=std::get_if<Entry>(&s);if(!e)return std::get<Error>(s);
        if(e->directory){auto r=transport_.list(remote);auto contents=std::get_if<std::vector<Entry>>(&r);if(!contents)return std::get<Error>(r);for(auto&item:*contents){if(item.name=="."||item.name==".."||item.name.find_first_of("\\/:")!=std::string::npos)return Error{ErrorCode::InvalidName,"Unsafe remote filename"};auto rr=remove(child(remote,item.name),depth+1);if(failed(rr))return rr;}}
        auto result=transport_.remove(remote,e->directory);if(!failed(result)&&progress)progress(++removedItems,0);return result;
    };
    try{
        if(q.operation=="upload_many"||q.operation=="move_many"||q.operation=="delete_many"){
            if(q.sources.empty()||q.sources.size()>65536)return Error{ErrorCode::Protocol,"Invalid number of source files"};
            for(auto&value:q.sources){
                if(stop.stop_requested())return cancelled();
                Result<Unit>result=Unit{};
                if(q.operation=="upload_many"){auto local=std::filesystem::path(utf16(value));result=upload(local,child(q.target,utf8(local.filename().wstring())),0);}
                else{auto normalized=normalize_remote(value);if(auto e=std::get_if<Error>(&normalized))return *e;auto source=std::get<RemotePath>(normalized);if(q.operation=="delete_many")result=remove(source,0);else{auto name=source.value.substr(source.value.find_last_of('\\')+1);if(name.empty())return Error{ErrorCode::InvalidName,"A drive root cannot be moved"};auto item=q;item.operation="move";item.source=source;item.target=child(q.target,name);item.sources.clear();result=run(item,stop,progress);}}
                if(failed(result))return result;
            }return Unit{};
        }
        if(q.operation=="upload")return upload(q.local,q.target,0);
        if(q.operation=="download")return download(q.source,q.local,0);
        if(q.operation=="delete")return remove(q.source,0);
        if(q.operation=="move"){
            if(std::holds_alternative<Error>(normalize_remote(q.source.value))||std::holds_alternative<Error>(normalize_remote(q.target.value))||q.source.value.ends_with(":\\")||q.target.value.ends_with(":\\"))return Error{ErrorCode::InvalidName,"Invalid move destination"};
            if(_stricmp(q.source.value.c_str(),q.target.value.c_str())==0)return Unit{};
            if(_strnicmp(q.target.value.c_str(),(q.source.value+"\\").c_str(),q.source.value.size()+1)==0)return Error{ErrorCode::InvalidName,"A folder cannot be moved into itself"};
            if(q.source.value.substr(0,q.source.value.find(':'))!=q.target.value.substr(0,q.target.value.find(':'))){
                auto source=transport_.stat(q.source);if(auto e=std::get_if<Error>(&source))return *e;
                if(std::get<Entry>(source).directory)return Error{ErrorCode::Unsupported,"Moving folders across drives requires atomic source protection; please copy instead"};
                auto existing=transport_.stat(q.target);auto target=std::get_if<Entry>(&existing);
                if(target){if(q.conflict==ConflictPolicy::Skip)return Unit{};if(q.conflict!=ConflictPolicy::Replace||target->directory)return conflict();}else if(std::get<Error>(existing).code!=ErrorCode::NotFound)return std::get<Error>(existing);
                GUID guid{};if(FAILED(CoCreateGuid(&guid)))return Error{ErrorCode::AccessDenied,"Could not create temporary folder"};wchar_t name[64]{};StringFromGUID2(guid,name,64);
                auto root=std::filesystem::temp_directory_path()/L"Crackhood-Moves"/name;std::filesystem::create_directories(root);auto original=root/L"source",verified=root/L"verified",current=root/L"current";
                auto received=download(q.source,original,0);if(failed(received))return received;
                RemotePath backup{q.target.value.substr(0,q.target.value.find_last_of('\\')+1)+".CH"+std::to_string(q.id)+".backup"};
                if(target){auto saved=transport_.rename(q.target,backup);if(failed(saved))return saved;}
                auto rollback=[&](Result<Unit>failure)->Result<Unit>{if(target){auto now=transport_.stat(q.target);if(std::holds_alternative<Entry>(now)){auto removed=transport_.remove(q.target,false);if(failed(removed))return Error{ErrorCode::Uncertain,"Destination backup was kept; rollback failed"};}auto restored=transport_.rename(backup,q.target);if(failed(restored))return Error{ErrorCode::Uncertain,"Destination backup was kept; restoration could not be verified"};}return failure;};
                auto sent=upload(original,q.target,0);if(failed(sent))return rollback(sent);
                auto checked=download(q.target,verified,0);if(failed(checked))return rollback(checked);
                auto equal=[](const std::filesystem::path&a,const std::filesystem::path&b){if(!std::filesystem::is_regular_file(b)||std::filesystem::file_size(a)!=std::filesystem::file_size(b))return false;std::ifstream x(a,std::ios::binary),y(b,std::ios::binary);return std::equal(std::istreambuf_iterator<char>(x),std::istreambuf_iterator<char>(),std::istreambuf_iterator<char>(y),std::istreambuf_iterator<char>());};
                if(!equal(original,verified))return rollback(Error{ErrorCode::Uncertain,"Destination contents could not be verified; source was kept"});
                if(stop.stop_requested())return rollback(cancelled());
                RemotePath quarantine{q.source.value.substr(0,q.source.value.find_last_of('\\')+1)+".CH"+std::to_string(q.id)+".move"};
                auto held=transport_.rename(q.source,quarantine);if(failed(held))return rollback(held);
                auto release=[&](Result<Unit>failure)->Result<Unit>{auto restored=transport_.rename(quarantine,q.source);if(failed(restored))return Error{ErrorCode::Uncertain,"Source was kept under its .CH job name; restoration failed"};return rollback(failure);};
                auto fresh=download(quarantine,current,0);if(failed(fresh))return release(fresh);
                if(!equal(original,current))return release(Error{ErrorCode::Uncertain,"Source changed during transfer; it was kept"});
                if(stop.stop_requested())return release(cancelled());
                auto deleted=transport_.remove(quarantine,false);if(failed(deleted))return release(deleted);
                if(target){auto clean=transport_.remove(backup,false);if(failed(clean))return Error{ErrorCode::Uncertain,"File moved; destination backup was kept"};}
                std::filesystem::remove_all(root);return Unit{};
            }
            auto source=transport_.stat(q.source);if(auto e=std::get_if<Error>(&source))return *e;
            auto existing=transport_.stat(q.target);auto target=std::get_if<Entry>(&existing);
            if(auto e=std::get_if<Error>(&existing);e&&e->code!=ErrorCode::NotFound)return *e;
            if(!target)return transport_.rename(q.source,q.target);
            if(q.conflict==ConflictPolicy::Skip)return Unit{};
            if(q.conflict!=ConflictPolicy::Replace||target->directory||std::get<Entry>(source).directory)return conflict();
            RemotePath backup{q.target.value.substr(0,q.target.value.find_last_of('\\')+1)+".CH"+std::to_string(q.id)+".backup"};
            auto saved=transport_.rename(q.target,backup);if(failed(saved))return saved;
            auto moved=transport_.rename(q.source,q.target);
            if(failed(moved)){auto rollback=transport_.rename(backup,q.target);if(failed(rollback))return Error{ErrorCode::Uncertain,"Move failed; destination backup was kept"};return moved;}
            auto clean=transport_.remove(backup,false);if(failed(clean))return Error{ErrorCode::Uncertain,"Moved; could not remove destination backup"};
            return Unit{};
        }
        return Error{ErrorCode::Unsupported,"Unsupported file operation"};
    }catch(const std::exception&e){return Error{ErrorCode::AccessDenied,e.what()};}
}
std::vector<std::byte> encode_job_status(const JobStatus&s){Writer w;w.number(s.id,8);w.number(static_cast<uint8_t>(s.state),1);w.number(s.completed_bytes,8);w.number(s.total_bytes,8);w.number(s.completed_items,8);w.text(s.current_path);w.number(s.error.has_value(),1);if(s.error){w.number(static_cast<uint32_t>(s.error->code),4);w.number(static_cast<uint32_t>(s.error->native_code),4);w.text(s.error->message);}return w.bytes;}
Result<JobStatus> decode_job_status(std::span<const std::byte>b){try{Reader r{b};JobStatus s;s.id=r.number(8);auto state=r.number(1);if(state>static_cast<uint8_t>(JobState::Cancelled))throw std::runtime_error("Invalid job state");s.state=static_cast<JobState>(state);s.completed_bytes=r.number(8);s.total_bytes=r.number(8);s.completed_items=r.number(8);s.current_path=r.text();if(r.number(1)){auto code=r.number(4);if(code>static_cast<uint32_t>(ErrorCode::Uncertain))throw std::runtime_error("Invalid error code");auto native=static_cast<int32_t>(r.number(4));s.error=Error{static_cast<ErrorCode>(code),r.text(),native};}if(r.offset!=b.size())throw std::runtime_error("Trailing job status data");return s;}catch(const std::exception&e){return Error{ErrorCode::Protocol,e.what()};}}
}
