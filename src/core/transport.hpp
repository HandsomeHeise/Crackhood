#pragma once
#include "model.hpp"
#include <functional>
#include <filesystem>
#include <stop_token>
#include <memory>
namespace crackhood {
using Progress=std::function<void(uint64_t,uint64_t)>;
struct ITransport {
    virtual ~ITransport()=default;
    virtual Result<std::vector<Entry>> drives()=0;
    virtual Result<std::vector<Entry>> list(const RemotePath&)=0;
    virtual Result<Entry> stat(const RemotePath&)=0;
    virtual Result<Unit> download(const RemotePath&,const std::filesystem::path&,std::stop_token,Progress)=0;
    virtual Result<Unit> upload(const std::filesystem::path&,const RemotePath&,std::stop_token,Progress)=0;
    virtual Result<Unit> mkdir(const RemotePath&)=0;
    virtual Result<Unit> rename(const RemotePath&,const RemotePath&)=0;
    virtual Result<Unit> remove(const RemotePath&,bool)=0;
    virtual Result<Unit> action(std::string_view,const RemotePath&)=0;
    virtual Result<DiskSpace> disk_space(const RemotePath&){return Error{ErrorCode::Unsupported,"Drive information is unavailable"};}
};
}
