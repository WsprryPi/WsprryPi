// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "native_windows_arguments.hpp"
#include "native_windows_child_pipe.hpp"
#include "application_control/local_pipe_windows.hpp"
#include "application_control/runtime_options.hpp"
#include <atomic>
#include <iostream>
#include <thread>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
namespace pipe=local_pipe;
namespace protocol=local_protocol;
void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
template<class F>void refused(F action){try{action();}catch(const LocalControlError &){return;}throw std::runtime_error("Expected transport refusal");}
LocalTransportOptions selected(const std::string &root){return {root,windows_file::current_sid(),L"",LocalChannel::Control,{},false};}
void child(const std::string &root){
    const auto result=local_control_request(selected(root),{LocalOperation::SettingsRead,Json::object()});
    check(result==Json{{"ok",true},{"value",{{"source","backend"}}}},"Independent native process reply");
}
void launch(const std::filesystem::path &root){
    std::vector<wchar_t> executable(32768);const auto length=::GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    check(length>0&&length<executable.size(),"Native executable path");
    const std::wstring application(executable.data(),length);
    auto command=L"\""+application+L"\" --child \""+root.native()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION process{};
    check(::CreateProcessW(application.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Independent native process launch");
    windows_file::Handle thread(process.hThread),handle(process.hProcess);
    const auto waited=::WaitForSingleObject(handle.get(),15000);
    if(waited!=WAIT_OBJECT_0){::TerminateProcess(handle.get(),1);::WaitForSingleObject(handle.get(),2000);throw std::runtime_error("Child deadline");}
    DWORD code{};check(::GetExitCodeProcess(handle.get(),&code)&&code==0,"Independent native process result");
}
}
int main(){
    try{
        {
            windows_process::ChildPipe delayed(windows_process::ChildPipe::Direction::FromChild);
            bool wrote=false;std::jthread writer([&]{std::this_thread::sleep_for(std::chrono::seconds(6));DWORD count{};
                wrote=::WriteFile(delayed.child(),"ready",5,&count,nullptr)&&count==5;});
            check(delayed.read(std::chrono::steady_clock::now()+std::chrono::seconds(9))=="ready","Owner budget survives native wait intervals");
            writer.join();check(wrote,"Delayed private pipe writer");
        }

        const auto arguments=native_windows_arguments();
        if(arguments.size()==3&&arguments[1]=="--child"){child(arguments[2]);return 0;}
        NativeWindowsFixture fixture;const auto options=selected(NativeWindowsFixture::utf8(fixture.root));
        std::atomic<unsigned> mutations{};
        LocalControlServer server(options,std::string(32,'a'),[&](LocalPeer peer,const LocalRequest &request){
            check(peer==LocalPeer::Administrator,"Actual OS token classified");
            pipe::require_primary_thread(); // Authentication must have reverted.
            if(request.operation==LocalOperation::SettingsRead)return Json{{"source","backend"}};
            if(request.arguments==Json{{"value",18}}){++mutations;throw std::runtime_error("Post-effect fixture failure");}
            if(request.operation!=LocalOperation::SettingsWrite||request.arguments!=Json{{"value",17}})throw LocalCommandRefused(LocalRefusal::InvalidRequest);
            ++mutations;return Json{{"saved",17}};
        });
        server.start();launch(fixture.root);
        check(local_control_request(options,{LocalOperation::SettingsWrite,{{"value",17}}})["ok"]==true&&mutations==1,"One admitted mutation");
        check(local_control_request(options,{LocalOperation::SettingsWrite,{{"value",false}}})["error"]=="invalid_request"&&mutations==1,"Refusal before mutation");
        try{(void)local_control_request(options,{LocalOperation::SettingsWrite,{{"value",18}}});throw std::runtime_error("Unknown outcome expected");}
        catch(const LocalControlError &error){check(error.code()==LocalControlError::Code::UnknownOutcome&&mutations==2,"Post-effect uncertainty without replay");}
        LocalControlServer duplicate(options,std::string(32,'b'),[](auto,const auto &){return Json::object();});refused([&]{duplicate.start();});
        child(options.directory);
        auto wrong=options;wrong.backend_sid=L"S-1-5-18";refused([&]{LocalControlConnection client(wrong);});
        // Malformed and stale envelopes traverse the real authenticated pipe,
        // then must close before the handler can observe a mutation.
        for(const auto variant:{0,1,2,3}){
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            auto endpoint=pipe::open(pipe::name(options),deadline);
            auto auth=pipe::authenticate_server(endpoint.get(),options,deadline);
            auto io=[&](void *data,std::size_t size,bool sending,protocol::Deadline limit){pipe::transfer(endpoint.get(),data,size,sending,limit);};
            const auto hello=protocol::receive_frame(io,deadline);
            Json request{{"protocol",protocol::protocol},{"boot",hello["boot"]},{"connection",hello["connection"]},
                {"sequence","1"},{"operation","settings.write"},{"arguments",{{"value",17}}}};
            if(variant==0)request["boot"]=std::string(32,'b');
            if(variant==1)request["sequence"]="2";
            if(variant==2)request["sid"]="S-1-5-18";
            if(variant==3)request["operation"]="unknown.operation";
            protocol::send_frame(io,request,deadline);refused([&]{(void)protocol::receive_frame(io,deadline);});
            check(mutations==2,"Invalid envelope refused before dispatch");
        }
        for(const auto channel:{LocalChannel::Events,LocalChannel::Browser,LocalChannel::Http,LocalChannel::WebService,LocalChannel::Recovery}){
            auto separate=options;separate.channel=channel;std::function<bool()> retained;
            LocalControlServer endpoint(separate,std::string(32,'a'),[&](auto,const LocalRequest &request){
                retained=request.transport_alive;check(retained&&retained(),"Live native connection probe");
                if(channel==LocalChannel::Recovery){
                    check(request.arguments==Json{{"bytes",std::string(87384,'x')}},"Complete maximum upload chunk reaches handler");
                    return Json{{"bytes",87384}};
                }
                return channel==LocalChannel::Events?Json{{"message",std::string(64000,'x')}}:Json::object();
            });endpoint.start();
            {
                LocalControlConnection connection(separate);
                const auto operation=channel==LocalChannel::Events?LocalOperation::EventsRead:channel==LocalChannel::Browser?LocalOperation::BrowserOpen:
                    channel==LocalChannel::Http?LocalOperation::HttpOpen:channel==LocalChannel::WebService?LocalOperation::WebAttach:LocalOperation::RecoveryCommand;
                const auto channel_arguments=channel==LocalChannel::Recovery?Json{{"bytes",std::string(87384,'x')}}:Json::object();
                const auto reply=connection.request({operation,channel_arguments});
                check(reply["ok"]==true,"Independent channel reply");
                if(channel==LocalChannel::Recovery)check(reply["value"]==Json{{"bytes",87384}},"Maximum upload chunk acknowledgement");
                if(channel==LocalChannel::Browser)check(connection.request({LocalOperation::BrowserCheck,Json::object()})["ok"]==true,"Persistent sequence advances");
                if(channel==LocalChannel::WebService)check(connection.request({LocalOperation::WebHeartbeat,Json::object()})["ok"]==true,"Persistent service sequence");
            }
            endpoint.stop();check(retained&&!retained(),"Retained callback refuses retired/reused handle");
        }
        // Cancellation must release all pending ConnectNamedPipe calls promptly.
        const auto began=std::chrono::steady_clock::now();server.stop();
        check(std::chrono::steady_clock::now()-began<std::chrono::seconds(2),"Idle native server stop deadline");
        server.start();child(options.directory);server.stop();
        testing::set_application_control_runtime_directory(options.directory);
        check(backend_control_options().backend_sid==options.backend_sid&&web_control_options().backend_sid==options.backend_sid,"Typed fixture uses actual SID");
        std::cout<<"PASS: native pipe authentication, independent process, closed protocol, uncertainty, channels, cancellation, and handle lifetime\n";return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
