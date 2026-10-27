#pragma once
#include "model.hpp"
#include <string_view>
namespace crackhood { Result<RemotePath> normalize_remote(std::string_view input); bool valid_local_component(std::wstring_view); }
