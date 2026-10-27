#include "test.hpp"
#include "shell/folder.hpp"
#include "shell/pidl.hpp"
#include "shell/appearance.hpp"
#include <wrl/client.h>
using namespace crackhood;
using Microsoft::WRL::ComPtr;
TEST(shell_root_is_browsable_folder){ComPtr<IShellFolder>root;CHECK(SUCCEEDED(CreateFolder({},IID_PPV_ARGS(&root))));SFGAOF flags=SFGAO_FOLDER|SFGAO_HASSUBFOLDER|SFGAO_BROWSABLE;CHECK(SUCCEEDED(root->GetAttributesOf(0,nullptr,&flags)));CHECK((flags&(SFGAO_FOLDER|SFGAO_HASSUBFOLDER|SFGAO_BROWSABLE))==(SFGAO_FOLDER|SFGAO_HASSUBFOLDER|SFGAO_BROWSABLE));}
TEST(shell_root_has_default_open){ComPtr<IShellFolder>root;CHECK(SUCCEEDED(CreateFolder({},IID_PPV_ARGS(&root))));ComPtr<IContextMenu>context;CHECK(SUCCEEDED(root->CreateViewObject(nullptr,IID_PPV_ARGS(&context))));auto menu=CreatePopupMenu();auto hr=context->QueryContextMenu(menu,0,1,100,CMF_DEFAULTONLY);char verb[32]{};auto command=context->GetCommandString(0,GCS_VERBA,nullptr,verb,sizeof(verb));DestroyMenu(menu);CHECK(SUCCEEDED(hr)&&LOWORD(hr)==1);CHECK(SUCCEEDED(command)&&std::string(verb)=="open");}
TEST(shell_parse_drive_before_enumeration){ComPtr<IShellFolder>folder;CHECK(SUCCEEDED(CreateFolder({NodeKind::Console,{"cold-parse"},{},{"Xbox",true}},IID_PPV_ARGS(&folder))));wchar_t name[]=L"HDD";PIDLIST_RELATIVE item=nullptr;auto hr=folder->ParseDisplayName(nullptr,nullptr,name,nullptr,&item,nullptr);if(item)CoTaskMemFree(item);CHECK(SUCCEEDED(hr));}
TEST(shell_enumerator_clone) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ComPtr<IShellFolder> root; CHECK(SUCCEEDED(CreateFolder({},IID_PPV_ARGS(&root))));
    ComPtr<IEnumIDList> e; CHECK(SUCCEEDED(root->EnumObjects(nullptr,SHCONTF_FOLDERS,&e)));
    ComPtr<IEnumIDList> clone; CHECK(SUCCEEDED(e->Clone(&clone)));
    PITEMID_CHILD a=nullptr,b=nullptr;
    CHECK(e->Next(1,&a,nullptr)==S_OK); CHECK(clone->Next(1,&b,nullptr)==S_OK); CHECK(ILIsEqual(a,b));
    CoTaskMemFree(a);CoTaskMemFree(b);
}
TEST(shell_bind_invalid_pidl) {
    ComPtr<IShellFolder> root; CHECK(SUCCEEDED(CreateFolder({},IID_PPV_ARGS(&root))));
    alignas(ITEMIDLIST) unsigned char bad[4]={3,0,255,0};
    ComPtr<IShellFolder> child;
    CHECK(FAILED(root->BindToObject(reinterpret_cast<PCUIDLIST_RELATIVE>(bad),nullptr,IID_PPV_ARGS(&child))));
}
TEST(shell_standard_view) {
    ComPtr<IShellFolder> root; CHECK(SUCCEEDED(CreateFolder({},IID_PPV_ARGS(&root))));
    ComPtr<IPersistFolder> persist;CHECK(SUCCEEDED(root.As(&persist)));
    ITEMIDLIST desktop{}; CHECK(SUCCEEDED(persist->Initialize(&desktop)));
    ComPtr<IShellView> view; auto hr=root->CreateViewObject(nullptr,IID_PPV_ARGS(&view));
    if(FAILED(hr))throw std::runtime_error("Standard view HRESULT="+std::to_string(uint32_t(hr)));
}
static PITEMID_CHILD test_item(const Node&node){auto bytes=std::get<std::vector<std::byte>>(encode_node(node));auto item=static_cast<PITEMID_CHILD>(CoTaskMemAlloc(bytes.size()+4));item->mkid.cb=static_cast<USHORT>(bytes.size()+2);memcpy(item->mkid.abID,bytes.data(),bytes.size());memset(reinterpret_cast<char*>(item)+item->mkid.cb,0,2);return item;}
TEST(shell_drive_labels_keep_parsing_identity){ComPtr<IShellFolder>folder;CHECK(SUCCEEDED(CreateFolder({NodeKind::Console,{"rgh"},{},{"Xbox",true}},IID_PPV_ARGS(&folder))));auto item=test_item({NodeKind::Drive,{"rgh"},{"HDD:\\"},{"HDD",true}});STRRET display{},parsing{};CHECK(SUCCEEDED(folder->GetDisplayNameOf(item,SHGDN_NORMAL,&display)));CHECK(SUCCEEDED(folder->GetDisplayNameOf(item,SHGDN_FORPARSING|SHGDN_INFOLDER,&parsing)));auto label=std::wstring(display.pOleStr),identity=std::wstring(parsing.pOleStr);CoTaskMemFree(display.pOleStr);CoTaskMemFree(parsing.pOleStr);CoTaskMemFree(item);CHECK(label==L"Retail Hard Drive Emulation (HDD:)");CHECK(identity==L"HDD");}
TEST(shell_view_defaults_by_level){for(auto kind:{NodeKind::Root,NodeKind::Console,NodeKind::Drive,NodeKind::Directory}){auto callback=new ViewDefaults(kind);FOLDERVIEWMODE mode=FVM_AUTO;CHECK(SUCCEEDED(callback->MessageSFVCB(SFVM_DEFVIEWMODE,0,reinterpret_cast<LPARAM>(&mode))));CHECK(mode==((kind==NodeKind::Root||kind==NodeKind::Console)?FVM_TILE:FVM_LIST));callback->Release();}}
TEST(shell_add_console_has_no_file_operations){ComPtr<IShellFolder>root;CHECK(SUCCEEDED(CreateFolder({},IID_PPV_ARGS(&root))));auto item=test_item({NodeKind::AddConsole,{},{},{"Add Xbox 360",false}});PCUITEMID_CHILD items[]={item};SFGAOF flags=SFGAO_FOLDER|SFGAO_CANCOPY|SFGAO_CANDELETE|SFGAO_CANRENAME;CHECK(SUCCEEDED(root->GetAttributesOf(1,items,&flags)));CoTaskMemFree(item);CHECK(flags==0);}
TEST(shell_custom_icons_extract_at_small_and_large_sizes){ComPtr<IShellFolder>root;CHECK(SUCCEEDED(CreateFolder({},IID_PPV_ARGS(&root))));for(auto node:{Node{NodeKind::AddConsole,{},{},{"Add Xbox 360",false}},Node{NodeKind::Console,{"icon-test"},{},{"Xbox",true}},Node{NodeKind::Drive,{"rgh"},{"HDD:\\"},{"HDD",true}},Node{NodeKind::Directory,{"rgh"},{"HDD:\\Content"},{"Content",true}},Node{NodeKind::File,{"rgh"},{"HDD:\\default.XEX"},{"default.XEX",false}}}){auto item=test_item(node);PCUITEMID_CHILD items[]={item};ComPtr<IExtractIconW>extract;CHECK(SUCCEEDED(root->GetUIObjectOf(nullptr,1,items,IID_IExtractIconW,nullptr,reinterpret_cast<void**>(extract.GetAddressOf()))));wchar_t path[32768]{};int index=0;UINT flags=0;CHECK(SUCCEEDED(extract->GetIconLocation(0,path,32768,&index,&flags)));HICON large=nullptr,smallIcon=nullptr;auto hr=extract->Extract(path,index,&large,&smallIcon,MAKELONG(48,16));if(large)DestroyIcon(large);if(smallIcon)DestroyIcon(smallIcon);CoTaskMemFree(item);CHECK(SUCCEEDED(hr));CHECK(large&&smallIcon);}}
TEST(shell_sort_modified_time){ComPtr<IShellFolder>folder;CHECK(SUCCEEDED(CreateFolder({},IID_PPV_ARGS(&folder))));auto a=test_item({NodeKind::File,{"rgh"},{"HDD:\\a"},{"a",false,10,100}}),b=test_item({NodeKind::File,{"rgh"},{"HDD:\\b"},{"b",false,20,50}});auto order=static_cast<short>(LOWORD(folder->CompareIDs(3,a,b)));CoTaskMemFree(a);CoTaskMemFree(b);CHECK(order>0);}
TEST(shell_compare_nested_paths){ComPtr<IShellFolder>folder;CHECK(SUCCEEDED(CreateFolder({},IID_PPV_ARGS(&folder))));auto console=test_item({NodeKind::Console,{"rgh"},{},{"Xbox",true}}),hdd=test_item({NodeKind::Drive,{"rgh"},{"HDD:\\"},{"HDD",true}}),game=test_item({NodeKind::Drive,{"rgh"},{"GAME:\\"},{"GAME",true}});auto a=ILCombine(console,hdd),b=ILCombine(console,game);auto order=static_cast<short>(LOWORD(folder->CompareIDs(0,a,b)));CoTaskMemFree(a);CoTaskMemFree(b);CoTaskMemFree(console);CoTaskMemFree(hdd);CoTaskMemFree(game);CHECK(order!=0);}
