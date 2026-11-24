// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "application_recovery/maintenance_io_windows.hpp"
#include "hardware_access_policy.hpp"
#include <cassert>
#include <fcntl.h>
#include <iostream>
#include <thread>
namespace wf=wsprrypi::windows_file;
namespace io=wsprrypi::maintenance_windows;
namespace {
template<class F>void refuses(F action){bool refused=false;try{action();}catch(const std::exception &){refused=true;}assert(refused);}
void write(const std::filesystem::path &path,std::string_view text){io::Export out(NativeWindowsFixture::utf8(path));out.write(text);out.publish(text.size());}
std::string read(const std::filesystem::path &path){const auto data=wf::read_protected(NativeWindowsFixture::utf8(path),true,65536);return {data.begin(),data.end()};}
int console_test(){
    wf::Handle input(::CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));DWORD original{};
    assert(input.get()!=INVALID_HANDLE_VALUE&&::GetConsoleMode(input.get(),&original));assert(::FlushConsoleInputBuffer(input.get()));
    const std::wstring secret=L"Ab\u00dfx\bC\r";std::vector<INPUT_RECORD> keys;
    for(const auto character:secret){INPUT_RECORD key{};key.EventType=KEY_EVENT;key.Event.KeyEvent.bKeyDown=TRUE;key.Event.KeyEvent.wRepeatCount=1;key.Event.KeyEvent.uChar.UnicodeChar=character;keys.push_back(key);}
    DWORD count{};assert(::WriteConsoleInputW(input.get(),keys.data(),static_cast<DWORD>(keys.size()),&count)&&count==keys.size());
    volatile std::sig_atomic_t interrupted{};assert(io::phrase_console(interrupted)=="Ab\xc3\x9f" "C"&&!interrupted);DWORD after{};
    assert(::GetConsoleMode(input.get(),&after)&&original==after);
    std::thread cancel([]{
        for(unsigned i=0;i<2000;++i){
            {std::lock_guard lock(io::console_state.mutex);if(io::console_state.active){assert(::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT,0));return;}}
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::terminate();
    });
    refuses([&]{(void)io::phrase_console(interrupted);});cancel.join();assert(interrupted==SIGINT);
    assert(::GetConsoleMode(input.get(),&after)&&original==after);return 0;
}
}
int main(int argc,char **argv){try{
    if(argc==2&&std::string_view(argv[1])=="--private-console")return console_test();
    assert(argc==1);
    for(const auto *value:{"","0","01","10","true"}){assert(!::_putenv_s("WSPRRYPI_DISABLE_HARDWARE_ACCESS",value));assert(!wsprrypi::hardware_access_disabled());}
    assert(!::_putenv_s("WSPRRYPI_DISABLE_HARDWARE_ACCESS","1")&&wsprrypi::hardware_access_disabled());assert(!::_putenv_s("WSPRRYPI_DISABLE_HARDWARE_ACCESS",""));
    NativeWindowsFixture fixture;const auto output=fixture.root/L"export.age";write(output,"encrypted fixture bytes");assert(read(output)=="encrypted fixture bytes");
    refuses([&]{write(output,"replacement");});assert(read(output)=="encrypted fixture bytes");
    {io::Export abandoned(NativeWindowsFixture::utf8(fixture.root/L"abandoned.age"));abandoned.write("partial");}
    assert(!std::filesystem::exists(fixture.root/L"abandoned.age"));
    for(const auto &entry:std::filesystem::directory_iterator(fixture.root))assert(entry.path()==output);
    refuses([&]{io::Export wrong(NativeWindowsFixture::utf8(fixture.root/L"wrong.age"));wrong.write("one");wrong.publish(2);});
    const auto secret=fixture.root/L"phrase";write(secret,"synthetic secret\n");
    HANDLE raw=::CreateFileW(secret.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);assert(raw!=INVALID_HANDLE_VALUE);
    const int descriptor=::_open_osfhandle(reinterpret_cast<intptr_t>(raw),_O_RDONLY|_O_BINARY);assert(descriptor>=3);
    assert(io::phrase_fd(descriptor)=="synthetic secret");assert(!::_close(descriptor));refuses([&]{(void)io::phrase_fd(1234567);});
    {
        wf::PrivateSecurity security(wf::current_sid());HANDLE reader{},writer{};assert(::CreatePipe(&reader,&writer,security.attributes(),4096));wf::Handle out(writer);
        DWORD count{};assert(::WriteFile(out.get(),"pipe phrase",11,&count,nullptr)&&count==11);out=wf::Handle();
        const auto pipe=::_open_osfhandle(reinterpret_cast<intptr_t>(reader),_O_RDONLY|_O_BINARY);assert(pipe>=3);
        assert(io::phrase_fd(pipe)=="pipe phrase");assert(!::_close(pipe));
    }
    const auto alias=fixture.root/L"alias";assert(::CreateHardLinkW(alias.c_str(),secret.c_str(),nullptr));
    raw=::CreateFileW(secret.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);assert(raw!=INVALID_HANDLE_VALUE);
    const int linked=::_open_osfhandle(reinterpret_cast<intptr_t>(raw),_O_RDONLY|_O_BINARY);assert(linked>=3);refuses([&]{(void)io::phrase_fd(linked);});assert(!::_close(linked));assert(::DeleteFileW(alias.c_str()));
    std::array<wchar_t,32768> image{};const auto size=::GetModuleFileNameW(nullptr,image.data(),static_cast<DWORD>(image.size()));assert(size>0&&size<image.size());
    std::wstring command=L"\""+std::wstring(image.data(),size)+L"\" --private-console";STARTUPINFOW startup{};startup.cb=sizeof startup;startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    assert(::CreateProcessW(image.data(),command.data(),nullptr,nullptr,FALSE,CREATE_NEW_CONSOLE,nullptr,fixture.root.c_str(),&startup,&process));wf::Handle child(process.hProcess),thread(process.hThread);
    if(::WaitForSingleObject(child.get(),20000)!=WAIT_OBJECT_0){::TerminateProcess(child.get(),2);::WaitForSingleObject(child.get(),5000);assert(false);}
    DWORD code{};assert(::GetExitCodeProcess(child.get(),&code)&&code==0);
    std::cout<<"Native maintenance private passphrase FD, hidden Unicode console and cancellation restoration, exclusive encrypted export, collision preservation and failed-export retirement passed\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
