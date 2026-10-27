#include "test.hpp"
#include "broker/sdk_transport.hpp"
#include "broker/actions.hpp"
using namespace crackhood;
TEST(sdk_hresult_mapping){CHECK(sdk_error(int32_t(0x82DA0100)).code==ErrorCode::Offline);CHECK(sdk_error(int32_t(0x82DA0002)).code==ErrorCode::NotFound);CHECK(sdk_error(int32_t(0x82DA000e)).code==ErrorCode::AccessDenied);CHECK(sdk_error(int32_t(0x82DA000f)).code==ErrorCode::DiskFull);CHECK(sdk_error(int32_t(0x82DA0007)).code==ErrorCode::Unsupported);}
TEST(action_sync_time_fields){CHECK(system_time_command(0x11223344,0x55667788)=="setsystime clockhi=0x11223344 clocklo=0x55667788");}
TEST(action_invalid_xex){auto transport=make_sdk_transport({"test"},"127.0.0.1");auto r=transport->action("launch_xex",{"HDD:\\..\\bad.xex"});CHECK(std::holds_alternative<Error>(r));CHECK(std::get<Error>(r).code==ErrorCode::InvalidName);}
TEST(action_invalid_screenshot){auto transport=make_sdk_transport({"test"},"127.0.0.1");CHECK(std::holds_alternative<Error>(transport->action("screenshot",{"relative.bmp"})));}
