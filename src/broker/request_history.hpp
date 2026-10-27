#pragma once
#include "ipc/protocol.hpp"
#include <map>
#include <optional>
namespace crackhood {
// Accessed under the console session lock. Retries cannot replay a completed write.
class RequestHistory {
    struct Item{std::vector<std::byte> request;Response response;};
    std::map<uint64_t,Item> items_;
public:
    std::optional<Response> lookup(const Request&q){auto it=items_.find(q.id);if(it==items_.end())return {};auto b=encode_request(q);if(auto bytes=std::get_if<std::vector<std::byte>>(&b);bytes&&*bytes==it->second.request)return it->second.response;return Response{q.id,Error{ErrorCode::Conflict,"Job ID was already used for a different job"}};}
    void remember(const Request&q,const Response&r){if(auto e=std::get_if<Error>(&r.result);e&&e->code==ErrorCode::Conflict)return;auto b=encode_request(q);if(auto bytes=std::get_if<std::vector<std::byte>>(&b)){if(items_.size()>=256)items_.erase(items_.begin());items_.insert_or_assign(q.id,Item{*bytes,r});}}
};
}
