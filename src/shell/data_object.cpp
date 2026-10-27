#include "data_object.hpp"
#include "pidl.hpp"
#include "ipc/client.hpp"
#include "core/binary.hpp"
#include "core/unicode.hpp"
#include <shlwapi.h>
#include <wrl/client.h>
#include <mutex>
#include <memory>
#include <filesystem>
namespace crackhood {
using Microsoft::WRL::ComPtr;
static CLIPFORMAT format(const wchar_t*name){return static_cast<CLIPFORMAT>(RegisterClipboardFormatW(name));}
static HRESULT global_data(std::span<const std::byte>bytes,STGMEDIUM*out){
    out->tymed=TYMED_HGLOBAL;out->pUnkForRelease=nullptr;out->hGlobal=GlobalAlloc(GMEM_MOVEABLE,bytes.size());if(!out->hGlobal)return E_OUTOFMEMORY;auto p=GlobalLock(out->hGlobal);if(!p){GlobalFree(out->hGlobal);return E_OUTOFMEMORY;}memcpy(p,bytes.data(),bytes.size());GlobalUnlock(out->hGlobal);return S_OK;
}
struct DownloadBacking {
    Node node;std::filesystem::path path;std::mutex mutex;bool ready=false;
    explicit DownloadBacking(Node n):node(std::move(n)){}
    ~DownloadBacking(){if(!path.empty()){std::error_code ec;std::filesystem::remove(path,ec);}}
    HRESULT ensure(){
        std::lock_guard lock(mutex);if(ready)return S_OK;
        wchar_t local[32768]{};GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);auto root=std::filesystem::path(local)/L"Crackhood"/L"Temp";
        GUID guid{};CoCreateGuid(&guid);wchar_t id[64]{};StringFromGUID2(guid,id,64);
        std::filesystem::create_directories(root);path=root/(std::wstring(id)+L".download");
        Writer w;w.text(utf8(path.wstring()));Request q{1,GetTickCount64(),node.console,"download",node.path,w.bytes};
        auto response=Client{}.call(q,std::chrono::milliseconds(3600000));if(std::holds_alternative<Error>(response.result))return HRESULT_FROM_WIN32(ERROR_READ_FAULT);
        ready=true;return S_OK;
    }
};
class DeferredStream final:public IStream {
    LONG refs_=1;std::shared_ptr<DownloadBacking> backing_;ComPtr<IStream> file_;uint64_t position_=0;
    HRESULT ensure(){try{if(file_)return S_OK;auto hr=backing_->ensure();if(FAILED(hr))return hr;hr=SHCreateStreamOnFileEx(backing_->path.c_str(),STGM_READ|STGM_SHARE_DENY_NONE,FILE_ATTRIBUTE_NORMAL,FALSE,nullptr,&file_);if(FAILED(hr))return hr;LARGE_INTEGER move{};move.QuadPart=position_;return file_->Seek(move,STREAM_SEEK_SET,nullptr);}catch(...){return E_FAIL;}}
public:
    explicit DeferredStream(std::shared_ptr<DownloadBacking> b,uint64_t pos=0):backing_(std::move(b)),position_(pos){InterlockedIncrement(&objects);}
    ~DeferredStream(){file_.Reset();InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_ISequentialStream&&iid!=IID_IStream)return E_NOINTERFACE;*out=static_cast<IStream*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release()override{auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE Read(void*buffer,ULONG n,ULONG*read)override{if(!buffer&&n)return STG_E_INVALIDPOINTER;auto hr=ensure();if(FAILED(hr))return hr;ULONG got=0;hr=file_->Read(buffer,n,&got);position_+=got;if(read)*read=got;return hr;}
    HRESULT STDMETHODCALLTYPE Write(const void*,ULONG,ULONG*)override{return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move,DWORD origin,ULARGE_INTEGER*out)override{
        uint64_t base=origin==STREAM_SEEK_SET?0:(origin==STREAM_SEEK_CUR?position_:backing_->node.entry.size);if(origin>STREAM_SEEK_END)return STG_E_INVALIDFUNCTION;
        if(move.QuadPart<0&&uint64_t(-(move.QuadPart+1))+1>base)return STG_E_INVALIDFUNCTION;
        if(move.QuadPart>0&&uint64_t(move.QuadPart)>UINT64_MAX-base)return STG_E_INVALIDFUNCTION;
        position_=move.QuadPart<0?base-(uint64_t(-(move.QuadPart+1))+1):base+move.QuadPart;
        if(file_){LARGE_INTEGER p{};p.QuadPart=static_cast<LONGLONG>(position_);auto hr=file_->Seek(p,STREAM_SEEK_SET,nullptr);if(FAILED(hr))return hr;}if(out)out->QuadPart=position_;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER)override{return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*target,ULARGE_INTEGER requested,ULARGE_INTEGER*read,ULARGE_INTEGER*written)override{
        if(!target)return E_POINTER;auto hr=ensure();if(FAILED(hr))return hr;ULARGE_INTEGER got{},sent{};hr=file_->CopyTo(target,requested,&got,&sent);position_+=got.QuadPart;if(read)*read=got;if(written)*written=sent;return hr;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE Revert()override{return STG_E_REVERTED;}
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE Stat(STATSTG*out,DWORD flags)override{if(!out)return E_POINTER;*out={};out->type=STGTY_STREAM;out->cbSize.QuadPart=backing_->node.entry.size;out->grfMode=STGM_READ;if(!(flags&STATFLAG_NONAME)){auto name=utf16(backing_->node.entry.name);out->pwcsName=static_cast<wchar_t*>(CoTaskMemAlloc((name.size()+1)*2));if(!out->pwcsName)return E_OUTOFMEMORY;memcpy(out->pwcsName,name.c_str(),(name.size()+1)*2);}return S_OK;}
    HRESULT STDMETHODCALLTYPE Clone(IStream**out)override{if(!out)return E_POINTER;*out=new(std::nothrow)DeferredStream(backing_,position_);return *out?S_OK:E_OUTOFMEMORY;}
};
class DataObject final:public IDataObject,public IDataObjectAsyncCapability {
    LONG refs_=1;std::vector<Node> nodes_,roots_;DWORD effect_;std::vector<std::wstring> names_;bool expanded_=false;BOOL async_=TRUE,inOperation_=FALSE;
    HRESULT expand(){
        if(expanded_)return S_OK;
        try{
            std::vector<Node> flat;std::vector<std::wstring> names;
            std::function<HRESULT(Node,std::wstring,unsigned)> add=[&](Node node,std::wstring relative,unsigned depth)->HRESULT{
                if(depth>64||flat.size()>=65536||relative.size()>=MAX_PATH)return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
                flat.push_back(node);names.push_back(relative);
                if(node.entry.directory){Request q{1,GetTickCount64(),node.console,"list",node.path,{}};auto reply=Client{}.call(q,std::chrono::milliseconds(4000));auto b=std::get_if<std::vector<std::byte>>(&reply.result);if(!b)return HRESULT_FROM_WIN32(ERROR_READ_FAULT);auto result=decode_entries(*b);auto list=std::get_if<std::vector<Entry>>(&result);if(!list)return E_FAIL;
                    for(auto&e:*list){if(e.name=="."||e.name==".."||e.name.find_first_of("\\/:")!=std::string::npos)return E_INVALIDARG;auto path=node.path.value+(node.path.value.back()=='\\'?"":"\\")+e.name;auto hr=add({e.directory?NodeKind::Directory:NodeKind::File,node.console,{path},e},relative+L"\\"+utf16(e.name),depth+1);if(FAILED(hr))return hr;}}
                return S_OK;
            };
            for(auto&n:nodes_){auto hr=add(n,utf16(n.entry.name),0);if(FAILED(hr))return hr;}
            nodes_=std::move(flat);names_=std::move(names);expanded_=true;return S_OK;
        }catch(...){return E_FAIL;}
    }
public:
    explicit DataObject(std::vector<Node> nodes,DWORD effect):nodes_(nodes),roots_(std::move(nodes)),effect_(effect){InterlockedIncrement(&objects);}
    ~DataObject(){InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(iid==IID_IUnknown||iid==IID_IDataObject)*out=static_cast<IDataObject*>(this);else if(iid==IID_IDataObjectAsyncCapability)*out=static_cast<IDataObjectAsyncCapability*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release()override{auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC*f,STGMEDIUM*out)override{
        if(!f||!out)return E_POINTER;*out={};auto hr=QueryGetData(f);if(FAILED(hr))return hr;
        if(f->cfFormat==format(CFSTR_PREFERREDDROPEFFECT)){DWORD externalEffect=DROPEFFECT_COPY;return global_data({reinterpret_cast<std::byte*>(&externalEffect),4},out);}
        if(f->cfFormat==format(L"Crackhood.Nodes.v1")){Writer w;w.number(1,4);w.number(effect_,4);w.number(roots_.size(),4);for(auto&n:roots_){w.text(n.console.value);w.text(n.path.value);w.text(n.entry.name);}return global_data(w.bytes,out);}
        if(f->cfFormat==format(CFSTR_FILECONTENTS)){
            if(f->lindex<0)return DV_E_LINDEX;
            hr=expand();if(FAILED(hr))return hr;if(static_cast<size_t>(f->lindex)>=nodes_.size()||nodes_[f->lindex].entry.directory)return DV_E_LINDEX;
            out->tymed=TYMED_ISTREAM;out->pstm=new(std::nothrow)DeferredStream(std::make_shared<DownloadBacking>(nodes_[f->lindex]));return out->pstm?S_OK:E_OUTOFMEMORY;
        }
        hr=expand();if(FAILED(hr))return hr;
        auto bytes=offsetof(FILEGROUPDESCRIPTORW,fgd)+sizeof(FILEDESCRIPTORW)*nodes_.size();std::vector<std::byte>buffer(bytes);auto group=reinterpret_cast<FILEGROUPDESCRIPTORW*>(buffer.data());group->cItems=static_cast<UINT>(nodes_.size());
        for(size_t i=0;i<nodes_.size();++i){auto&d=group->fgd[i];auto&e=nodes_[i].entry;d.dwFlags=FD_ATTRIBUTES|FD_FILESIZE|FD_PROGRESSUI;d.dwFileAttributes=e.directory?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL;d.nFileSizeLow=DWORD(e.size);d.nFileSizeHigh=DWORD(e.size>>32);if(e.modified_filetime){d.dwFlags|=FD_WRITESTIME;d.ftLastWriteTime={DWORD(e.modified_filetime),DWORD(e.modified_filetime>>32)};}wcscpy_s(d.cFileName,names_[i].c_str());}
        return global_data(buffer,out);
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*,STGMEDIUM*)override{return DATA_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC*f)override{if(!f)return E_POINTER;if(f->dwAspect!=DVASPECT_CONTENT)return DV_E_DVASPECT;if(f->cfFormat==format(CFSTR_FILECONTENTS))return f->tymed&TYMED_ISTREAM?S_OK:DV_E_TYMED;if(f->cfFormat==format(CFSTR_FILEDESCRIPTORW)||f->cfFormat==format(CFSTR_PREFERREDDROPEFFECT)||f->cfFormat==format(L"Crackhood.Nodes.v1"))return f->tymed&TYMED_HGLOBAL?S_OK:DV_E_TYMED;return DV_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,FORMATETC*out)override{if(out)out->ptd=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*f,STGMEDIUM*medium,BOOL release)override{if(!f||!medium)return E_POINTER;if(f->cfFormat!=format(CFSTR_PERFORMEDDROPEFFECT)&&f->cfFormat!=format(CFSTR_PASTESUCCEEDED))return DV_E_FORMATETC;if(release)ReleaseStgMedium(medium);return S_OK;}
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,IEnumFORMATETC**out)override{if(direction!=DATADIR_GET)return E_NOTIMPL;FORMATETC f[]={{format(CFSTR_FILEDESCRIPTORW),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL},{format(CFSTR_FILECONTENTS),nullptr,DVASPECT_CONTENT,0,TYMED_ISTREAM},{format(CFSTR_PREFERREDDROPEFFECT),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL}};return SHCreateStdEnumFmtEtc(3,f,out);}
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*,DWORD,IAdviseSink*,DWORD*)override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD)override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**)override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL value)override{async_=value;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL*out)override{if(!out)return E_POINTER;*out=async_;return S_OK;}
    HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx*)override{inOperation_=TRUE;return S_OK;}
    HRESULT STDMETHODCALLTYPE InOperation(BOOL*out)override{if(!out)return E_POINTER;*out=inOperation_;return S_OK;}
    HRESULT STDMETHODCALLTYPE EndOperation(HRESULT,IBindCtx*,DWORD)override{inOperation_=FALSE;return S_OK;}
};
HRESULT CreateDataObject(std::vector<Node>nodes,REFIID iid,void**out,DWORD effect){if(!out)return E_POINTER;*out=nullptr;auto p=new(std::nothrow)DataObject(std::move(nodes),effect);if(!p)return E_OUTOFMEMORY;auto hr=p->QueryInterface(iid,out);p->Release();return hr;}
}
