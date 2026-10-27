#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <span>
#include <cstddef>
#include <stdexcept>
namespace crackhood {
struct Handle { HANDLE value=INVALID_HANDLE_VALUE; explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h){} ~Handle(){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);} Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;operator HANDLE()const{return value;} };
std::wstring user_sid();
std::wstring pipe_name();
bool transfer(HANDLE,bool write,std::span<std::byte>,DWORD timeout);
bool write_frame(HANDLE,std::span<const std::byte>,DWORD timeout);
std::vector<std::byte> read_frame(HANDLE,DWORD timeout);
HANDLE create_server_pipe(const std::wstring&);
bool connect_server_pipe(HANDLE,DWORD timeout);
}
