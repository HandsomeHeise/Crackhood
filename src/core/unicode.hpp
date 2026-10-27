#pragma once
#include <windows.h>
#include <string>
#include <stdexcept>
namespace crackhood {
inline std::string utf8(std::wstring_view s){if(s.empty())return {};auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);if(!n)throw std::runtime_error("Invalid Unicode");std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
inline std::wstring utf16(std::string_view s){if(s.empty())return {};auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)throw std::runtime_error("Invalid UTF-8");std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
}
