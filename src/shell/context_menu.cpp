#include "data_object.hpp"
#include "launch.hpp"
#include "pidl.hpp"
#include "ipc/client.hpp"
#include "ipc/changes.hpp"
#include "broker/profiles.hpp"
#include "core/binary.hpp"
#include "core/unicode.hpp"
#include "core/path.hpp"
#include <wrl/client.h>
#include <thread>
#include <shellapi.h>
#include <algorithm>
namespace crackhood {
using Microsoft::WRL::ComPtr;
class ContextMenu final:public IContextMenu,public IObjectWithSite {
    LONG refs_=1;Node parent_;std::vector<Node>selection_;PIDLIST_ABSOLUTE absolute_=nullptr;
    std::vector<std::pair<std::string,std::wstring>> verbs_;
    ComPtr<IUnknown> site_;
    HRESULT browse(PCIDLIST_ABSOLUTE path){ComPtr<IShellBrowser>browser;if(site_){site_.As(&browser);if(!browser){ComPtr<IServiceProvider>provider;if(SUCCEEDED(site_.As(&provider)))provider->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser));}}if(browser)return browser->BrowseObject(path,SBSP_ABSOLUTE|SBSP_SAMEBROWSER);return SHOpenFolderAndSelectItems(path,0,nullptr,0);}
    HRESULT invoke(const std::string&verb,HWND window){
        if(verb=="settings")return LaunchSettings();
        if(verb=="new_console")return LaunchSettings();
        if(verb=="default_console"||verb=="remove_console"){
            if(selection_.size()!=1||selection_[0].kind!=NodeKind::Console)return E_INVALIDARG;auto node=selection_[0];
            if(verb=="remove_console"&&MessageBoxW(window,L"Remove this console profile? Files on the Xbox will be kept.",L"Crackhood",MB_YESNO|MB_ICONQUESTION)!=IDYES)return S_OK;
            auto profiles=load_profiles();auto rows=std::get_if<std::vector<Profile>>(&profiles);if(!rows)return E_FAIL;
            if(verb=="default_console")for(auto&p:*rows)p.is_default=p.id==node.console;
            else{rows->erase(std::remove_if(rows->begin(),rows->end(),[&](auto&p){return p.id==node.console;}),rows->end());if(!rows->empty()&&std::none_of(rows->begin(),rows->end(),[](auto&p){return p.is_default;}))(*rows)[0].is_default=true;}
            auto saved=save_profiles(*rows);if(auto error=std::get_if<Error>(&saved)){MessageBoxW(window,utf16(error->message).c_str(),L"Crackhood",MB_ICONERROR);return E_FAIL;}notify_mutation({},{});return S_OK;
        }
        if(verb=="rename"){
            if(selection_.size()!=1||!absolute_)return E_INVALIDARG;auto encoded=encode_node(selection_[0]);auto bytes=std::get_if<std::vector<std::byte>>(&encoded);if(!bytes)return E_INVALIDARG;auto item=static_cast<PITEMID_CHILD>(CoTaskMemAlloc(bytes->size()+4));if(!item)return E_OUTOFMEMORY;item->mkid.cb=static_cast<USHORT>(bytes->size()+2);memcpy(item->mkid.abID,bytes->data(),bytes->size());memset(reinterpret_cast<char*>(item)+item->mkid.cb,0,2);PCUITEMID_CHILD selected=item;auto hr=SHOpenFolderAndSelectItems(absolute_,1,&selected,OFASI_EDIT);CoTaskMemFree(item);return hr;
        }
        if(verb=="sync_time"){if(MessageBoxW(window,L"Synchronize the Xbox clock with the current PC time?",L"Crackhood",MB_YESNO|MB_ICONQUESTION)!=IDYES)return S_OK;auto node=selection_.empty()?parent_:selection_[0];return LaunchRequest({1,0,node.console,"sync_time",{}, {}});}
        if(verb=="security_status"){auto node=selection_.empty()?parent_:selection_[0];return LaunchRequest({1,0,node.console,"security_status",{}, {}});}
        if(verb=="check_connection"){auto node=selection_.empty()?parent_:selection_[0];return LaunchRequest({1,0,node.console,"check_connection",{}, {}});}
        if(verb=="screenshot"){auto node=selection_.empty()?parent_:selection_[0];try{auto root=user_root()/L"Screenshots";std::filesystem::create_directories(root);auto path=root/(L"Xbox-"+std::to_wstring(GetTickCount64())+L".bmp");return LaunchRequest({1,0,node.console,"screenshot",{utf8(path.wstring())},{}});}catch(...){return E_FAIL;}}
        if(verb=="refresh"){InvalidateFolder(parent_.console,parent_.path);if(absolute_)SHChangeNotify(SHCNE_UPDATEDIR,SHCNF_IDLIST|SHCNF_FLUSHNOWAIT,absolute_,nullptr);return S_OK;}
        if(verb=="copy"||verb=="cut"){ComPtr<IDataObject>data;auto hr=CreateDataObject(selection_,IID_PPV_ARGS(&data),verb=="cut"?DROPEFFECT_MOVE:DROPEFFECT_COPY);return SUCCEEDED(hr)?OleSetClipboard(data.Get()):hr;}
        if(verb=="paste"){ComPtr<IDataObject>data;auto hr=OleGetClipboard(&data);if(FAILED(hr))return hr;ComPtr<IDropTarget>target;hr=CreateDropTarget(parent_,IID_PPV_ARGS(&target));DWORD effect=DROPEFFECT_COPY;POINTL pt{};if(SUCCEEDED(hr))hr=target->Drop(data.Get(),0,pt,&effect);return hr;}
        if(verb=="mkdir")return LaunchRequest({1,0,parent_.console,"mkdir",parent_.path,{}});
        if(verb=="delete"){if(selection_.empty())return E_INVALIDARG;if(MessageBoxW(window,L"Permanently delete these Xbox files?",L"Crackhood",MB_YESNO|MB_ICONWARNING)!=IDYES)return S_OK;if(selection_.size()==1)return LaunchRequest({1,0,selection_[0].console,"job_delete",selection_[0].path,{}});Writer p;p.text(selection_[0].path.value);p.number(0,1);p.number(selection_.size(),4);for(size_t i=1;i<selection_.size();++i)p.text(selection_[i].path.value);return LaunchRequest({1,0,parent_.console,"job_delete_many",parent_.path,p.bytes});}
        if(verb=="reboot"||verb=="reboot_title"||verb=="reboot_active"){if(MessageBoxW(window,L"Restart the Xbox now?",L"Crackhood",MB_YESNO|MB_ICONQUESTION)!=IDYES)return S_OK;auto node=selection_.empty()?parent_:selection_[0];return LaunchRequest({1,0,node.console,verb,{}, {}});}
        if(verb=="launch_xex"){if(selection_.size()!=1)return E_INVALIDARG;auto&node=selection_[0];return LaunchRequest({1,0,node.console,"launch_xex",node.path,{}});}
        if(verb=="properties"){auto node=selection_.empty()?parent_:selection_[0];InterlockedIncrement(&objects);std::thread([node,window]{try{auto details=utf16(node.entry.name)+L"\n"+utf16(node.path.value)+L"\n"+(node.entry.directory?L"Folder":std::to_wstring(node.entry.size)+L" Bytes");if(node.kind==NodeKind::Drive){auto response=Client{}.call({1,GetTickCount64(),node.console,"disk_space",node.path,{}},std::chrono::milliseconds(4000));if(auto b=std::get_if<std::vector<std::byte>>(&response.result)){Reader r{*b};auto available=r.number(8),total=r.number(8);details+=L"\nFree: "+std::to_wstring(available/(1024*1024))+L" MiB\nTotal: "+std::to_wstring(total/(1024*1024))+L" MiB";}else details+=L"\n"+utf16(std::get<Error>(response.result).message);}MessageBoxW(window,details.c_str(),L"Xbox properties",MB_OK|MB_ICONINFORMATION);}catch(...){MessageBoxW(window,L"Could not load properties.",L"Crackhood",MB_ICONERROR);}InterlockedDecrement(&objects);}).detach();return S_OK;}
        if(verb=="open"){
            if(selection_.empty()&&absolute_)return browse(absolute_);
            if(selection_.size()!=1)return E_INVALIDARG;auto node=selection_[0];
            if(node.kind==NodeKind::AddConsole)return LaunchSettings();
            if(node.entry.directory&&absolute_){auto encoded=encode_node(node);auto b=std::get_if<std::vector<std::byte>>(&encoded);if(!b)return E_INVALIDARG;auto child=static_cast<PITEMID_CHILD>(CoTaskMemAlloc(b->size()+4));if(!child)return E_OUTOFMEMORY;child->mkid.cb=static_cast<USHORT>(b->size()+2);memcpy(child->mkid.abID,b->data(),b->size());memset(reinterpret_cast<char*>(child)+child->mkid.cb,0,2);auto absolute=ILCombine(absolute_,child);CoTaskMemFree(child);auto hr=browse(absolute);CoTaskMemFree(absolute);return hr;}
            if(node.kind!=NodeKind::File)return E_INVALIDARG;
            try{auto name=utf16(node.entry.name);if(!valid_local_component(name))return E_INVALIDARG;GUID guid{};CoCreateGuid(&guid);wchar_t id[64]{};StringFromGUID2(guid,id,64);auto root=user_root()/L"Open"/id;std::filesystem::create_directories(root);auto local=root/name;Writer w;w.text(utf8(local.wstring()));return LaunchRequest({1,0,node.console,"job_download_open",node.path,w.bytes});}catch(...){return E_FAIL;}
        }
        return E_INVALIDARG;
    }
