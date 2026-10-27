#pragma once
#include "core/model.hpp"
#include <span>
#include <cstddef>
namespace crackhood {
struct Request { uint32_t version=1; uint64_t id=0; ConsoleId console; std::string operation; RemotePath path; std::vector<std::byte> payload; };
struct Response { uint64_t id=0; Result<std::vector<std::byte>> result; };
Result<Request> decode_request(std::span<const std::byte>);
Result<std::vector<std::byte>> encode_request(const Request&);
Result<Response> decode_response(std::span<const std::byte>);
Result<std::vector<std::byte>> encode_response(const Response&);
std::vector<std::byte> encode_entries(const std::vector<Entry>&);
Result<std::vector<Entry>> decode_entries(std::span<const std::byte>);
}
