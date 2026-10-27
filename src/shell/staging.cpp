#include "staging.hpp"
#include <fstream>
namespace crackhood {
HRESULT stage_virtual_file(IStream*source,const std::filesystem::path&path,std::optional<uint64_t>expected){
    if(!source)return E_POINTER;
    try{std::ofstream file(path,std::ios::binary);if(!file)return STG_E_ACCESSDENIED;char buffer[65536];uint64_t written=0;
        for(;;){ULONG got=0;auto result=source->Read(buffer,sizeof(buffer),&got);if(FAILED(result))return result;if(got>sizeof(buffer))return STG_E_READFAULT;if(!got)break;file.write(buffer,got);if(!file)return STG_E_WRITEFAULT;written+=got;if(expected&&written>*expected)return STG_E_READFAULT;}
        file.flush();if(!file)return STG_E_WRITEFAULT;if(expected&&written!=*expected)return STG_E_READFAULT;return S_OK;
    }catch(...){return STG_E_WRITEFAULT;}
}
}
