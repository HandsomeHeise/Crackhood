#include "ipc/client.hpp"
#include "core/unicode.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
using namespace crackhood;
int wmain(int argc,wchar_t**argv){try{
    auto path=argc>1?utf8(argv[1]):"HDD:\\";auto output=argc>2?std::filesystem::path(argv[2]):std::filesystem::path(L"benchmark.csv");
    std::ofstream csv(output);if(!csv)throw std::runtime_error("CSV konnte nicht geöffnet werden");csv<<"operation,iteration,elapsed_ms,entries\n";
    for(auto forced:{true,false}){std::vector<double>times;for(unsigned i=0;i<5;++i){Request q{1,GetTickCount64(),{"rgh"},"list",{path},{}};if(forced)q.payload={std::byte{1}};auto begin=std::chrono::steady_clock::now();auto response=Client{}.call(q,std::chrono::milliseconds(4000));auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();if(auto e=std::get_if<Error>(&response.result))throw std::runtime_error(e->message);auto result=decode_entries(std::get<std::vector<std::byte>>(response.result));if(auto e=std::get_if<Error>(&result))throw std::runtime_error(e->message);auto count=std::get<std::vector<Entry>>(result).size();csv<<(forced?"network_refresh":"broker_cache")<<','<<i+1<<','<<ms<<','<<count<<'\n';times.push_back(ms);}std::sort(times.begin(),times.end());std::cout<<(forced?"network_refresh":"broker_cache")<<": median="<<times[2]<<" ms min="<<times.front()<<" max="<<times.back()<<'\n';}
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
