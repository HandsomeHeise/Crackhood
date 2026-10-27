#include "test.hpp"
#include "broker/profiles.hpp"
using namespace crackhood;
TEST(profile_corrupt_file){CHECK(std::holds_alternative<Error>(decode_profiles({})));}
TEST(profile_invalid_address){CHECK(std::holds_alternative<Error>(encode_profiles({{{"rgh"},"RGH","10.10.10.999",true}})));}
TEST(profile_roundtrip){auto bytes=std::get<std::vector<std::byte>>(encode_profiles({{{"rgh"},"Meine Xbox","10.10.10.218",true}}));auto rows=std::get<std::vector<Profile>>(decode_profiles(bytes));CHECK(rows.size()==1);CHECK(rows[0].address=="10.10.10.218");CHECK(rows[0].is_default);}
TEST(profile_empty_list_roundtrip){auto encoded=encode_profiles({});CHECK(std::holds_alternative<std::vector<std::byte>>(encoded));auto decoded=decode_profiles(std::get<std::vector<std::byte>>(encoded));CHECK(std::holds_alternative<std::vector<Profile>>(decoded));CHECK(std::get<std::vector<Profile>>(decoded).empty());}
TEST(profile_invalid_utf8_name){auto encoded=encode_profiles({{{"rgh"},std::string("bad\xff",4),"10.10.10.218",true}});CHECK(std::holds_alternative<Error>(encoded));}
