#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>
namespace crackhood {
struct Writer {
    std::vector<std::byte> bytes;
    void number(uint64_t value,unsigned size) { for(unsigned i=0;i<size;++i) bytes.push_back(static_cast<std::byte>((value>>(8*i))&255)); }
    void text(const std::string& s) { if(s.size()>65500) throw std::runtime_error("string too large"); number(s.size(),4); for(unsigned char c:s) bytes.push_back(static_cast<std::byte>(c)); }
};
struct Reader {
    std::span<const std::byte> bytes; size_t offset=0;
    uint64_t number(unsigned size) {
        if(size>8 || offset>bytes.size() || size>bytes.size()-offset) throw std::runtime_error("truncated field");
        uint64_t result=0; for(unsigned i=0;i<size;++i) result|=uint64_t(std::to_integer<unsigned char>(bytes[offset++]))<<(8*i); return result;
    }
    std::string text() { auto n=number(4); if(n>65500 || n>bytes.size()-offset) throw std::runtime_error("invalid string length"); std::string out; out.reserve(static_cast<size_t>(n)); while(n--) out+=static_cast<char>(bytes[offset++]); return out; }
};
}
