#include "test.hpp"
#include "shell/data_object.hpp"
#include "shell/pidl.hpp"
#include <wrl/client.h>
using namespace crackhood;
using Microsoft::WRL::ComPtr;
class Browser final:public IShellBrowser {
    LONG refs_=1;
public:
    PIDLIST_ABSOLUTE opened=nullptr;UINT flags=0;
    ~Browser(){CoTaskMemFree(opened);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_IOleWindow&&iid!=IID_IShellBrowser)return E_NOINTERFACE;*out=static_cast<IShellBrowser*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release()override{auto r=InterlockedDecrement(&refs_);if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE GetWindow(HWND*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE InsertMenusSB(HMENU,LPOLEMENUGROUPWIDTHS)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetMenuSB(HMENU,HOLEMENU,HWND)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RemoveMenusSB(HMENU)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetStatusTextSB(LPCWSTR)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnableModelessSB(BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE TranslateAcceleratorSB(MSG*,WORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE BrowseObject(PCUIDLIST_RELATIVE path,UINT options)override{opened=ILCloneFull(path);flags=options;return opened?S_OK:E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE GetViewStateStream(DWORD,IStream**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetControlWindow(UINT,HWND*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SendControlMsg(UINT,UINT,WPARAM,LPARAM,LRESULT*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryActiveShellView(IShellView**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnViewWindowActive(IShellView*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetToolbarItems(LPTBBUTTONSB,UINT,UINT)override{return E_NOTIMPL;}
};
TEST(context_folder_open_uses_current_browser){
    ITEMIDLIST desktop{};Node console{NodeKind::Console,{"rgh"},{},{"Xbox",true}};ComPtr<IContextMenu>menu;CHECK(SUCCEEDED(CreateContextMenu({}, {console},&desktop,IID_PPV_ARGS(&menu))));ComPtr<IObjectWithSite>site;CHECK(SUCCEEDED(menu.As(&site)));ComPtr<Browser>browser;browser.Attach(new Browser);CHECK(SUCCEEDED(site->SetSite(browser.Get())));CMINVOKECOMMANDINFO command{sizeof(command)};command.lpVerb="open";CHECK(SUCCEEDED(menu->InvokeCommand(&command)));CHECK(browser->opened);CHECK(browser->flags&SBSP_SAMEBROWSER);auto&item=browser->opened->mkid;auto node=decode_node({reinterpret_cast<const std::byte*>(item.abID),size_t(item.cb-2)});CHECK(std::holds_alternative<Node>(node));CHECK(std::get<Node>(node).console.value=="rgh");
}
