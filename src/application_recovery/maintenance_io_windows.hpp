// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "windows_file_io.hpp"
#include "native_random.hpp"
#include <io.h>
#include <csignal>
#include <mutex>
namespace wsprrypi::maintenance_windows {
namespace wf=windows_file;
inline HANDLE descriptor_handle(int descriptor){
#ifdef _MSC_VER
    // A malformed command-line FD must refuse rather than invoke the CRT's
    // default process-termination handler. The change is thread-local.
    const auto old=::_set_thread_local_invalid_parameter_handler([](const wchar_t *,const wchar_t *,const wchar_t *,unsigned,uintptr_t){});
    struct Restore { _invalid_parameter_handler old;~Restore(){::_set_thread_local_invalid_parameter_handler(old);} } restore{old};
#endif
    const auto raw=::_get_osfhandle(descriptor);wf::require(raw!=-1&&raw!=-2);
    return reinterpret_cast<HANDLE>(raw);
}
inline std::string phrase_fd(int descriptor){
    const auto handle=descriptor_handle(descriptor);const auto owner=wf::current_sid();
    const auto type=::GetFileType(handle);wf::Snapshot before{};
    if(type==FILE_TYPE_DISK)before=wf::inspect(handle,false);
    else{DWORD flags{};wf::require(type==FILE_TYPE_PIPE&&::GetNamedPipeInfo(handle,&flags,nullptr,nullptr,nullptr)&&!(flags&PIPE_TYPE_MESSAGE));}
    wf::verify_exact_owner(handle,owner);wf::verify_security(handle,owner,true,false);
    std::string result;char byte{};
    for(;;){DWORD count{};if(!::ReadFile(handle,&byte,1,&count,nullptr))wf::require(type==FILE_TYPE_PIPE&&::GetLastError()==ERROR_BROKEN_PIPE&&!count);if(!count||byte=='\n')break;
        wf::require(byte!='\0'&&result.size()<1024);result+=byte;}
    wf::require(!result.empty());
    if(type==FILE_TYPE_DISK)wf::require(wf::same_snapshot(before,wf::inspect(handle,false)));
    wf::verify_exact_owner(handle,owner);wf::verify_security(handle,owner,true,false);return result;
}
struct ConsoleState {std::mutex mutex;HANDLE input{},stop{};DWORD mode{};bool active{};};
inline ConsoleState console_state;
inline std::mutex console_gate;
inline BOOL WINAPI console_control(DWORD control){
    std::lock_guard lock(console_state.mutex);if(!console_state.active)return FALSE;
    (void)::SetConsoleMode(console_state.input,console_state.mode);
    if(control==CTRL_C_EVENT||control==CTRL_BREAK_EVENT){::SetEvent(console_state.stop);return TRUE;}
    return FALSE;
}
inline std::string phrase_console(volatile std::sig_atomic_t &interrupted){
    std::lock_guard gate(console_gate);
    wf::Handle input(::CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
    wf::Handle stop(::CreateEventW(nullptr,TRUE,FALSE,nullptr));DWORD original{};
    wf::require(input.get()!=INVALID_HANDLE_VALUE&&stop.get()!=nullptr&&::GetConsoleMode(input.get(),&original));
    {
        std::lock_guard lock(console_state.mutex);wf::require(!console_state.active);
        console_state.input=input.get();console_state.stop=stop.get();console_state.mode=original;console_state.active=true;
        if(!::SetConsoleCtrlHandler(console_control,TRUE)){console_state.active=false;wf::require(false);}
    }
    struct Restore {
        ~Restore(){
            {std::lock_guard lock(console_state.mutex);(void)::SetConsoleMode(console_state.input,console_state.mode);console_state.active=false;}
            (void)::SetConsoleCtrlHandler(console_control,FALSE);
        }
    } restore;
    wf::require(::SetConsoleMode(input.get(),original&~(ENABLE_ECHO_INPUT|ENABLE_LINE_INPUT|ENABLE_PROCESSED_INPUT)));
    std::wstring text;const HANDLE waits[]{stop.get(),input.get()};
    for(;;){
        const auto ready=::WaitForMultipleObjects(2,waits,FALSE,INFINITE);
        if(ready==WAIT_OBJECT_0){interrupted=SIGINT;wf::require(false);}
        wf::require(ready==WAIT_OBJECT_0+1);INPUT_RECORD record{};DWORD count{};
        wf::require(::ReadConsoleInputW(input.get(),&record,1,&count)&&count==1);
        if(record.EventType!=KEY_EVENT||!record.Event.KeyEvent.bKeyDown)continue;
        const auto key=record.Event.KeyEvent;const auto character=key.uChar.UnicodeChar;
        if(character==3||character==27){interrupted=SIGINT;wf::require(false);}
        if(character==L'\r'||character==L'\n')break;
        if(character==L'\b'){
            if(!text.empty()){const auto tail=text.back();text.pop_back();if(tail>=0xdc00&&tail<=0xdfff&&!text.empty()&&text.back()>=0xd800&&text.back()<=0xdbff)text.pop_back();}
            continue;
        }
        if(!character)continue;
        wf::require(character>=32&&character!=127&&key.wRepeatCount>0&&key.wRepeatCount<=1024-text.size());
        text.append(key.wRepeatCount,character);
    }
    wf::require(!text.empty());const auto length=::WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    wf::require(length>0&&length<=1024);std::string result(static_cast<std::size_t>(length),'\0');
    wf::require(::WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),length,nullptr,nullptr)==length);return result;
}
// An explicit encrypted export only. Active recovery publication continues to
// use the common journal/generation engine and its independent owner locks.
class Export {
public:
    explicit Export(const std::string &requested):destination_(wf::path_from_utf8(requested)),parents_(destination_,wf::current_sid()),directory_(recovery_windows::private_directory(destination_.parent_path())){
        wf::flush_directory(destination_.parent_path(),wf::current_sid());
        std::array<std::uint8_t,16> random{};native_random(random);std::wstring name=L".wsprrypi-backup-";
        for(const auto byte:random){name+=L"0123456789abcdef"[byte>>4];name+=L"0123456789abcdef"[byte&15];}name+=L".writing";
        const auto path=destination_.parent_path()/name;wf::PrivateSecurity security(wf::current_sid());
        file_=wf::Handle(::CreateFileW(wf::api_path(path).c_str(),GENERIC_READ|GENERIC_WRITE|DELETE|READ_CONTROL,FILE_SHARE_READ,
            security.attributes(),CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));wf::require(file_.get()!=INVALID_HANDLE_VALUE);
    }
    ~Export(){if(file_.get()!=INVALID_HANDLE_VALUE&&!published_){FILE_DISPOSITION_INFO remove{TRUE};(void)::SetFileInformationByHandle(file_.get(),FileDispositionInfo,&remove,sizeof remove);}}
    Export(const Export &)=delete;Export &operator=(const Export &)=delete;
    void write(std::string_view bytes){
        wf::require(!bytes.empty()&&bytes.size()<=65536&&size_+bytes.size()<=std::uint64_t{2}*1024*1024*1024);DWORD count{};
        wf::require(::WriteFile(file_.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size());size_+=count;
    }
    void publish(std::uint64_t expected){
        const auto snapshot=wf::inspect(file_.get(),false);wf::require(expected>0&&size_==expected&&snapshot.standard.EndOfFile.QuadPart==static_cast<std::int64_t>(expected));
        wf::verify_exact_owner(file_.get(),wf::current_sid());wf::verify_security(file_.get(),wf::current_sid(),true,false);wf::require(::FlushFileBuffers(file_.get()));
        const auto name=wf::api_path(destination_);const auto bytes=name.size()*sizeof(wchar_t);
        std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO)+bytes,0);auto *rename=reinterpret_cast<FILE_RENAME_INFO *>(storage.data());
        rename->ReplaceIfExists=FALSE;rename->RootDirectory=nullptr;rename->FileNameLength=static_cast<DWORD>(bytes);std::memcpy(rename->FileName,name.data(),bytes);
        wf::require(::SetFileInformationByHandle(file_.get(),FileRenameInfo,rename,static_cast<DWORD>(storage.size())));published_=true;
        wf::require(::FlushFileBuffers(file_.get()));file_=wf::Handle();wf::flush_directory(destination_.parent_path(),wf::current_sid());
    }
private:
    std::filesystem::path destination_;wf::ParentChain parents_;wf::Handle directory_,file_;std::uint64_t size_{};bool published_{};
};
}
#endif
