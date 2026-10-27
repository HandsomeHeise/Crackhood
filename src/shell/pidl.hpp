#pragma once
#include "core/model.hpp"
#include <span>
#include <cstddef>
namespace crackhood {
Result<std::vector<std::byte>> encode_node(const Node&);
Result<Node> decode_node(std::span<const std::byte>);
}
