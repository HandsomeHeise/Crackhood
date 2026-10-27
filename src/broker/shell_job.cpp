#include "shell_job.hpp"
#include "ipc/client.hpp"
#include "ipc/win_pipe.hpp"
#include "broker/profiles.hpp"
#include "core/binary.hpp"
#include "core/unicode.hpp"
#include <shlobj.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <future>
#include <atomic>
#include <fstream>
namespace crackhood {
static bool can_retry(const std::string&op){return op=="job_upload"||op=="job_download"||op=="job_move"||op=="job_upload_many"||op=="job_move_many";}
Response run_shell_job(Request q,ShellJobFeedback&ui,ShellExecutor execute,std::function<void(uint64_t)> cancel){
    for(;;){
        std::atomic<uint64_t>done=0,total=0;ui.begin(q);Response result;
        try{
            auto pending=std::async(std::launch::async,[&]{return execute(q,[&](uint64_t d,uint64_t t){total=t;done=d;});});
            bool cancelled=false;
            while(pending.wait_for(std::chrono::milliseconds(50))!=std::future_status::ready){
                MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                ui.progress(done,total);if(!cancelled&&ui.cancelled()){cancelled=true;cancel(q.id);}
            }
            result=pending.get();ui.progress(done,total);
        }catch(const std::exception&e){result={q.id,Error{ErrorCode::Protocol,e.what()}};}
        ui.end();auto error=std::get_if<Error>(&result.result);if(!error)return result;
        if(error->code==ErrorCode::Conflict&&can_retry(q.operation)){
            auto choice=ui.conflict(*error);if(choice==RetryChoice::Cancel)return {q.id,Error{ErrorCode::Cancelled,"Job cancelled"}};
            try{Reader r{q.payload};Writer w;w.text(r.text());if(r.offset<r.bytes.size())r.number(1);w.number(choice==RetryChoice::Replace?1:2,1);w.bytes.insert(w.bytes.end(),r.bytes.begin()+r.offset,r.bytes.end());q.payload=std::move(w.bytes);continue;}catch(const std::exception&e){result={q.id,Error{ErrorCode::Protocol,e.what()}};}
        }
        if(std::get<Error>(result.result).code!=ErrorCode::Cancelled)ui.failure(std::get<Error>(result.result));return result;
    }
}
static const wchar_t* heading(const std::string&op){if(op.starts_with("job_delete"))return L"Deleting Xbox files";if(op.starts_with("job_move"))return L"Moving Xbox files";if(op.starts_with("job_upload"))return L"Copying files to Xbox";if(op.starts_with("job_download"))return L"Copying files from Xbox";if(op=="screenshot")return L"Capturing Xbox screenshot";if(op.starts_with("reboot"))return L"Restarting Xbox";if(op=="launch_xex")return L"Launching Xbox title";if(op=="sync_time")return L"Synchronizing Xbox clock";return L"Connecting to Xbox";}
class NativeFeedback final:public ShellJobFeedback {
    Microsoft::WRL::ComPtr<IProgressDialog>dialog_;bool active_=false,deleting_=false;
public:
    NativeFeedback(){auto hr=CoCreateInstance(CLSID_ProgressDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog_));if(FAILED(hr))throw std::runtime_error("Windows progress dialog is unavailable");}
    ~NativeFeedback(){end();}
    void begin(const Request&q)override{
        if(!dialog_){auto hr=CoCreateInstance(CLSID_ProgressDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog_));if(FAILED(hr))throw std::runtime_error("Windows progress dialog is unavailable");}
        deleting_=q.operation.starts_with("job_delete");
        dialog_->SetTitle(heading(q.operation));dialog_->SetLine(1,utf16(q.path.value).c_str(),TRUE,nullptr);
        dialog_->SetCancelMsg(L"Cancelling. Waiting for the Xbox operation to stop safely …",nullptr);
        auto hr=dialog_->StartProgressDialog(nullptr,nullptr,PROGDLG_AUTOTIME,nullptr);if(FAILED(hr))throw std::runtime_error("Could not start Windows progress dialog");active_=true;dialog_->SetProgress64(0,1);
    }
    void progress(uint64_t done,uint64_t total)override{dialog_->SetProgress64(total?std::min(done,total):0,total?total:1);if(total){auto label=std::to_wstring(done/1024)+L" / "+std::to_wstring(total/1024)+L" KiB";dialog_->SetLine(2,label.c_str(),FALSE,nullptr);}else if(deleting_){auto label=std::to_wstring(done)+L" items deleted";dialog_->SetLine(2,label.c_str(),FALSE,nullptr);}}
    bool cancelled()override{return dialog_->HasUserCancelled()!=FALSE;}
    void end()override{if(active_){dialog_->StopProgressDialog();active_=false;}dialog_.Reset();}
    RetryChoice conflict(const Error&)override{auto choice=MessageBoxW(nullptr,L"The destination already exists. Overwrite?\nYes: Replace · No: Skip · Cancel: Stop the job",L"File conflict",MB_YESNOCANCEL|MB_ICONQUESTION);return choice==IDYES?RetryChoice::Replace:choice==IDNO?RetryChoice::Skip:RetryChoice::Cancel;}
    void failure(const Error&e)override{MessageBoxW(nullptr,utf16(e.message).c_str(),L"Xbox operation failed",MB_OK|MB_ICONERROR);}
};
std::unique_ptr<ShellJobFeedback> make_native_job_feedback(){return std::make_unique<NativeFeedback>();}
int run_native_shell_job(const wchar_t*requestFile){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    struct ComLifetime{HRESULT hr;~ComLifetime(){if(SUCCEEDED(hr))CoUninitialize();}}lifetime{initialized};
    try{
        auto path=std::filesystem::path(requestFile);auto size=std::filesystem::file_size(path);if(size>1048576)throw std::runtime_error("Job is too large");
        std::vector<std::byte>bytes(static_cast<size_t>(size));std::ifstream input(path,std::ios::binary);input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());if(!input)throw std::runtime_error("Could not read job");input.close();
        auto decoded=decode_request(bytes);auto q=std::get_if<Request>(&decoded);if(!q)throw std::runtime_error(std::get<Error>(decoded).message);
        std::error_code ec;std::filesystem::remove(path,ec);
        auto cancelName=L"Local\\Crackhood.Cancel."+user_sid()+L"."+std::to_wstring(q->id);Handle cancellation(CreateEventW(nullptr,TRUE,FALSE,cancelName.c_str()));if(!cancellation.value)throw std::runtime_error("Could not create cancellation event");
        NativeFeedback ui;auto result=run_shell_job(*q,ui,[](Request request,auto progress){return Client{}.call(request,std::chrono::milliseconds(3600000),progress);},[&](uint64_t){SetEvent(cancellation);});
        if(std::holds_alternative<Error>(result.result))return 1;
        if(q->operation=="job_download_open"){
            Reader r{q->payload};auto local=utf16(r.text());auto folder=std::filesystem::path(local).parent_path().wstring();SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;info.lpVerb=L"open";info.lpFile=local.c_str();info.lpDirectory=folder.c_str();info.nShow=SW_SHOWNORMAL;
            if(!ShellExecuteExW(&info)){ui.failure({ErrorCode::Unsupported,"File downloaded; could not open it. Please choose a suitable application."});return 1;}
        }else if(q->operation=="check_connection")MessageBoxW(nullptr,L"Xbox is reachable.",L"Xbox connection",MB_OK|MB_ICONINFORMATION);
        else if(q->operation=="security_status")MessageBoxW(nullptr,L"SDK access security is enabled.",L"Xbox security",MB_OK|MB_ICONINFORMATION);
        else if(q->operation=="screenshot"){auto status=L"Screenshot saved: "+utf16(q->path.value);MessageBoxW(nullptr,status.c_str(),L"Xbox screenshot",MB_OK|MB_ICONINFORMATION);}
        return 0;
    }catch(const std::exception&e){MessageBoxW(nullptr,utf16(e.what()).c_str(),L"Xbox operation failed",MB_OK|MB_ICONERROR);return 1;}
}
}
