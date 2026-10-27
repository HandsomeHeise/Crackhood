#include "data_object.hpp"
#include "staging.hpp"
#include "launch.hpp"
#include "core/binary.hpp"
#include "core/unicode.hpp"
#include "core/path.hpp"
#include "broker/profiles.hpp"
#include <wrl/client.h>
#include <shellapi.h>
#include <fstream>
#include <thread>
#include <algorithm>
namespace crackhood {
using Microsoft::WRL::ComPtr;
class DropTarget final:public IDropTarget {
    LONG refs_=1;Node destination_;bool allowed_=false;
    bool can_accept(IDataObject*data){FORMATETC f{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};if(SUCCEEDED(data->QueryGetData(&f)))return true;f.cfFormat=static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW));return SUCCEEDED(data->QueryGetData(&f));}
    HRESULT upload(const std::filesystem::path&local){auto name=utf8(local.filename().wstring());if(name.empty())return E_INVALIDARG;auto target=destination_.path.value+(destination_.path.value.back()=='\\'?"":"\\")+name;if(std::holds_alternative<Error>(normalize_remote(target)))return E_INVALIDARG;Writer w;w.text(utf8(local.wstring()));return LaunchRequest({1,0,destination_.console,"job_upload",{target},w.bytes});}
    HRESULT upload_many(const std::vector<std::filesystem::path>&paths){if(paths.empty())return E_INVALIDARG;if(paths.size()==1)return upload(paths[0]);for(auto&p:paths)if(p.filename().empty()||std::holds_alternative<Error>(normalize_remote(destination_.path.value+(destination_.path.value.back()=='\\'?"":"\\")+utf8(p.filename().wstring()))))return E_INVALIDARG;Writer w;w.text(utf8(paths[0].wstring()));w.number(0,1);w.number(paths.size(),4);for(size_t i=1;i<paths.size();++i)w.text(utf8(paths[i].wstring()));return LaunchRequest({1,0,destination_.console,"job_upload_many",destination_.path,w.bytes});}
