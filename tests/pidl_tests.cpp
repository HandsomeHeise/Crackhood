#include "test.hpp"
#include "shell/pidl.hpp"
#include <array>
using namespace crackhood;
TEST(pidl_truncated) { CHECK(std::holds_alternative<Error>(decode_node(std::array<std::byte,1>{}))); }
TEST(pidl_identity) {
    Node n{NodeKind::Directory,{"rgh"},{"HDD:\\Games"},{"Games",true,0,0,0}};
    auto bytes=std::get<std::vector<std::byte>>(encode_node(n));
    auto result=std::get<Node>(decode_node(bytes));
    CHECK(result.path.value=="HDD:\\Games"); CHECK(result.console.value=="rgh"); CHECK(result.entry.directory);
}
TEST(pidl_oversized) { Node n; n.path.value=std::string(65536,'x'); CHECK(std::holds_alternative<Error>(encode_node(n))); }
TEST(pidl_bad_kind) { Node n; n.kind=static_cast<NodeKind>(255); CHECK(std::holds_alternative<Error>(encode_node(n))); }
