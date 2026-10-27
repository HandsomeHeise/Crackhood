#include "folder.hpp"
#include "diagnostics.hpp"
#include <new>
using namespace crackhood;
class Factory final:public IClassFactory {
    LONG refs_=1;
public:
    Factory(){InterlockedIncrement(&objects);}~Factory(){InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out) override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_IClassFactory)return E_NOINTERFACE;*out=static_cast<IClassFactory*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release() override{auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown*outer,REFIID iid,void**out) override{if(outer)return CLASS_E_NOAGGREGATION;return CreateFolder({},iid,out);}
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override{if(lock)InterlockedIncrement(&objects);else InterlockedDecrement(&objects);return S_OK;}
};
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid,REFIID iid,void**out){trace_shell("DllGetClassObject",iid);if(!out)return E_POINTER;*out=nullptr;if(clsid!=CLSID_Crackhood)return CLASS_E_CLASSNOTAVAILABLE;auto f=new(std::nothrow)Factory;if(!f)return E_OUTOFMEMORY;auto hr=f->QueryInterface(iid,out);f->Release();return hr;}
extern "C" HRESULT __stdcall DllCanUnloadNow(){return objects==0?S_OK:S_FALSE;}
