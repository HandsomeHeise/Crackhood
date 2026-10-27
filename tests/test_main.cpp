#include "test.hpp"
#include <iostream>
int main(int argc,char**argv) {
    unsigned failed=0,executed=0;
    for(auto& t:tests()) {
        if(argc>1 && t.name!=argv[1]) continue;
        ++executed;
        try { t.run(); std::cout<<"PASS "<<t.name<<'\n'; }
        catch(const std::exception&e) { ++failed; std::cerr<<"FAIL "<<t.name<<": "<<e.what()<<'\n'; }
    }
    if(!executed) { std::cerr<<"No matching tests\n"; return 2; }
    return failed?1:0;
}
