#include "test.hpp"
#include "broker/jobs.hpp"
#include "core/unicode.hpp"
#include "broker/cancellation.hpp"
#include <windows.h>
#include <map>
#include <fstream>
using namespace crackhood;
class MemoryTransport:public ITransport {
public:
    std::map<std::string,std::string> files;
    bool interrupted=false,commit_disconnect=false;
    bool corrupt_upload=false,change_source_on_verify=false;
    Result<std::vector<Entry>> drives()override{return std::vector<Entry>{};}
    Result<std::vector<Entry>> list(const RemotePath&)override{return std::vector<Entry>{};}
    Result<Entry> stat(const RemotePath&p)override{auto it=files.find(p.value);if(it==files.end())return Error{ErrorCode::NotFound,"missing"};return Entry{p.value,false,it->second.size()};}
    Result<Unit> download(const RemotePath&p,const std::filesystem::path&local,std::stop_token,Progress)override{std::ofstream f(local,std::ios::binary);f<<files[p.value];if(change_source_on_verify&&p.value=="media:\\target")files["HDD:\\source"]="changed";if(interrupted)return Error{ErrorCode::Offline,"disconnected"};return Unit{};}
    Result<Unit> upload(const std::filesystem::path&local,const RemotePath&p,std::stop_token,Progress)override{std::ifstream f(local,std::ios::binary);files[p.value]=std::string(std::istreambuf_iterator<char>(f),{});if(corrupt_upload&&!files[p.value].empty())files[p.value][0]='!';if(commit_disconnect)return Error{ErrorCode::Offline,"uncertain"};return Unit{};}
    Result<Unit> mkdir(const RemotePath&)override{return Unit{};}
    Result<Unit> rename(const RemotePath&a,const RemotePath&b)override{auto it=files.find(a.value);if(it==files.end())return Error{ErrorCode::NotFound,"missing"};if(files.contains(b.value))return Error{ErrorCode::Conflict,"exists"};files[b.value]=it->second;files.erase(it);return Unit{};}
    Result<Unit> remove(const RemotePath&p,bool)override{files.erase(p.value);return Unit{};}
    Result<Unit> action(std::string_view,const RemotePath&)override{return Unit{};}
};
struct LocalFile {
    std::filesystem::path path=std::filesystem::temp_directory_path()/std::filesystem::path("Crackhood-test-"+std::to_string(GetTickCount64())+"-"+std::to_string(GetCurrentThreadId())+".bin");
    LocalFile(){std::ofstream f(path,std::ios::binary);f<<"new-content";}
    ~LocalFile(){std::error_code ec;std::filesystem::remove(path,ec);}
};
TEST(job_conflict_skip){MemoryTransport t;t.files["HDD:\\target"]="original";LocalFile f;JobSpec q{42,{"rgh"},"upload",f.path,{}, {"HDD:\\target"},ConflictPolicy::Skip};CHECK(std::holds_alternative<Unit>(TransferJob(t).run(q,{})));CHECK(t.files["HDD:\\target"]=="original");}
TEST(job_conflict_ask){MemoryTransport t;t.files["HDD:\\target"]="original";LocalFile f;JobSpec q{42,{"rgh"},"upload",f.path,{}, {"HDD:\\target"},ConflictPolicy::Ask};auto r=TransferJob(t).run(q,{});CHECK(std::holds_alternative<Error>(r));CHECK(std::get<Error>(r).code==ErrorCode::Conflict);CHECK(t.files["HDD:\\target"]=="original");}
TEST(job_cancel_preserves_source){MemoryTransport t;t.files["HDD:\\source"]="original";std::stop_source stop;stop.request_stop();JobSpec q{42,{"rgh"},"move",{}, {"HDD:\\source"},{"HDD:\\target"}};auto r=TransferJob(t).run(q,stop.get_token());CHECK(std::holds_alternative<Error>(r));CHECK(t.files["HDD:\\source"]=="original");CHECK(!t.files.contains("HDD:\\target"));}
TEST(job_upload_uncertain){MemoryTransport t;t.files["HDD:\\target"]="original";t.commit_disconnect=true;LocalFile f;JobSpec q{42,{"rgh"},"upload",f.path,{}, {"HDD:\\target"},ConflictPolicy::Replace};auto r=TransferJob(t).run(q,{});CHECK(std::holds_alternative<Error>(r));CHECK(t.files["HDD:\\target"]=="original");}
TEST(job_upload_replace){MemoryTransport t;t.files["HDD:\\target"]="original";LocalFile f;JobSpec q{42,{"rgh"},"upload",f.path,{}, {"HDD:\\target"},ConflictPolicy::Replace};CHECK(std::holds_alternative<Unit>(TransferJob(t).run(q,{})));CHECK(t.files["HDD:\\target"]=="new-content");}
TEST(job_upload_preserves_existing_temporary_file){MemoryTransport t;t.files["HDD:\\.CH42.part"]="previous-job";LocalFile f;JobSpec q{42,{"rgh"},"upload",f.path,{}, {"HDD:\\target"},ConflictPolicy::Replace};CHECK(std::holds_alternative<Error>(TransferJob(t).run(q,{})));CHECK(t.files["HDD:\\.CH42.part"]=="previous-job");CHECK(!t.files.contains("HDD:\\target"));}
TEST(job_skip_missing_target_uploads){MemoryTransport t;LocalFile f;JobSpec q{42,{"rgh"},"upload",f.path,{}, {"HDD:\\target"},ConflictPolicy::Skip};CHECK(std::holds_alternative<Unit>(TransferJob(t).run(q,{})));CHECK(t.files.contains("HDD:\\target"));CHECK(t.files["HDD:\\target"]=="new-content");}
TEST(job_download_failure_preserves_target){MemoryTransport t;t.files["HDD:\\source"]="remote";t.interrupted=true;LocalFile f;JobSpec q{42,{"rgh"},"download",f.path,{"HDD:\\source"},{},ConflictPolicy::Replace};CHECK(std::holds_alternative<Error>(TransferJob(t).run(q,{})));std::ifstream input(f.path);CHECK(std::string(std::istreambuf_iterator<char>(input),{})=="new-content");}
TEST(job_move_conflict_preserves_both){MemoryTransport t;t.files["HDD:\\source"]="source";t.files["HDD:\\target"]="old";JobSpec q{42,{"rgh"},"move",{}, {"HDD:\\source"},{"HDD:\\target"},ConflictPolicy::Replace};auto r=TransferJob(t).run(q,{});CHECK(std::holds_alternative<Unit>(r));CHECK(!t.files.contains("HDD:\\source"));CHECK(t.files["HDD:\\target"]=="source");}
TEST(job_cross_drive_verified_move){MemoryTransport t;t.files["HDD:\\source"]="source";JobSpec q{43,{"rgh"},"move",{}, {"HDD:\\source"},{"media:\\target"},ConflictPolicy::Ask};CHECK(std::holds_alternative<Unit>(TransferJob(t).run(q,{})));CHECK(!t.files.contains("HDD:\\source"));CHECK(t.files["media:\\target"]=="source");}
TEST(job_cross_drive_failure_preserves_source){MemoryTransport t;t.files["HDD:\\source"]="source";t.commit_disconnect=true;JobSpec q{44,{"rgh"},"move",{}, {"HDD:\\source"},{"media:\\target"},ConflictPolicy::Ask};CHECK(std::holds_alternative<Error>(TransferJob(t).run(q,{})));CHECK(t.files["HDD:\\source"]=="source");}
TEST(job_cross_drive_corruption_restores_old_target){MemoryTransport t;t.files["HDD:\\source"]="source";t.files["media:\\target"]="old";t.corrupt_upload=true;JobSpec q{45,{"rgh"},"move",{}, {"HDD:\\source"},{"media:\\target"},ConflictPolicy::Replace};CHECK(std::holds_alternative<Error>(TransferJob(t).run(q,{})));CHECK(t.files["HDD:\\source"]=="source");CHECK(t.files["media:\\target"]=="old");}
TEST(job_cross_drive_changed_source_survives){MemoryTransport t;t.files["HDD:\\source"]="source";t.change_source_on_verify=true;JobSpec q{46,{"rgh"},"move",{}, {"HDD:\\source"},{"media:\\target"},ConflictPolicy::Ask};CHECK(std::holds_alternative<Error>(TransferJob(t).run(q,{})));CHECK(t.files["HDD:\\source"]=="changed");}
TEST(job_move_multiple_files){MemoryTransport t;t.files["HDD:\\a"]="a";t.files["HDD:\\b"]="b";JobSpec q{47,{"rgh"},"move_many",{}, {},{"HDD:\\folder"},ConflictPolicy::Ask,{"HDD:\\a","HDD:\\b"}};CHECK(std::holds_alternative<Unit>(TransferJob(t).run(q,{})));CHECK(!t.files.contains("HDD:\\a"));CHECK(!t.files.contains("HDD:\\b"));CHECK(t.files["HDD:\\folder\\a"]=="a");CHECK(t.files["HDD:\\folder\\b"]=="b");}
TEST(job_upload_multiple_files){MemoryTransport t;LocalFile file;auto other=std::filesystem::path(file.path.wstring()+L".two");{std::ofstream f(other);f<<"second";}JobSpec q{48,{"rgh"},"upload_many",{}, {},{"HDD:\\folder"},ConflictPolicy::Ask,{utf8(file.path.wstring()),utf8(other.wstring())}};auto r=TransferJob(t).run(q,{});std::filesystem::remove(other);CHECK(std::holds_alternative<Unit>(r));CHECK(t.files["HDD:\\folder\\"+utf8(file.path.filename().wstring())]=="new-content");CHECK(t.files["HDD:\\folder\\"+utf8(other.filename().wstring())]=="second");}
TEST(job_delete_cancel_stops_siblings){
    auto id=GetTickCount64();Cancellation cancellation(id);
    class DeleteTransport:public MemoryTransport{public:HANDLE event;std::stop_token stop;unsigned removed=0;
        Result<Entry>stat(const RemotePath&p)override{if(p.value=="HDD:\\root")return Entry{"root",true};return MemoryTransport::stat(p);}
        Result<std::vector<Entry>>list(const RemotePath&)override{return std::vector<Entry>{{"a",false,1},{"b",false,1}};}
        Result<Unit>remove(const RemotePath&p,bool directory)override{++removed;auto result=MemoryTransport::remove(p,directory);SetEvent(event);for(unsigned i=0;i<100&&!stop.stop_requested();++i)Sleep(5);return result;}
    }t;
    Handle event(CreateEventW(nullptr,TRUE,FALSE,(L"Local\\Crackhood.Cancel."+user_sid()+L"."+std::to_wstring(id)).c_str()));t.event=event;t.stop=cancellation.token();t.files["HDD:\\root\\a"]="a";t.files["HDD:\\root\\b"]="b";
    auto result=TransferJob(t).run({id,{"rgh"},"delete",{}, {"HDD:\\root"},{},ConflictPolicy::Ask},cancellation.token());CHECK(std::holds_alternative<Error>(result));CHECK(t.removed==1);CHECK(t.files.contains("HDD:\\root\\b"));
}
