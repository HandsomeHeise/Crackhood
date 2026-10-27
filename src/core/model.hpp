#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <compare>
namespace crackhood {
enum class ErrorCode { Offline, Timeout, NotFound, InvalidName, DiskFull, AccessDenied, Unsupported, Protocol, Cancelled, Conflict, Uncertain };
struct Error { ErrorCode code; std::string message; int32_t native_code=0; };
template<class T> using Result=std::variant<T,Error>;
struct Unit {};
struct ConsoleId { std::string value; auto operator<=>(const ConsoleId&) const=default; };
struct RemotePath { std::string value; auto operator<=>(const RemotePath&) const=default; };
struct Entry { std::string name; bool directory=false; uint64_t size=0; uint64_t modified_filetime=0; uint32_t attributes=0; };
struct DiskSpace { uint64_t available=0,total=0,free=0; };
enum class NodeKind:uint8_t { Root,Console,Drive,Directory,File,Status,AddConsole };
struct Node { NodeKind kind=NodeKind::Root; ConsoleId console; RemotePath path; Entry entry; };
}
