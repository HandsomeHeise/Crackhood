#pragma once
#include <windows.h>
#include <objidl.h>
#include <filesystem>
#include <optional>
namespace crackhood { HRESULT stage_virtual_file(IStream*,const std::filesystem::path&,std::optional<uint64_t> expected); }
