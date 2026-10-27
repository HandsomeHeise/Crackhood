#include "test.hpp"
#include "core/path.hpp"
using namespace crackhood;
TEST(path_parent_escape) { CHECK(std::holds_alternative<Error>(normalize_remote("HDD:\\..\\x"))); }
TEST(path_embedded_nul) { CHECK(std::holds_alternative<Error>(normalize_remote(std::string("HDD:\\a\0b",8)))); }
TEST(path_unicode_unrepresentable) { CHECK(std::holds_alternative<Error>(normalize_remote("HDD:\\emoji\xf0\x9f\x98\x80"))); }
TEST(path_command_limit) { CHECK(std::holds_alternative<Error>(normalize_remote("HDD:\\"+std::string(510,'a')))); }
TEST(path_normalize) { CHECK(std::get<RemotePath>(normalize_remote("HDD:/Games//Title/")).value=="HDD:\\Games\\Title"); }
TEST(path_bad_drive) { CHECK(std::holds_alternative<Error>(normalize_remote("bad drive:\\a"))); }
TEST(path_xbdm_buffer_limit){std::string p="HDD:\\";while(p.size()<260)p+="x\\";CHECK(std::holds_alternative<Error>(normalize_remote(p)));}
TEST(local_reserved_device_names){CHECK(!valid_local_component(L"CON"));CHECK(!valid_local_component(L"aux.txt"));CHECK(!valid_local_component(L"LPT1.bin"));CHECK(!valid_local_component(L"file:stream"));CHECK(valid_local_component(L"Prüfung.bin"));}
