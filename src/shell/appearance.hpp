#pragma once
#include "folder.hpp"
#include "core/unicode.hpp"
namespace crackhood {
inline std::wstring display_label(const Node& n) {
    auto name=utf16(n.entry.name);
    if(n.kind==NodeKind::Console){auto separator=name.rfind(L" · ");if(separator!=std::wstring::npos)name.resize(separator);return name;}
    if(n.kind==NodeKind::Drive) {
        if(_wcsicmp(name.c_str(),L"HDD")==0)return L"Retail Hard Drive Emulation (HDD:)";
        if(_wcsicmp(name.c_str(),L"GAME")==0||_wcsicmp(name.c_str(),L"D")==0)return L"Active Title Media ("+name+L":)";
        return L"Volume ("+name+L":)";
    }
    return name;
}
inline const wchar_t* type_label(const Node&n){if(n.kind==NodeKind::AddConsole)return L"Action";if(n.kind==NodeKind::Console)return L"Xbox 360";if(n.kind==NodeKind::Drive)return L"Xbox drive";return n.entry.directory?L"Folder":L"File";}
inline int icon_resource(const Node& n) {
    if(n.kind==NodeKind::AddConsole)return 102;
    if(n.kind==NodeKind::Console)return 103;
    if(n.kind==NodeKind::Drive)return 105;
    if(n.kind==NodeKind::Directory)return 106;
    auto name=utf16(n.entry.name);
    if(n.kind==NodeKind::File&&name.size()>4&&_wcsicmp(name.c_str()+name.size()-4,L".xex")==0)return 107;
    return 0;
}
class ViewDefaults final:public IShellFolderViewCB {
    LONG refs_=1; NodeKind kind_;
public:
    explicit ViewDefaults(NodeKind kind):kind_(kind){InterlockedIncrement(&objects);}
    ~ViewDefaults(){InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_IShellFolderViewCB)return E_NOINTERFACE;*out=static_cast<IShellFolderViewCB*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release()override{auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE MessageSFVCB(UINT msg,WPARAM,LPARAM value)override {
        if(msg!=SFVM_DEFVIEWMODE)return E_NOTIMPL;
        if(!value)return E_POINTER;
        *reinterpret_cast<FOLDERVIEWMODE*>(value)=(kind_==NodeKind::Root||kind_==NodeKind::Console)?FVM_TILE:FVM_LIST;
        return S_OK;
    }
};
}
