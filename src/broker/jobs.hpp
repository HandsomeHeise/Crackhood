#pragma once
#include "core/transport.hpp"
#include <optional>
#include <span>
#include <cstddef>
namespace crackhood {
enum class ConflictPolicy:uint8_t { Ask,Replace,Skip,Cancel };
enum class JobState:uint8_t { Queued,Running,WaitingForConflict,Cancelling,Completed,Failed,Uncertain,Cancelled };
struct JobSpec { uint64_t id=0; ConsoleId console; std::string operation; std::filesystem::path local; RemotePath source,target; ConflictPolicy conflict=ConflictPolicy::Ask; std::vector<std::string> sources; };
struct JobStatus { uint64_t id=0; JobState state=JobState::Queued; uint64_t completed_bytes=0,total_bytes=0,completed_items=0; std::string current_path; std::optional<Error> error; };
class TransferJob {
    ITransport& transport_;
public:
    explicit TransferJob(ITransport&t):transport_(t){}
    Result<Unit> run(const JobSpec&,std::stop_token,Progress={});
};
std::vector<std::byte> encode_job_status(const JobStatus&);
Result<JobStatus> decode_job_status(std::span<const std::byte>);
}
