#include "test.hpp"
#include "broker/shell_job.hpp"
#include "core/binary.hpp"
#include <objbase.h>
#include <atomic>
#include <thread>
using namespace crackhood;
struct Feedback:ShellJobFeedback {
    int begun=0,ended=0,failed=0,conflicts=0;bool cancel=false;RetryChoice choice=RetryChoice::Skip;
    void begin(const Request&)override{++begun;}
    void progress(uint64_t,uint64_t)override{}
    bool cancelled()override{return cancel;}
    void end()override{++ended;}
    RetryChoice conflict(const Error&)override{CHECK(begun==ended);++conflicts;return choice;}
    void failure(const Error&)override{CHECK(begun==ended);++failed;}
};
TEST(shell_job_conflict_retry_preserves_batch){Feedback ui;Writer w;w.text("HDD:\\dest");w.number(0,1);w.number(2,4);w.text("C:\\second");Request q{1,919,{"rgh"},"job_upload_many",{"C:\\first"},w.bytes};int calls=0;auto result=run_shell_job(q,ui,[&](Request request,auto){CHECK(request.id==919);if(!calls++)return Response{request.id,Error{ErrorCode::Conflict,"Existing file"}};Reader r{request.payload};CHECK(r.text()=="HDD:\\dest");CHECK(r.number(1)==2);CHECK(r.number(4)==2);CHECK(r.text()=="C:\\second");CHECK(r.offset==r.bytes.size());return Response{request.id,std::vector<std::byte>{}};},[](uint64_t){});CHECK(std::holds_alternative<std::vector<std::byte>>(result.result));CHECK(calls==2&&ui.conflicts==1&&ui.begun==2&&ui.ended==2);}
TEST(shell_job_cancel_reaches_worker){Feedback ui;ui.cancel=true;std::atomic_bool cancel=false;auto result=run_shell_job({1,72,{"rgh"},"job_upload",{},{}},ui,[&](Request q,auto){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);while(!cancel&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));return Response{q.id,Error{cancel?ErrorCode::Cancelled:ErrorCode::Timeout,"Stopped"}};},[&](uint64_t id){CHECK(id==72);cancel=true;});CHECK(std::get<Error>(result.result).code==ErrorCode::Cancelled);CHECK(ui.begun==1&&ui.ended==1&&ui.failed==0);}
TEST(shell_job_failure_closes_progress_first){Feedback ui;auto result=run_shell_job({1,73,{"rgh"},"job_download",{},{}},ui,[](Request q,auto){return Response{q.id,Error{ErrorCode::Offline,"Offline"}};},[](uint64_t){});CHECK(std::get<Error>(result.result).code==ErrorCode::Offline);CHECK(ui.begun==1&&ui.ended==1&&ui.failed==1);}
TEST(shell_native_progress_can_restart_after_conflict){auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);struct ComLifetime{HRESULT hr;~ComLifetime(){if(SUCCEEDED(hr))CoUninitialize();}}lifetime{initialized};auto ui=make_native_job_feedback();Request q{1,74,{"rgh"},"job_upload",{"HDD:\\progress-test"},{}};ui->begin(q);ui->end();ui->begin(q);ui->end();}
