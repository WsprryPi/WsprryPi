// SPDX-License-Identifier: MIT
#include "native_windows_process.hpp"
#include "native_windows_child_pipe.hpp"
#include "native_windows_fixture.hpp"
#include "native_windows_arguments.hpp"
#include "json.hpp"
#include <cassert>
#include <fstream>
#include <future>
#include <iostream>
namespace wf=wsprrypi::windows_file;
namespace wp=wsprrypi::windows_process;
namespace {
std::wstring executable(){std::array<wchar_t,32768> value{};const auto size=::GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));assert(size>0&&size<value.size());return {value.data(),size};}
void emit(const nlohmann::json &value){
    const auto bytes=value.dump();assert(bytes.size()<=8000);std::string wire(4,'\0');
    for(unsigned index=0;index<4;++index)wire[index]=static_cast<char>((bytes.size()>>(24-index*8))&255);
    wire+=bytes;DWORD count{};assert(::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE),wire.data(),static_cast<DWORD>(wire.size()),&count,nullptr)&&count==wire.size());
}
nlohmann::json receive(HANDLE pipe){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    const auto read=[&](std::size_t size){
        std::string result;
        while(result.size()<size&&std::chrono::steady_clock::now()<deadline){
            DWORD available{};assert(::PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr));
            if(!available){std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}
            const auto count=static_cast<DWORD>(std::min<std::size_t>(available,size-result.size()));std::string bytes(count,'\0');DWORD got{};
            assert(::ReadFile(pipe,bytes.data(),count,&got,nullptr)&&got>0&&got<=count);result.append(bytes.data(),got);
        }
        assert(result.size()==size);return result;
    };
    const auto prefix=read(4);std::size_t size=0;for(const auto byte:prefix)size=(size<<8)|static_cast<unsigned char>(byte);
    assert(size>0&&size<=8000);return nlohmann::json::parse(read(size));
}
void copy_program(const std::filesystem::path &destination){
    std::ifstream input(std::filesystem::path(executable()),std::ios::binary);assert(input);
    wf::PrivateSecurity security(wf::current_sid());wf::Handle output(::CreateFileW(destination.c_str(),GENERIC_WRITE,0,security.attributes(),CREATE_NEW,FILE_FLAG_WRITE_THROUGH,nullptr));assert(output.get()!=INVALID_HANDLE_VALUE);
    std::array<char,65536> bytes{};
    while(input){input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));const auto count=input.gcount();if(!count)break;
        DWORD written{};assert(::WriteFile(output.get(),bytes.data(),static_cast<DWORD>(count),&written,nullptr)&&written==count);}
    assert(input.eof()&&::FlushFileBuffers(output.get()));
}
}
int main(){const char *stage="entry";try{
    const auto arguments=wsprrypi::native_windows_arguments();
    if(arguments.size()>=2&&arguments[1]=="--echo"){
        wchar_t ignored{};assert(!::GetEnvironmentVariableW(L"WSPRRYPI_CHILD_LEAK_TEST",&ignored,1));
        std::array<wchar_t,32768> drive{};const auto count=::GetEnvironmentVariableW(L"SYSTEMDRIVE",drive.data(),static_cast<DWORD>(drive.size()));
        assert(count>0&&count<drive.size()&&std::wstring(drive.data(),count)==std::filesystem::path(wp::system_directory(true)).root_name().native());
        emit(nlohmann::json(arguments));return 0;
    }
    if(arguments.size()==4&&arguments[1]=="--pipe-copy"){
        const auto handle=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(arguments[2])));
        std::array<char,4> buffer{};DWORD count{};assert(::GetFileType(handle)==FILE_TYPE_PIPE);
        const auto gate=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(arguments[3])));
        assert(::WaitForSingleObject(gate,5000)==WAIT_OBJECT_0); // Writer has already closed.
        assert(::ReadFile(handle,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)&&count==buffer.size());
        char extra{};const auto ended=::ReadFile(handle,&extra,1,&count,nullptr);
        assert((ended&&count==0)||(!ended&&::GetLastError()==ERROR_BROKEN_PIPE));
        assert(::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE),buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)&&count==buffer.size());return 0;
    }
    if(arguments.size()==2&&arguments[1]=="--sleep"){for(;;)::Sleep(50);}
    if(arguments.size()==2&&(arguments[1]=="--descendant"||arguments[1]=="--exit-with-descendant")){
        const auto program=executable();auto command=wp::quote(program)+L" --sleep";STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION child{};
        assert(::CreateProcessW(program.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child));
        wf::Handle process(child.hProcess),thread(child.hThread);emit({{"pid",child.dwProcessId}});if(arguments[1]=="--exit-with-descendant")return 0;for(;;)::Sleep(50);
    }
    assert(arguments.size()==1);NativeWindowsFixture fixture;
    const auto image=fixture.root/L"child-\u03a9.exe";copy_program(image);const auto program=NativeWindowsFixture::utf8(image),root=NativeWindowsFixture::utf8(fixture.root);
    wf::Handle null(::CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));assert(null.get()!=INVALID_HANDLE_VALUE);
    const auto launch=[&](const std::vector<std::wstring> &values,wf::Handle &read,wf::Handle &write){
        HANDLE first{},second{};assert(::CreatePipe(&first,&second,nullptr,8192));read=wf::Handle(first);write=wf::Handle(second);
        return std::make_unique<wp::OwnedChild>(program,root,std::array<HANDLE,3>{null.get(),write.get(),null.get()},std::vector<HANDLE>{},
            [&](const auto &){return values;});
    };
    assert(::SetEnvironmentVariableW(L"WSPRRYPI_CHILD_LEAK_TEST",L"must not inherit"));
    const auto drive=std::filesystem::path(wp::system_directory(true)).root_name().native();
    const auto poisoned_drive=drive==L"Z:"?L"Y:":L"Z:";
    assert(::SetEnvironmentVariableW(L"SYSTEMDRIVE",poisoned_drive));
    wf::Handle read,write;const std::vector<std::wstring> values{L"--echo",L"",L"spaces and \u03a9",L"trailing\\",L"quote\"inside",L"\\\"",L";$(literal)&"};
    auto child=launch(values,read,write);write=wf::Handle();assert(child->wait(std::chrono::seconds(10))==0);const auto reply=receive(read.get());
    assert(reply.size()==values.size()+1);for(std::size_t index=0;index<values.size();++index)assert(reply[index+1]==wsprrypi::windows_argument_utf8(values[index]));
    child.reset();assert(::SetEnvironmentVariableW(L"WSPRRYPI_CHILD_LEAK_TEST",nullptr));
    assert(::SetEnvironmentVariableW(L"SYSTEMDRIVE",drive.c_str()));
    {
        stage="binary pipe creation";
        wp::ChildPipe input(wp::ChildPipe::Direction::ToChild),output(wp::ChildPipe::Direction::FromChild);
        wf::Handle gate(::CreateEventW(nullptr,FALSE,FALSE,nullptr));assert(gate.get()!=nullptr);
        stage="binary child launch";
        wp::OwnedChild copy(program,root,{null.get(),output.child(),null.get()},{input.child(),gate.get()},
            [](const auto &handles){assert(handles.size()==2);return std::vector<std::wstring>{L"--pipe-copy",std::to_wstring(reinterpret_cast<std::uintptr_t>(handles[0])),std::to_wstring(reinterpret_cast<std::uintptr_t>(handles[1]))};});
        input.close_child();output.close_child();const std::array<std::uint8_t,4> bytes{'a',0,'\n','\r'};
        stage="binary input write";
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);input.send(bytes,deadline);
        input.close_write();
        assert(::SetEvent(gate.get()));
        stage="binary output read";
        std::string echoed;while(echoed.size()<bytes.size())echoed+=output.read(deadline);
        assert(echoed==std::string(reinterpret_cast<const char *>(bytes.data()),bytes.size())&&copy.wait(std::chrono::seconds(5))==0);
    }
    {
        stage="bounded read cancellation";
        wp::ChildPipe output(wp::ChildPipe::Direction::FromChild);
        wp::OwnedChild sleeping(program,root,{null.get(),output.child(),null.get()},{},[](const auto &){return std::vector<std::wstring>{L"--sleep"};});
        output.close_child();bool timeout=false;
        try{(void)output.read(std::chrono::steady_clock::now()+std::chrono::milliseconds(20));}catch(const std::exception &){timeout=true;}
        assert(timeout);sleeping.stop();
    }
    child=launch({L"--descendant"},read,write);write=wf::Handle();const auto descendant=receive(read.get());
    wf::Handle owned_descendant(::OpenProcess(SYNCHRONIZE,FALSE,descendant.at("pid").get<DWORD>()));assert(owned_descendant.get()!=nullptr&&child->alive());
    assert(::WaitForSingleObject(owned_descendant.get(),0)==WAIT_TIMEOUT);child.reset();assert(::WaitForSingleObject(owned_descendant.get(),5000)==WAIT_OBJECT_0);
    child=launch({L"--exit-with-descendant"},read,write);write=wf::Handle();const auto stranded=receive(read.get());
    wf::Handle pending_descendant(::OpenProcess(SYNCHRONIZE,FALSE,stranded.at("pid").get<DWORD>()));assert(pending_descendant.get()!=nullptr);
    bool held=false;try{(void)child->wait(std::chrono::milliseconds(30));}catch(const std::exception &){held=true;}
    assert(held&&::WaitForSingleObject(pending_descendant.get(),5000)==WAIT_OBJECT_0);child.reset();
    child=launch({L"--sleep"},read,write);write=wf::Handle();std::promise<void> waiting;auto started=waiting.get_future();
    auto cancelled=std::async(std::launch::async,[&]{waiting.set_value();try{(void)child->wait(std::chrono::seconds(10));return false;}catch(const std::exception &){return true;}});
    started.get();std::this_thread::sleep_for(std::chrono::milliseconds(20));child->stop();
    assert(cancelled.wait_for(std::chrono::seconds(6))==std::future_status::ready&&cancelled.get());child.reset();
    std::cout<<"Native private child handles, Unicode/quote round trips, sanitized environment, and owned descendant teardown passed\n";return 0;
}catch(const std::exception &error){std::cerr<<stage<<": "<<error.what()<<'\n';return 1;}}
