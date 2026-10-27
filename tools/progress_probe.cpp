#include <windows.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <iostream>
int main(){auto init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Microsoft::WRL::ComPtr<IProgressDialog>dialog;auto hr=CoCreateInstance(CLSID_ProgressDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));std::cout<<"Create: 0x"<<std::hex<<unsigned(hr)<<'\n';if(FAILED(hr))return 1;for(int i=0;i<3;++i){dialog->SetTitle(L"Windows progress lifecycle diagnostic");dialog->SetLine(1,L"HDD:\\diagnostic",TRUE,nullptr);dialog->SetCancelMsg(L"Cancelling …",nullptr);hr=dialog->StartProgressDialog(nullptr,nullptr,PROGDLG_AUTOTIME,nullptr);std::cout<<"Start "<<i<<": 0x"<<std::hex<<unsigned(hr)<<'\n';if(SUCCEEDED(hr)){dialog->SetProgress64(0,1);Sleep(100);}auto stop=dialog->StopProgressDialog();std::cout<<"Stop "<<i<<": 0x"<<std::hex<<unsigned(stop)<<'\n';}dialog.Reset();if(SUCCEEDED(init))CoUninitialize();return 0;}
