#include "test.hpp"
#include "core/cache.hpp"
using namespace crackhood;
TEST(cache_old_generation){Cache c;c.invalidate({"rgh"},{"HDD:\\"});CHECK(!c.publish({"rgh"},{"HDD:\\"},0,{}));}
TEST(cache_two_windows){Cache c;auto a=c.get({"rgh"},{"HDD:\\"}),b=c.get({"rgh"},{"HDD:\\"});CHECK(a.generation==b.generation);CHECK(c.publish({"rgh"},{"HDD:\\"},a.generation,{{"Games",true}}));CHECK(c.get({"rgh"},{"HDD:\\"}).entries.size()==1);CHECK(!c.get({"rgh"},{"HDD:\\"}).pending);}
TEST(cache_invalidate_retains_stale){Cache c;c.publish({"rgh"},{"HDD:\\"},0,{{"Games",true}});c.invalidate({"rgh"},{"HDD:\\"});auto s=c.get({"rgh"},{"HDD:\\"});CHECK(s.stale);CHECK(s.pending);CHECK(s.entries.size()==1);}