public:
    ContextMenu(Node parent,std::vector<Node>nodes,PCIDLIST_ABSOLUTE absolute):parent_(std::move(parent)),selection_(std::move(nodes)),absolute_(absolute?ILCloneFull(absolute):nullptr){InterlockedIncrement(&objects);}
    ~ContextMenu(){CoTaskMemFree(absolute_);InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(iid==IID_IObjectWithSite)*out=static_cast<IObjectWithSite*>(this);else if(iid==IID_IUnknown||iid==IID_IContextMenu)*out=static_cast<IContextMenu*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown*site)override{site_=site;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;return site_?site_->QueryInterface(iid,out):E_FAIL;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release()override{auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU menu,UINT index,UINT first,UINT last,UINT flags)override{
        verbs_.clear();auto add=[&](const char*verb,const wchar_t*title){if(first+verbs_.size()<=last){InsertMenuW(menu,index++,MF_BYPOSITION|MF_STRING,first+static_cast<UINT>(verbs_.size()),title);verbs_.push_back({verb,title});}};
        if(selection_.size()==1||(selection_.empty()&&(flags&CMF_DEFAULTONLY))){add("open",L"Open");SetMenuDefaultItem(menu,first,FALSE);}
        if(flags&CMF_DEFAULTONLY)return MAKE_HRESULT(SEVERITY_SUCCESS,0,static_cast<USHORT>(verbs_.size()));
        bool files=!selection_.empty();for(auto&n:selection_)files=files&&(n.kind==NodeKind::File||n.kind==NodeKind::Directory);
        if(files){add("copy",L"Copy");add("cut",L"Cut");if(selection_.size()==1)add("rename",L"Rename");add("delete",L"Delete");if(selection_.size()==1&&selection_[0].path.value.size()>4&&_stricmp(selection_[0].path.value.c_str()+selection_[0].path.value.size()-4,".xex")==0)add("launch_xex",L"Launch on Xbox");}
        if(selection_.empty()&&(parent_.kind==NodeKind::Drive||parent_.kind==NodeKind::Directory)){add("paste",L"Paste");add("mkdir",L"New folder");}
        if(selection_.empty()&&parent_.kind==NodeKind::Root)add("new_console",L"Add Xbox 360 …");
        if(selection_.size()==1&&selection_[0].kind==NodeKind::Console){add("default_console",L"Set as default console");add("remove_console",L"Remove console profile");}
        if((selection_.size()==1&&selection_[0].kind==NodeKind::Console)||(selection_.empty()&&parent_.kind==NodeKind::Console)){add("check_connection",L"Check connection");add("screenshot",L"Save screenshot");add("sync_time",L"Synchronize clock");add("security_status",L"Check SDK security");add("reboot_title",L"Restart title");add("reboot_active",L"Restart active title");add("reboot",L"Restart Xbox (cold)");}
        add("refresh",L"Refresh");add("properties",L"Properties");add("settings",L"Crackhood settings");return MAKE_HRESULT(SEVERITY_SUCCESS,0,static_cast<USHORT>(verbs_.size()));
    }
    HRESULT STDMETHODCALLTYPE InvokeCommand(CMINVOKECOMMANDINFO*info)override{
        if(!info)return E_POINTER;std::string verb;
        if(!HIWORD(info->lpVerb)){auto i=LOWORD(info->lpVerb);if(i>=verbs_.size())return E_INVALIDARG;verb=verbs_[i].first;}
        else if(info->cbSize>=sizeof(CMINVOKECOMMANDINFOEX)&&(info->fMask&CMIC_MASK_UNICODE)){auto ex=reinterpret_cast<CMINVOKECOMMANDINFOEX*>(info);if(ex->lpVerbW)verb=utf8(ex->lpVerbW);}
        else verb=info->lpVerb;
        return invoke(verb,info->hwnd);
    }
    HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR command,UINT flags,UINT*,LPSTR text,UINT length)override{
        if(command>=verbs_.size()||!text)return E_INVALIDARG;auto&v=verbs_[command];if(flags==GCS_VERBA)return strcpy_s(text,length,v.first.c_str())==0?S_OK:E_FAIL;if(flags==GCS_VERBW){auto s=utf16(v.first);return wcscpy_s(reinterpret_cast<wchar_t*>(text),length,s.c_str())==0?S_OK:E_FAIL;}if(flags==GCS_VALIDATEA||flags==GCS_VALIDATEW)return S_OK;return E_NOTIMPL;
    }
};
HRESULT CreateContextMenu(Node parent,std::vector<Node>nodes,PCIDLIST_ABSOLUTE absolute,REFIID iid,void**out){if(!out)return E_POINTER;*out=nullptr;auto p=new(std::nothrow)ContextMenu(std::move(parent),std::move(nodes),absolute);if(!p)return E_OUTOFMEMORY;auto hr=p->QueryInterface(iid,out);p->Release();return hr;}
}
