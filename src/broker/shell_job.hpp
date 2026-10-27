#pragma once
#include "ipc/protocol.hpp"
#include <functional>
#include <memory>
namespace crackhood {
enum class RetryChoice { Cancel, Replace, Skip };
struct ShellJobFeedback {
    virtual ~ShellJobFeedback()=default;
    virtual void begin(const Request&)=0;
    virtual void progress(uint64_t,uint64_t)=0;
    virtual bool cancelled()=0;
    virtual void end()=0;
    virtual RetryChoice conflict(const Error&)=0;
    virtual void failure(const Error&)=0;
};
using ShellExecutor=std::function<Response(Request,std::function<void(uint64_t,uint64_t)>)>;
Response run_shell_job(Request,ShellJobFeedback&,ShellExecutor,std::function<void(uint64_t)> cancel);
int run_native_shell_job(const wchar_t*requestFile);
std::unique_ptr<ShellJobFeedback> make_native_job_feedback();
}
