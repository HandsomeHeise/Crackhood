#include "sdk_transport.hpp"
#include "xbdm_stream.hpp"
#include "actions.hpp"
#include "core/path.hpp"
#include <windows.h>
#include <xbdm.h>
#include <fstream>
#include <array>
#include <limits>
namespace crackhood {
Error sdk_error(int32_t hresult){
    auto hr=static_cast<HRESULT>(hresult);
    if(hr==XBDM_CANNOTCONNECT||hr==XBDM_CONNECTIONLOST)return {ErrorCode::Offline,"Xbox connection is unavailable",hresult};
    if(hr==XBDM_NOSUCHFILE)return {ErrorCode::NotFound,"File or folder not found",hresult};
    if(hr==XBDM_CANNOTACCESS)return {ErrorCode::AccessDenied,"Access denied",hresult};
    if(hr==XBDM_DEVICEFULL)return {ErrorCode::DiskFull,"Xbox disk is full",hresult};
    if(hr==XBDM_INVALIDCMD)return {ErrorCode::Unsupported,"XBDM does not support this action",hresult};
    if(hr==XBDM_BADFILENAME||hr==XBDM_LINE_TOO_LONG)return {ErrorCode::InvalidName,"Invalid or overly long Xbox filename",hresult};
    if(hr==XBDM_ALREADYEXISTS)return {ErrorCode::Conflict,"Destination file already exists",hresult};
    return {ErrorCode::Protocol,"XBDM error "+std::to_string(uint32_t(hr)),hresult};
}
class SdkTransport final:public ITransport {
    HMODULE module_=nullptr;
    std::string address_;
    template<class F> F api(const char*name){auto p=GetProcAddress(module_,name);if(!p)throw std::runtime_error(std::string("SDK function is missing: ")+name);return reinterpret_cast<F>(p);}
    static Entry entry(const DM_FILE_ATTRIBUTES&a){return {std::string(a.Name,strnlen_s(a.Name,sizeof(a.Name))),(a.Attributes&FILE_ATTRIBUTE_DIRECTORY)!=0,(uint64_t(a.SizeHigh)<<32)|a.SizeLow,(uint64_t(a.ChangeTime.dwHighDateTime)<<32)|a.ChangeTime.dwLowDateTime,a.Attributes};}
    static Result<Unit> result(HRESULT hr){return FAILED(hr)?Result<Unit>{sdk_error(hr)}:Result<Unit>{Unit{}};}
    static bool valid(const RemotePath&p){return std::holds_alternative<RemotePath>(normalize_remote(p.value));}
public:
    explicit SdkTransport(const std::string&address):address_(address){
        wchar_t sdk[32768]{};GetEnvironmentVariableW(L"XEDK",sdk,32768);
        auto path=std::filesystem::path(sdk)/L"bin"/L"x64"/L"xbdm.dll";
        module_=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module_)throw std::runtime_error("Xbox SDK not found");
        auto hr=api<decltype(&DmSetXboxNameNoRegister)>("DmSetXboxNameNoRegister")(address.c_str());
        if(FAILED(hr)){FreeLibrary(module_);module_=nullptr;throw std::runtime_error(sdk_error(hr).message);}
        api<decltype(&DmUseSharedConnection)>("DmUseSharedConnection")(FALSE);
    }
    ~SdkTransport(){if(module_)FreeLibrary(module_);}
    Result<std::vector<Entry>> drives() override{
        char names[1024]{};DWORD size=sizeof(names);auto hr=api<decltype(&DmGetDriveList)>("DmGetDriveList")(names,&size);if(FAILED(hr))return sdk_error(hr);
        std::string s(names,strnlen_s(names,sizeof(names)));std::vector<Entry> out;size_t start=0;
        while(start<s.size()){auto end=s.find(',',start);if(end==std::string::npos)end=s.size();auto name=s.substr(start,end-start);if(!name.empty())out.push_back({name,true});start=end+1;}return out;
    }
    Result<std::vector<Entry>> list(const RemotePath&p) override{
        if(!valid(p))return Error{ErrorCode::InvalidName,"Invalid path"};
        auto walkFn=api<decltype(&DmWalkDir)>("DmWalkDir");auto closeFn=api<decltype(&DmCloseDir)>("DmCloseDir");
        PDM_WALK_DIR walk=nullptr;DM_FILE_ATTRIBUTES a{};std::vector<Entry> out;HRESULT hr;
        while(SUCCEEDED(hr=walkFn(&walk,p.value.c_str(),&a))){out.push_back(entry(a));if(out.size()>65536){if(walk)closeFn(walk);return Error{ErrorCode::Unsupported,"Directory contains too many entries"};}}
        if(walk)closeFn(walk);if(hr!=XBDM_ENDOFLIST)return sdk_error(hr);return out;
    }
    Result<Entry> stat(const RemotePath&p) override{if(!valid(p))return Error{ErrorCode::InvalidName,"Invalid path"};DM_FILE_ATTRIBUTES a{};auto hr=api<decltype(&DmGetFileAttributes)>("DmGetFileAttributes")(p.value.c_str(),&a);if(FAILED(hr))return sdk_error(hr);return entry(a);}
    Result<Unit> download(const RemotePath&remote,const std::filesystem::path&local,std::stop_token stop,Progress progress) override{
        auto sr=stat(remote);auto a=std::get_if<Entry>(&sr);if(!a)return std::get<Error>(sr);
        if(a->directory||a->size>UINT32_MAX)return Error{ErrorCode::Unsupported,"Partial I/O supports files up to 4 GiB"};
        return receive_xbdm_file(address_,remote,local,a->size,stop,progress);
    }
    Result<Unit> upload(const std::filesystem::path&local,const RemotePath&remote,std::stop_token stop,Progress progress) override{
        if(!valid(remote))return Error{ErrorCode::InvalidName,"Invalid path"};
        std::ifstream file(local,std::ios::binary);if(!file)return Error{ErrorCode::AccessDenied,"Could not open local source file"};
        std::error_code ec;auto total=std::filesystem::file_size(local,ec);if(ec)return Error{ErrorCode::AccessDenied,ec.message()};if(total>UINT32_MAX)return Error{ErrorCode::Unsupported,"Partial I/O supports files up to 4 GiB"};
        return send_xbdm_file(address_,remote,local,total,stop,progress);
    }
    Result<Unit> mkdir(const RemotePath&p) override{if(!valid(p))return Error{ErrorCode::InvalidName,"Invalid path"};return result(api<decltype(&DmMkdir)>("DmMkdir")(p.value.c_str()));}
    Result<Unit> rename(const RemotePath&a,const RemotePath&b) override{if(!valid(a)||!valid(b)||a.value.size()+b.value.size()>430)return Error{ErrorCode::InvalidName,"Invalid or overly long rename path"};return result(api<decltype(&DmRenameFile)>("DmRenameFile")(a.value.c_str(),b.value.c_str()));}
    Result<Unit> remove(const RemotePath&p,bool directory) override{if(!valid(p))return Error{ErrorCode::InvalidName,"Invalid path"};return result(api<decltype(&DmDeleteFile)>("DmDeleteFile")(p.value.c_str(),directory));}
    Result<Unit> action(std::string_view verb,const RemotePath&p) override{
        if(verb=="sync_time"){
            FILETIME time{};GetSystemTimeAsFileTime(&time);auto command=system_time_command(time.dwHighDateTime,time.dwLowDateTime);
            auto open=api<decltype(&DmOpenConnection)>("DmOpenConnection");auto close=api<decltype(&DmCloseConnection)>("DmCloseConnection");auto send=api<decltype(&DmSendCommand)>("DmSendCommand");PDM_CONNECTION connection=nullptr;auto hr=open(&connection);if(FAILED(hr))return sdk_error(hr);char response[1024]{};DWORD size=sizeof(response);hr=send(connection,command.c_str(),response,&size);close(connection);return result(hr);
        }
        if(verb=="security_status"){BOOL enabled=FALSE;auto hr=api<decltype(&DmIsSecurityEnabled)>("DmIsSecurityEnabled")(&enabled);if(FAILED(hr))return sdk_error(hr);if(!enabled)return Error{ErrorCode::Unsupported,"XBDM reports that SDK access security is disabled"};return Unit{};}
        if(verb=="screenshot"){
            if(p.value.empty()||!std::filesystem::path(p.value).is_absolute())return Error{ErrorCode::InvalidName,"A local screenshot path is required"};
            for(unsigned char c:p.value)if(c>127)return Error{ErrorCode::Unsupported,"Screenshot SDK requires an ASCII destination path"};
            return result(api<decltype(&DmScreenShot)>("DmScreenShot")(p.value.c_str()));
        }
        if(verb=="check_connection"){auto r=drives();if(auto e=std::get_if<Error>(&r))return *e;return Unit{};}
        if(verb=="reboot")return result(api<decltype(&DmReboot)>("DmReboot")(DMBOOT_COLD));
        if(verb=="reboot_title")return result(api<decltype(&DmReboot)>("DmReboot")(DMBOOT_TITLE));
        if(verb=="reboot_active")return result(api<decltype(&DmRebootEx)>("DmRebootEx")(DMBOOT_TITLE,nullptr,nullptr,nullptr));
        if(verb=="launch_xex"){if(!valid(p)||p.value.size()<4||_stricmp(p.value.c_str()+p.value.size()-4,".xex"))return Error{ErrorCode::InvalidName,"An XEX path is required"};auto pos=p.value.find_last_of('\\');auto dir=p.value.substr(0,pos);return result(api<decltype(&DmRebootEx)>("DmRebootEx")(DMBOOT_TITLE,p.value.c_str(),dir.c_str(),nullptr));}
        return Error{ErrorCode::Unsupported,"Console action is unavailable"};
    }
    Result<DiskSpace> disk_space(const RemotePath&p)override{
        if(!valid(p))return Error{ErrorCode::InvalidName,"Invalid drive path"};auto drive=p.value.substr(0,p.value.find(':')+1)+"\\";ULARGE_INTEGER available{},total{},free{};
        auto hr=api<decltype(&DmGetDiskFreeSpace)>("DmGetDiskFreeSpace")(drive.data(),&available,&total,&free);if(FAILED(hr))return sdk_error(hr);return DiskSpace{available.QuadPart,total.QuadPart,free.QuadPart};
    }
};
std::unique_ptr<ITransport> make_sdk_transport(ConsoleId,std::string address){return std::make_unique<SdkTransport>(address);}
}