public:
    explicit DropTarget(Node n):destination_(std::move(n)){InterlockedIncrement(&objects);}
    ~DropTarget(){InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_IDropTarget)return E_NOINTERFACE;*out=static_cast<IDropTarget*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release()override{auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*data,DWORD,POINTL,DWORD*effect)override{if(!data||!effect)return E_POINTER;allowed_=can_accept(data);*effect=allowed_?(*effect&DROPEFFECT_COPY):DROPEFFECT_NONE;return S_OK;}
    HRESULT STDMETHODCALLTYPE DragOver(DWORD,POINTL,DWORD*effect)override{if(!effect)return E_POINTER;*effect=allowed_?(*effect&DROPEFFECT_COPY):DROPEFFECT_NONE;return S_OK;}
    HRESULT STDMETHODCALLTYPE DragLeave()override{allowed_=false;return S_OK;}
    HRESULT STDMETHODCALLTYPE Drop(IDataObject*data,DWORD,POINTL,DWORD*effect)override{
        if(!data||!effect)return E_POINTER;*effect=DROPEFFECT_NONE;
        FORMATETC remoteFormat{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"Crackhood.Nodes.v1")),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM remote{};
        if(SUCCEEDED(data->GetData(&remoteFormat,&remote))){
            if(remote.tymed!=TYMED_HGLOBAL||!remote.hGlobal){ReleaseStgMedium(&remote);return DV_E_TYMED;}
            HRESULT result=S_OK;auto size=GlobalSize(remote.hGlobal);auto bytes=static_cast<std::byte*>(GlobalLock(remote.hGlobal));bool move=false;
            try{if(!bytes||size>1048576)throw std::runtime_error("Invalid clipboard data");Reader reader{{bytes,size}};if(reader.number(4)!=1)throw std::runtime_error("Version");move=reader.number(4)==DROPEFFECT_MOVE;auto count=reader.number(4);if(!count||count>65536)throw std::runtime_error("Count");
                struct Item{ConsoleId console;RemotePath path;std::string name;};std::vector<Item>items;for(uint64_t i=0;i<count;++i){Item item{{reader.text()},{reader.text()},reader.text()};if(item.name.empty()||item.name.find_first_of("\\/:")!=std::string::npos||std::holds_alternative<Error>(normalize_remote(item.path.value)))throw std::runtime_error("Path");if(item.console!=destination_.console)move=false;items.push_back(std::move(item));}if(reader.offset!=size)throw std::runtime_error("Data");
                if(move){if(items.size()==1){Writer w;w.text(destination_.path.value+(destination_.path.value.back()=='\\'?"":"\\")+items[0].name);result=LaunchRequest({1,0,items[0].console,"job_move",items[0].path,w.bytes});}else{Writer w;w.text(items[0].path.value);w.number(0,1);w.number(items.size(),4);for(size_t i=1;i<items.size();++i)w.text(items[i].path.value);result=LaunchRequest({1,0,destination_.console,"job_move_many",destination_.path,w.bytes});}}
            }catch(...){result=E_INVALIDARG;}if(bytes)GlobalUnlock(remote.hGlobal);ReleaseStgMedium(&remote);if(FAILED(result))return result;if(move){*effect=DROPEFFECT_COPY;return S_OK;}
        }
        FORMATETC f{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM medium{};
        if(SUCCEEDED(data->GetData(&f,&medium))){if(medium.tymed!=TYMED_HGLOBAL||!medium.hGlobal){ReleaseStgMedium(&medium);return DV_E_TYMED;}auto count=DragQueryFileW(static_cast<HDROP>(medium.hGlobal),0xffffffff,nullptr,0);if(count>65536){ReleaseStgMedium(&medium);return E_INVALIDARG;}std::vector<std::filesystem::path>paths;for(UINT i=0;i<count;++i){auto size=DragQueryFileW(static_cast<HDROP>(medium.hGlobal),i,nullptr,0);std::wstring path(size+1,L'\0');DragQueryFileW(static_cast<HDROP>(medium.hGlobal),i,path.data(),size+1);path.resize(size);paths.emplace_back(path);}ReleaseStgMedium(&medium);auto hr=upload_many(paths);if(SUCCEEDED(hr))*effect=DROPEFFECT_COPY;return hr;}
        ComPtr<IDataObjectAsyncCapability>async;data->QueryInterface(IID_PPV_ARGS(&async));BOOL enabled=FALSE;if(async)async->GetAsyncMode(&enabled);
        ComPtr<IStream> marshalled;auto hr=CoMarshalInterThreadInterfaceInStream(IID_IDataObject,data,&marshalled);if(FAILED(hr))return hr;
        if(async&&enabled){hr=async->StartOperation(nullptr);if(FAILED(hr))return hr;}
        auto stream=marshalled.Detach();auto destination=destination_;InterlockedIncrement(&objects);
        std::thread([stream,destination]{
            CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);ComPtr<IDataObject>source;auto result=CoGetInterfaceAndReleaseStream(stream,IID_PPV_ARGS(&source));
            if(SUCCEEDED(result)){FORMATETC descriptor{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW)),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM m{};result=source->GetData(&descriptor,&m);
                if(SUCCEEDED(result)){if(m.tymed!=TYMED_HGLOBAL||!m.hGlobal){ReleaseStgMedium(&m);m={};result=DV_E_TYMED;}auto size=GlobalSize(m.hGlobal);auto group=static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(m.hGlobal));
                    if(!group||size<offsetof(FILEGROUPDESCRIPTORW,fgd)||group->cItems>65536||group->cItems>(size - offsetof(FILEGROUPDESCRIPTORW,fgd))/sizeof(FILEDESCRIPTORW))result=E_INVALIDARG;
                    else try{GUID guid{};CoCreateGuid(&guid);wchar_t id[64]{};StringFromGUID2(guid,id,64);auto root=user_root()/L"Staging"/id;std::filesystem::create_directories(root);std::vector<std::filesystem::path>tops;
                        for(UINT i=0;i<group->cItems;++i){auto&d=group->fgd[i];if(wcsnlen_s(d.cFileName,MAX_PATH)>=MAX_PATH){result=E_INVALIDARG;break;}auto relative=std::filesystem::path(d.cFileName);bool invalid=relative.empty()||relative.is_absolute()||relative.has_root_name();for(auto&component:relative)if(component==L".."||component==L".")invalid=true;if(invalid){result=E_INVALIDARG;break;}
                            for(auto&component:relative)if(!valid_local_component(component.wstring()))invalid=true;if(invalid){result=E_INVALIDARG;break;}
                            auto local=root/relative;auto top=root/(*relative.begin());if(std::find(tops.begin(),tops.end(),top)==tops.end())tops.push_back(top);
                            if((d.dwFlags&FD_ATTRIBUTES)&&(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)){std::filesystem::create_directories(local);continue;}std::filesystem::create_directories(local.parent_path());
                            FORMATETC contents{static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILECONTENTS)),nullptr,DVASPECT_CONTENT,static_cast<LONG>(i),TYMED_ISTREAM};STGMEDIUM content{};result=source->GetData(&contents,&content);if(FAILED(result))break;if(content.tymed!=TYMED_ISTREAM||!content.pstm){ReleaseStgMedium(&content);result=DV_E_TYMED;break;}
                            std::optional<uint64_t>expected;if(d.dwFlags&FD_FILESIZE)expected=(uint64_t(d.nFileSizeHigh)<<32)|d.nFileSizeLow;result=stage_virtual_file(content.pstm,local,expected);ReleaseStgMedium(&content);if(FAILED(result))break;
                        }
                        if(SUCCEEDED(result)){DropTarget target(destination);result=target.upload_many(tops);}
                    }catch(...){result=E_FAIL;}
                    if(group)GlobalUnlock(m.hGlobal);ReleaseStgMedium(&m);
                }
                ComPtr<IDataObjectAsyncCapability>capability;source.As(&capability);if(capability)capability->EndOperation(result,nullptr,SUCCEEDED(result)?DROPEFFECT_COPY:DROPEFFECT_NONE);
            }
            if(FAILED(result))MessageBoxW(nullptr,L"Could not prepare all pasted files. No upload was started. The local staging copy was kept for inspection.",L"Crackhood – Paste failed",MB_OK|MB_ICONERROR);
            source.Reset();CoUninitialize();InterlockedDecrement(&objects);
        }).detach();*effect=DROPEFFECT_COPY;return S_OK;
    }
};
HRESULT CreateDropTarget(Node destination,REFIID iid,void**out){if(!out)return E_POINTER;*out=nullptr;if((destination.kind!=NodeKind::Drive&&destination.kind!=NodeKind::Directory)||std::holds_alternative<Error>(normalize_remote(destination.path.value)))return E_INVALIDARG;auto p=new(std::nothrow)DropTarget(std::move(destination));if(!p)return E_OUTOFMEMORY;auto hr=p->QueryInterface(iid,out);p->Release();return hr;}
}
