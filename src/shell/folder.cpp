#include "folder.hpp"
#include "appearance.hpp"
#include "diagnostics.hpp"
#include "pidl.hpp"
#include "data_object.hpp"
#include "core/path.hpp"
#include "core/binary.hpp"
#include "ipc/client.hpp"
#include "ipc/changes.hpp"
#include "core/unicode.hpp"
#include "broker/profiles.hpp"
#include <shlwapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <algorithm>
#include <cstring>
#include <new>
#include <thread>
#include <mutex>
#include <map>
namespace crackhood {
struct ShellSnapshot{std::vector<Node>nodes;bool refreshing=false;bool ready=false;uint64_t generation=0;std::chrono::steady_clock::time_point updated{};};
static std::mutex snapshotMutex;
static std::map<std::string,ShellSnapshot> snapshots;
static uint64_t lastEpoch=0;
void InvalidateFolder(ConsoleId id,RemotePath path){std::lock_guard lock(snapshotMutex);auto&s=snapshots[id.value+"|"+path.value];++s.generation;s.updated={};}
static std::wstring wide(const std::string& s) { return utf16(s); }
static HRESULT strret(const std::wstring& text,STRRET* out) {
    if(!out) return E_POINTER;
    out->uType=STRRET_WSTR; auto bytes=(text.size()+1)*sizeof(wchar_t);
    out->pOleStr=static_cast<wchar_t*>(CoTaskMemAlloc(bytes));
    if(!out->pOleStr) return E_OUTOFMEMORY; memcpy(out->pOleStr,text.c_str(),bytes); return S_OK;
}
static PITEMID_CHILD make_item(const Node& n) {
    auto result=encode_node(n); if(auto bytes=std::get_if<std::vector<std::byte>>(&result)) {
        auto size=bytes->size()+2; auto p=static_cast<PITEMID_CHILD>(CoTaskMemAlloc(size+2));
        if(!p) return nullptr; p->mkid.cb=static_cast<USHORT>(size); memcpy(p->mkid.abID,bytes->data(),bytes->size());
        memset(reinterpret_cast<char*>(p)+size,0,2); return p;
    } return nullptr;
}
static Result<Node> parse_item(PCUITEMID_CHILD p) {
    if(!p || p->mkid.cb<2) return Error{ErrorCode::Protocol,"Invalid PIDL"};
    return decode_node({reinterpret_cast<const std::byte*>(p->mkid.abID),size_t(p->mkid.cb-2)});
}
static std::vector<Node> children(const Node& n,PCIDLIST_ABSOLUTE absolute=nullptr) {
    if(n.kind==NodeKind::Root){auto result=load_profiles();std::vector<Node>out{{NodeKind::AddConsole,{},{},{"Add Xbox 360",false}}};if(auto rows=std::get_if<std::vector<Profile>>(&result)){std::stable_sort(rows->begin(),rows->end(),[](auto&a,auto&b){return a.is_default>b.is_default;});for(auto&p:*rows)out.push_back({NodeKind::Console,p.id,{},{p.name+" · "+p.address,true}});}else out.push_back({NodeKind::Status,{},{},{std::get<Error>(result).message,false}});return out;}
    if(n.kind==NodeKind::File)return {};
    const auto key=n.console.value+"|"+n.path.value;
    std::lock_guard lock(snapshotMutex);auto epoch=mutation_epoch();if(epoch!=lastEpoch){for(auto&item:snapshots){++item.second.generation;item.second.updated={};}lastEpoch=epoch;}
    if(!snapshots.contains(key)&&snapshots.size()>=64){auto old=std::find_if(snapshots.begin(),snapshots.end(),[](auto&item){return !item.second.refreshing;});if(old!=snapshots.end())snapshots.erase(old);}
    auto&state=snapshots[key];
    if(!state.refreshing&&std::chrono::steady_clock::now()-state.updated>std::chrono::seconds(5)){
        state.refreshing=true;auto notify=absolute?ILCloneFull(absolute):nullptr;auto generation=state.generation;
        InterlockedIncrement(&objects);
        std::thread([n,key,notify,generation]{
            std::vector<Node>nodes;
            try {
                Request q{1,GetTickCount64(),n.console,n.kind==NodeKind::Console?"drives":"list",n.path,{}};if(generation)q.payload={std::byte{1}};
                auto response=Client{}.call(q,std::chrono::milliseconds(4000));
                if(auto bytes=std::get_if<std::vector<std::byte>>(&response.result)){
                    auto result=decode_entries(*bytes);
                    if(auto entries=std::get_if<std::vector<Entry>>(&result))for(auto&e:*entries){
                        auto path=n.kind==NodeKind::Console?e.name+":\\":n.path.value+(n.path.value.back()=='\\'?"":"\\")+e.name;
                        nodes.push_back({n.kind==NodeKind::Console?NodeKind::Drive:(e.directory?NodeKind::Directory:NodeKind::File),n.console,{path},e});
                    }else response.result=std::get<Error>(result);
                }
                if(auto error=std::get_if<Error>(&response.result))nodes.push_back({NodeKind::Status,n.console,n.path,{"Connection: "+error->message,false}});
            } catch(const std::exception&e){nodes.push_back({NodeKind::Status,n.console,n.path,{e.what(),false}});}
            {std::lock_guard guard(snapshotMutex);auto&s=snapshots[key];if(s.generation==generation){s.nodes=std::move(nodes);s.ready=true;s.updated=std::chrono::steady_clock::now();}s.refreshing=false;}
            if(notify){SHChangeNotify(SHCNE_UPDATEDIR,SHCNF_IDLIST|SHCNF_FLUSHNOWAIT,notify,nullptr);CoTaskMemFree(notify);}
            InterlockedDecrement(&objects);
        }).detach();
    }
    if(!state.ready)return {{NodeKind::Status,n.console,n.path,{"Loading …",false}}};
    return state.nodes;
}
class Enumerator final:public IEnumIDList {
    LONG refs_=1; std::vector<Node> nodes_; size_t cursor_=0;
public:
    explicit Enumerator(std::vector<Node> nodes,size_t cursor=0):nodes_(std::move(nodes)),cursor_(cursor) { InterlockedIncrement(&objects); }
    ~Enumerator() { InterlockedDecrement(&objects); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out) override { if(!out)return E_POINTER; *out=nullptr; if(iid==IID_IUnknown||iid==IID_IEnumIDList) { *out=static_cast<IEnumIDList*>(this); AddRef(); return S_OK;} return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { auto r=InterlockedDecrement(&refs_); if(!r)delete this; return r; }
    HRESULT STDMETHODCALLTYPE Next(ULONG count,PITEMID_CHILD*out,ULONG*fetched) override {
        if(!out || (count!=1&&!fetched))return E_POINTER; ULONG done=0;
        for(;done<count&&cursor_<nodes_.size();++done,++cursor_) { out[done]=make_item(nodes_[cursor_]); if(!out[done]) { if(fetched)*fetched=done;return E_OUTOFMEMORY; } }
        if(fetched)*fetched=done; return done==count?S_OK:S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG n) override { auto available=nodes_.size()-cursor_;cursor_+=std::min<size_t>(n,available);return n<=available?S_OK:S_FALSE; }
    HRESULT STDMETHODCALLTYPE Reset() override { cursor_=0;return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumIDList**out) override { if(!out)return E_POINTER;*out=new(std::nothrow) Enumerator(nodes_,cursor_);return *out?S_OK:E_OUTOFMEMORY; }
};
class Folder final:public IShellFolder2,public IPersistFolder2 {
    LONG refs_=1; Node node_; PIDLIST_ABSOLUTE absolute_=nullptr;
public:
    explicit Folder(Node n):node_(std::move(n)) {InterlockedIncrement(&objects);}
    ~Folder(){CoTaskMemFree(absolute_);InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out) override {
        trace_shell("Folder.QueryInterface",iid);
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==IID_IUnknown||iid==IID_IShellFolder||iid==IID_IShellFolder2)*out=static_cast<IShellFolder2*>(this);
        else if(iid==IID_IPersist||iid==IID_IPersistFolder||iid==IID_IPersistFolder2)*out=static_cast<IPersistFolder2*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release() override{auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID*out) override{if(!out)return E_POINTER;*out=CLSID_Crackhood;return S_OK;}
    HRESULT STDMETHODCALLTYPE Initialize(PCIDLIST_ABSOLUTE p) override {if(!p)return E_INVALIDARG;auto copy=ILCloneFull(p);if(!copy)return E_OUTOFMEMORY;CoTaskMemFree(absolute_);absolute_=copy;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCurFolder(PIDLIST_ABSOLUTE*out) override {if(!out)return E_POINTER;*out=absolute_?ILCloneFull(absolute_):nullptr;return *out?S_OK:E_FAIL;}
    HRESULT STDMETHODCALLTYPE ParseDisplayName(HWND,IBindCtx*,LPWSTR name,ULONG*eaten,PIDLIST_RELATIVE*out,ULONG*attributes) override {
        if(!name||!out)return E_POINTER;*out=nullptr;
        std::wstring input=name;auto sep=input.find(L'\\');auto head=input.substr(0,sep);
        std::vector<Node> candidates;
        if(node_.kind==NodeKind::Console){auto drive=utf8(head);auto path=normalize_remote(drive+":\\");if(std::holds_alternative<RemotePath>(path))candidates.push_back({NodeKind::Drive,node_.console,std::get<RemotePath>(path),{drive,true}});}
        else candidates=children(node_,absolute_);
        for(auto& n:candidates) if(wide(n.entry.name)==head) {
            auto item=make_item(n);if(!item)return E_OUTOFMEMORY;
            if(sep!=std::wstring::npos) { IShellFolder*folder=nullptr;auto hr=BindToObject(item,nullptr,IID_PPV_ARGS(&folder));if(FAILED(hr)){CoTaskMemFree(item);return hr;}
                PIDLIST_RELATIVE tail=nullptr;hr=folder->ParseDisplayName(nullptr,nullptr,name+sep+1,nullptr,&tail,nullptr);folder->Release();
                if(FAILED(hr)){CoTaskMemFree(item);return hr;}*out=ILCombine(item,tail);CoTaskMemFree(item);CoTaskMemFree(tail);
            } else *out=item;
            if(eaten)*eaten=static_cast<ULONG>(input.size());if(attributes){PCUITEMID_CHILD one=*out;GetAttributesOf(1,&one,attributes);}return *out?S_OK:E_OUTOFMEMORY;
        }return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    HRESULT STDMETHODCALLTYPE EnumObjects(HWND,SHCONTF flags,IEnumIDList**out) override {
        trace_shell("Folder.EnumObjects");
        if(!out)return E_POINTER;std::vector<Node> nodes;
        for(auto&n:children(node_,absolute_))if((n.entry.directory&&(flags&SHCONTF_FOLDERS))||(!n.entry.directory&&(flags&SHCONTF_NONFOLDERS)))nodes.push_back(n);
        *out=new(std::nothrow)Enumerator(std::move(nodes));return *out?S_OK:E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE BindToObject(PCUIDLIST_RELATIVE p,IBindCtx*ctx,REFIID iid,void**out) override {
        trace_shell("Folder.BindToObject",iid);
        if(!out)return E_POINTER;*out=nullptr;auto result=parse_item(p);auto n=std::get_if<Node>(&result);if(!n||!n->entry.directory)return E_INVALIDARG;
        auto f=new(std::nothrow)Folder(*n);if(!f)return E_OUTOFMEMORY;
        auto first=ILCloneFirst(p);if(absolute_&&first){auto joined=ILCombine(absolute_,first);if(joined){f->Initialize(joined);CoTaskMemFree(joined);}}CoTaskMemFree(first);
        auto rest=ILNext(p);HRESULT hr;
        if(rest&&!ILIsEmpty(rest))hr=f->BindToObject(rest,ctx,iid,out);else hr=f->QueryInterface(iid,out);f->Release();return hr;
    }
    HRESULT STDMETHODCALLTYPE BindToStorage(PCUIDLIST_RELATIVE,IBindCtx*,REFIID,void**out) override{if(out)*out=nullptr;return E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE CompareIDs(LPARAM column,PCUIDLIST_RELATIVE a,PCUIDLIST_RELATIVE b) override {
        auto ar=parse_item(a),br=parse_item(b);auto an=std::get_if<Node>(&ar),bn=std::get_if<Node>(&br);if(!an||!bn)return E_INVALIDARG;
        int c=0;
        if((an->kind==NodeKind::AddConsole)!=(bn->kind==NodeKind::AddConsole))c=an->kind==NodeKind::AddConsole?-1:1;
        else if(an->entry.directory!=bn->entry.directory)c=an->entry.directory?-1:1;
        else if(LOWORD(column)==1)c=(an->entry.size>bn->entry.size)-(an->entry.size<bn->entry.size);
        else if(LOWORD(column)==3)c=(an->entry.modified_filetime>bn->entry.modified_filetime)-(an->entry.modified_filetime<bn->entry.modified_filetime);
        else c=StrCmpLogicalW(wide(an->entry.name).c_str(),wide(bn->entry.name).c_str());
        if(!c)c=StrCmpLogicalW(wide(an->entry.name).c_str(),wide(bn->entry.name).c_str());
        if(!c)c=an->console.value.compare(bn->console.value);
        if(!c)c=_stricmp(an->path.value.c_str(),bn->path.value.c_str());
        if(!c){auto tailA=ILNext(a),tailB=ILNext(b);bool restA=tailA&&!ILIsEmpty(tailA),restB=tailB&&!ILIsEmpty(tailB);if(restA&&restB){auto first=ILCloneFirst(a);if(!first)return E_OUTOFMEMORY;IShellFolder*folder=nullptr;auto hr=BindToObject(first,nullptr,IID_PPV_ARGS(&folder));CoTaskMemFree(first);if(FAILED(hr))return hr;hr=folder->CompareIDs(column,tailA,tailB);folder->Release();return hr;}c=restA-restB;}
        return MAKE_HRESULT(SEVERITY_SUCCESS,0,static_cast<USHORT>(static_cast<short>((c>0)-(c<0))));
    }
    HRESULT STDMETHODCALLTYPE CreateViewObject(HWND,REFIID iid,void**out) override {
        trace_shell("Folder.CreateViewObject",iid);
        if(!out)return E_POINTER;*out=nullptr;if(iid==IID_IDropTarget)return CreateDropTarget(node_,iid,out);if(iid==IID_IContextMenu)return CreateContextMenu(node_,{},absolute_,iid,out);if(iid!=IID_IShellView)return E_NOINTERFACE;
        auto callback=new(std::nothrow)ViewDefaults(node_.kind);if(!callback)return E_OUTOFMEMORY;
        SFV_CREATE sfv{sizeof(sfv)};sfv.pshf=this;sfv.psfvcb=callback;auto hr=SHCreateShellFolderView(&sfv,reinterpret_cast<IShellView**>(out));callback->Release();return hr;
    }
    HRESULT STDMETHODCALLTYPE GetAttributesOf(UINT count,PCUITEMID_CHILD_ARRAY items,SFGAOF*attrs) override {
        if(!attrs|| (count&&!items))return E_POINTER;
        auto flags=[](const Node& n)->SFGAOF {return SFGAO_HASPROPSHEET|((n.kind==NodeKind::File||n.kind==NodeKind::Directory)?(SFGAO_CANCOPY|SFGAO_CANRENAME|SFGAO_CANDELETE):0)|((n.kind==NodeKind::Root||n.entry.directory)?(SFGAO_FOLDER|SFGAO_HASSUBFOLDER|SFGAO_BROWSABLE):0)|((n.kind==NodeKind::Drive||n.kind==NodeKind::Directory)?SFGAO_DROPTARGET:0);};
        SFGAOF mask=count?~SFGAOF(0):flags(node_);
        for(UINT i=0;i<count;++i){auto r=parse_item(items[i]);auto n=std::get_if<Node>(&r);if(!n)return E_INVALIDARG;mask&=flags(*n);}*attrs&=mask;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetUIObjectOf(HWND,UINT count,PCUITEMID_CHILD_ARRAY items,REFIID iid,UINT*,void**out) override {
        if(!out)return E_POINTER;*out=nullptr;
        std::vector<Node>selected;for(UINT i=0;i<count;++i){auto r=parse_item(items[i]);auto n=std::get_if<Node>(&r);if(!n)return E_INVALIDARG;selected.push_back(*n);}
        if(iid==IID_IDataObject)return CreateDataObject(selected,iid,out);
        if(iid==IID_IContextMenu)return CreateContextMenu(node_,selected,absolute_,iid,out);
        if(iid==IID_IDropTarget&&selected.size()==1)return CreateDropTarget(selected[0],iid,out);
        if(count==1&&(iid==IID_IExtractIconW||iid==IID_IExtractIconA)) {
            const auto&n=selected[0];auto icon=icon_resource(n);
            if(n.kind==NodeKind::Console){auto profiles=load_profiles();if(auto rows=std::get_if<std::vector<Profile>>(&profiles))for(auto&p:*rows)if(p.id==n.console&&p.is_default)icon=104;}
            if(!icon)return SHCreateFileExtractIconW(wide(n.entry.name).c_str(),FILE_ATTRIBUTE_NORMAL,iid,out);
            HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&CreateFolder),&module);
            wchar_t path[32768]{};if(!GetModuleFileNameW(module,path,32768))return HRESULT_FROM_WIN32(GetLastError());
            IDefaultExtractIconInit*extract=nullptr;auto hr=SHCreateDefaultExtractIcon(IID_PPV_ARGS(&extract));if(FAILED(hr))return hr;
            hr=extract->SetNormalIcon(path,-icon);if(SUCCEEDED(hr))hr=extract->SetOpenIcon(path,-icon);if(SUCCEEDED(hr))hr=extract->QueryInterface(iid,out);extract->Release();return hr;
        }
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE GetDisplayNameOf(PCUITEMID_CHILD p,SHGDNF flags,STRRET*out) override {
        auto result=parse_item(p);auto n=std::get_if<Node>(&result);if(!n)return E_INVALIDARG;
        auto name=(flags&SHGDN_FORPARSING)?wide(n->entry.name):display_label(*n);
        if((flags&SHGDN_FORPARSING)&&!(flags&SHGDN_INFOLDER)&&absolute_) {
            PWSTR parent=nullptr; if(SUCCEEDED(SHGetNameFromIDList(absolute_,SIGDN_DESKTOPABSOLUTEPARSING,&parent))){name=std::wstring(parent)+L"\\"+name;CoTaskMemFree(parent);}
        }return strret(name,out);
    }
    HRESULT STDMETHODCALLTYPE SetNameOf(HWND,PCUITEMID_CHILD item,LPCWSTR name,SHGDNF,PITEMID_CHILD*out) override{if(out)*out=nullptr;if(!name)return E_POINTER;auto r=parse_item(item);auto n=std::get_if<Node>(&r);if(!n||(n->kind!=NodeKind::File&&n->kind!=NodeKind::Directory))return E_INVALIDARG;auto newName=utf8(name);if(newName.empty()||newName.find_first_of("\\/:")!=std::string::npos)return E_INVALIDARG;auto newPath=n->path.value.substr(0,n->path.value.find_last_of('\\')+1)+newName;if(std::holds_alternative<Error>(normalize_remote(newPath)))return E_INVALIDARG;Writer w;w.text(newPath);auto reply=Client{}.call({1,GetTickCount64(),n->console,"rename",n->path,w.bytes},std::chrono::milliseconds(3500));if(std::holds_alternative<Error>(reply.result))return E_FAIL;InvalidateFolder(node_.console,node_.path);Node updated=*n;updated.path.value=newPath;updated.entry.name=newName;if(out)*out=make_item(updated);if(absolute_)SHChangeNotify(SHCNE_UPDATEDIR,SHCNF_IDLIST|SHCNF_FLUSHNOWAIT,absolute_,nullptr);return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDefaultSearchGUID(GUID*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumSearches(IEnumExtraSearch**out) override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDefaultColumn(DWORD,ULONG*sort,ULONG*display) override{if(!sort||!display)return E_POINTER;*sort=0;*display=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDefaultColumnState(UINT column,SHCOLSTATEF*out) override {if(!out)return E_POINTER;if(column>3)return E_INVALIDARG;*out=SHCOLSTATE_ONBYDEFAULT|(column==1?SHCOLSTATE_TYPE_INT:SHCOLSTATE_TYPE_STR);return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDetailsEx(PCUITEMID_CHILD p,const SHCOLUMNID*key,VARIANT*out) override {
        if(!key||!out)return E_POINTER;VariantInit(out);auto r=parse_item(p);auto n=std::get_if<Node>(&r);if(!n)return E_INVALIDARG;
        if(IsEqualPropertyKey(*key,PKEY_Size)){out->vt=VT_UI8;out->ullVal=n->entry.size;return S_OK;}
        if(IsEqualPropertyKey(*key,PKEY_DateModified)){if(!n->entry.modified_filetime)return S_OK;out->vt=VT_DATE;FILETIME ft{DWORD(n->entry.modified_filetime),DWORD(n->entry.modified_filetime>>32)};SYSTEMTIME st{};if(!FileTimeToSystemTime(&ft,&st)||!SystemTimeToVariantTime(&st,&out->date))return E_FAIL;return S_OK;}
        std::wstring s;if(IsEqualPropertyKey(*key,PKEY_ItemNameDisplay))s=display_label(*n);else if(IsEqualPropertyKey(*key,PKEY_ItemTypeText))s=type_label(*n);else return E_INVALIDARG;
        out->vt=VT_BSTR;out->bstrVal=SysAllocString(s.c_str());return out->bstrVal?S_OK:E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetDetailsOf(PCUITEMID_CHILD p,UINT col,SHELLDETAILS*out) override {
        if(!out||col>3)return E_INVALIDARG;out->fmt=col==1?LVCFMT_RIGHT:LVCFMT_LEFT;out->cxChar=col==0?36:20;
        if(!p){static const wchar_t* titles[]={L"Name",L"Size",L"Type",L"Date modified"};return strret(titles[col],&out->str);}
        auto r=parse_item(p);auto n=std::get_if<Node>(&r);if(!n)return E_INVALIDARG;std::wstring text;
        if(col==0)text=display_label(*n);
        else if(col==1&&n->kind==NodeKind::File)text=std::to_wstring(n->entry.size)+L" Bytes";
        else if(col==2)text=type_label(*n);
        else if(col==3&&n->entry.modified_filetime){FILETIME ft{DWORD(n->entry.modified_filetime),DWORD(n->entry.modified_filetime>>32)},local{};SYSTEMTIME st{};if(FileTimeToLocalFileTime(&ft,&local)&&FileTimeToSystemTime(&local,&st)){wchar_t date[80]{},time[80]{};GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,DATE_SHORTDATE,&st,nullptr,date,80,nullptr);GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,0,&st,nullptr,time,80);text=std::wstring(date)+L" "+time;}}
        return strret(text,&out->str);
    }
    HRESULT STDMETHODCALLTYPE MapColumnToSCID(UINT column,SHCOLUMNID*out) override {if(!out)return E_POINTER;static const PROPERTYKEY keys[]={PKEY_ItemNameDisplay,PKEY_Size,PKEY_ItemTypeText,PKEY_DateModified};if(column>3)return E_INVALIDARG;*out=keys[column];return S_OK;}
};
HRESULT CreateFolder(const Node& n,REFIID iid,void**out) {
    if(!out)return E_POINTER;*out=nullptr;auto p=new(std::nothrow)Folder(n);if(!p)return E_OUTOFMEMORY;auto hr=p->QueryInterface(iid,out);p->Release();return hr;
}
}
