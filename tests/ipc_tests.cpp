#include "test.hpp"
#include "ipc/protocol.hpp"
#include "core/binary.hpp"
#include "broker/request_history.hpp"
#include "broker/queue_lock.hpp"
#include "ipc/win_pipe.hpp"
#include <thread>
using namespace crackhood;
TEST(ipc_bad_version){Request q;q.version=99;auto r=encode_request(q);CHECK(std::holds_alternative<Error>(r));}
TEST(ipc_oversized){std::vector<std::byte> b(1048577);CHECK(std::holds_alternative<Error>(decode_request(b)));}
TEST(ipc_partial_frame){std::vector<std::byte>b{std::byte('C'),std::byte('R')};CHECK(std::holds_alternative<Error>(decode_request(b)));}
TEST(ipc_roundtrip){Request q{1,42,{"rgh"},"list",{"HDD:\\Games"},{std::byte(1)}};auto b=std::get<std::vector<std::byte>>(encode_request(q));auto r=std::get<Request>(decode_request(b));CHECK(r.id==42);CHECK(r.operation=="list");CHECK(r.path.value=="HDD:\\Games");CHECK(r.payload.size()==1);}
TEST(ipc_error_roundtrip){Response q{42,Error{ErrorCode::Timeout,"offline",123}};auto b=std::get<std::vector<std::byte>>(encode_response(q));auto r=std::get<Response>(decode_response(b));CHECK(r.id==42);CHECK(std::get<Error>(r.result).code==ErrorCode::Timeout);}
TEST(ipc_entries_roundtrip){auto b=encode_entries({{"Games",true,0,123,16},{"default.xex",false,1048576,45,32}});auto r=std::get<std::vector<Entry>>(decode_entries(b));CHECK(r.size()==2);CHECK(r[1].size==1048576);CHECK(r[0].directory);}
TEST(job_duplicate_id){RequestHistory history;Request q{1,123,{"rgh"},"job_delete",{"HDD:\\x"},{}};CHECK(!history.lookup(q));history.remember(q,{123,std::vector<std::byte>{}});CHECK(history.lookup(q));q.path.value="HDD:\\different";CHECK(std::holds_alternative<Error>(history.lookup(q)->result));}
TEST(job_conflict_retry_allowed){RequestHistory history;Request q{1,123,{"rgh"},"job_upload",{"HDD:\\x"},{}};history.remember(q,{123,Error{ErrorCode::Conflict,"exists"}});CHECK(!history.lookup(q));}
TEST(session_cancel_queued_request){std::mutex mutex;std::unique_lock occupied(mutex);Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));Result<Unit>result=Unit{};bool owned=false;std::thread queued([&]{std::unique_lock lock(mutex,std::defer_lock);result=acquire_session(lock,event,3000);owned=lock.owns_lock();});SetEvent(event);queued.join();CHECK(std::holds_alternative<Error>(result));CHECK(std::get<Error>(result).code==ErrorCode::Cancelled);CHECK(!owned);}
TEST(session_metadata_timeout){std::mutex mutex;std::unique_lock occupied(mutex);Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));Result<Unit>result=Unit{};std::thread queued([&]{std::unique_lock lock(mutex,std::defer_lock);result=acquire_session(lock,event,25);});queued.join();CHECK(std::holds_alternative<Error>(result));CHECK(std::get<Error>(result).code==ErrorCode::Timeout);}
