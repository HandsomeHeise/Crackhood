#pragma once
#include "core/model.hpp"
#include <filesystem>
#include <span>
#include <cstddef>
namespace crackhood {
struct Profile{ConsoleId id;std::string name,address;bool is_default=false;};
Result<std::vector<Profile>> decode_profiles(std::span<const std::byte>);
Result<std::vector<std::byte>> encode_profiles(const std::vector<Profile>&);
std::filesystem::path user_root();
Result<std::vector<Profile>> load_profiles();
Result<Unit> save_profiles(const std::vector<Profile>&);
}
