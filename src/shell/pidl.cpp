#include "pidl.hpp"
#include "core/binary.hpp"
#include "core/path.hpp"
namespace crackhood {
Result<std::vector<std::byte>> encode_node(const Node& n) {
    try {
        if(n.kind>NodeKind::AddConsole) throw std::runtime_error("invalid node kind");
        Writer w; w.number(0x484B5243,4); w.number(1,1); w.number(static_cast<uint8_t>(n.kind),1);
        w.text(n.console.value); w.text(n.path.value); w.text(n.entry.name);
        w.number(n.entry.directory,1); w.number(n.entry.size,8); w.number(n.entry.modified_filetime,8); w.number(n.entry.attributes,4);
        if(w.bytes.size()>65532) throw std::runtime_error("PIDL too large"); return w.bytes;
    } catch(const std::exception&e) { return Error{ErrorCode::Protocol,e.what()}; }
}
Result<Node> decode_node(std::span<const std::byte> bytes) {
    try {
        if(bytes.size()>65532) throw std::runtime_error("PIDL too large");
        Reader r{bytes}; if(r.number(4)!=0x484B5243 || r.number(1)!=1) throw std::runtime_error("invalid PIDL version");
        Node n; n.kind=static_cast<NodeKind>(r.number(1)); if(n.kind>NodeKind::AddConsole) throw std::runtime_error("invalid node kind");
        n.console.value=r.text(); n.path.value=r.text(); n.entry.name=r.text();
        auto dir=r.number(1); if(dir>1) throw std::runtime_error("invalid directory flag"); n.entry.directory=dir!=0;
        n.entry.size=r.number(8); n.entry.modified_filetime=r.number(8); n.entry.attributes=static_cast<uint32_t>(r.number(4));
        if(r.offset!=bytes.size()) throw std::runtime_error("trailing PIDL data");
        if(n.kind>=NodeKind::Drive && n.kind<=NodeKind::File && std::holds_alternative<Error>(normalize_remote(n.path.value))) throw std::runtime_error("invalid remote path");
        return n;
    } catch(const std::exception&e) { return Error{ErrorCode::Protocol,e.what()}; }
}
}
