#include "changes.hpp"
#include "win_pipe.hpp"
#include "core/binary.hpp"
#include "broker/profiles.hpp"
#include "shell/pidl.hpp"
#include <shlobj.h>
namespace crackhood {
struct Epoch{
    Handle mapping;volatile LONG64*value=nullptr;
    Epoch(){auto name=L"Local\\Crackhood.Epoch."+user_sid();mapping.value=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,8,name.c_str());if(mapping.value)value=static_cast<volatile LONG64*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,8));}
    ~Epoch(){if(value)UnmapViewOfFile(const_cast<LONG64*>(value));}
};
static Epoch& epoch(){static Epoch value;return value;}
uint64_t mutation_epoch(){auto p=epoch().value;return p?InterlockedCompareExchange64(p,0,0):0;}
static void append(PIDLIST_ABSOLUTE&pidl,Node node){auto b=encode_node(node);auto bytes=std::get_if<std::vector<std::byte>>(&b);if(!bytes)return;auto child=static_cast<PITEMID_CHILD>(CoTaskMemAlloc(bytes->size()+4));if(!child)return;child->mkid.cb=static_cast<USHORT>(bytes->size()+2);memcpy(child->mkid.abID,bytes->data(),bytes->size());memset(reinterpret_cast<char*>(child)+child->mkid.cb,0,2);auto next=ILCombine(pidl,child);CoTaskMemFree(child);if(next){CoTaskMemFree(pidl);pidl=next;}}
void notify_mutation(ConsoleId console,RemotePath path){
    if(auto p=epoch().value)InterlockedIncrement64(p);
    auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);PIDLIST_ABSOLUTE pidl=nullptr;
    if(SUCCEEDED(SHParseDisplayName(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\::{5B4B37D2-ED9F-4A50-8A76-8DFE13DA7318}",nullptr,&pidl,0,nullptr))){
        auto profiles=load_profiles();if(auto rows=std::get_if<std::vector<Profile>>(&profiles))for(auto&p:*rows)if(p.id==console){append(pidl,{NodeKind::Console,console,{},{p.name+" · "+p.address,true}});break;}
        if(!path.value.empty()){
            auto colon=path.value.find(':');if(colon!=std::string::npos){auto drive=path.value.substr(0,colon);auto current=drive+":\\";append(pidl,{NodeKind::Drive,console,{current},{drive,true}});size_t start=colon+2;while(start<path.value.size()){auto end=path.value.find('\\',start);if(end==std::string::npos)end=path.value.size();auto name=path.value.substr(start,end-start);if(!name.empty()){if(current.back()!='\\')current+='\\';current+=name;append(pidl,{NodeKind::Directory,console,{current},{name,true}});}start=end+1;}}
        }
        SHChangeNotify(SHCNE_UPDATEDIR,SHCNF_IDLIST|SHCNF_FLUSHNOWAIT,pidl,nullptr);CoTaskMemFree(pidl);
    }
    if(SUCCEEDED(initialized))CoUninitialize();
}
}
