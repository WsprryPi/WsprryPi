// SPDX-License-Identifier: MIT
#include "application_control/local_transport.hpp"
#include "software_child_process.hpp"
#include <atomic>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <utility>
#include <unistd.h>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
template<class F>void refusal(F action,LocalControlError::Code expected){
    try{action();}catch(const LocalControlError &error){check(error.code()==expected,"Wrong refusal class");return;}
    throw std::runtime_error("Expected local control refusal");
}
struct Socket {
    int fd{-1};explicit Socket(int value):fd(value){check(fd>=0,"Socket creation");}
    ~Socket(){if(fd>=0)::close(fd);}
};
sockaddr_un address(const std::string &path){sockaddr_un result{};result.sun_family=AF_UNIX;check(path.size()<sizeof result.sun_path,"Socket path bound");std::memcpy(result.sun_path,path.c_str(),path.size()+1);return result;}
Socket connection(const std::string &path){
    Socket result(::socket(AF_UNIX,SOCK_STREAM,0));const auto endpoint=address(path);
    check(!::connect(result.fd,reinterpret_cast<const sockaddr *>(&endpoint),sizeof endpoint),"Raw client connect");
    timeval timeout{2,0};check(!::setsockopt(result.fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout),"Raw timeout");return Socket(std::exchange(result.fd,-1));
}
void transfer(int fd,char *bytes,std::size_t length,bool send){
    while(length){const auto count=send ? ::send(fd,bytes,length,0) : ::recv(fd,bytes,length,0);
        if(count<0&&errno==EINTR)continue;
        check(count>0,"Raw frame transfer");bytes+=count;length-=count;}
}
void frame(int fd,std::string bytes){
    const std::uint32_t size=static_cast<std::uint32_t>(bytes.size());
    char header[]{char(size>>24),char(size>>16),char(size>>8),char(size)};
    transfer(fd,header,4,true);transfer(fd,bytes.data(),bytes.size(),true);
}
std::string receive(int fd){
    char header[4]{};transfer(fd,header,4,false);std::uint32_t length=0;
    for(const unsigned char value:header)length=(length<<8)|value;
    check(length>0&&length<=32768,"Raw frame allocation bound");std::string result(length,'\0');transfer(fd,result.data(),result.size(),false);return result;
}
}
int main(int argc,char *argv[]){
    try{
        if(argc==3&&std::string_view(argv[1])=="--web-client"){
            check(geteuid()==0&&!::setuid(1),"Unprivileged web peer fixture");
            LocalTransportOptions options{argv[2],0,1,getegid()};
            refusal([&]{local_control_request(options,{LocalOperation::SettingsRead,Json::object()});},LocalControlError::Code::Unavailable);
            for(const auto operation:{LocalOperation::RecoveryCapture,LocalOperation::RecoveryValidate,LocalOperation::RecoveryPrepare,
                                     LocalOperation::RecoveryReview,LocalOperation::RecoverySessionIssue,LocalOperation::DiagnosticRead})
                refusal([&]{local_control_request(options,{operation,{{"job_id",std::string(32,'a')}}});},
                        operation==LocalOperation::DiagnosticRead?LocalControlError::Code::Unavailable:LocalControlError::Code::UnknownOutcome);
            check(local_control_request(options,{LocalOperation::ListenerRead,Json::object()})==Json{{"ok",true},{"value",{{"role","pi"}}}},
                "Web peer can read only the admitted secret-free listener contract");
            return 0;
        }
        if(argc==3&&std::string_view(argv[1])=="--client"){
            LocalTransportOptions options{argv[2],geteuid(),static_cast<uid_t>(geteuid()+1),getegid()};
            const auto result=local_control_request(options,{LocalOperation::SettingsRead,Json::object()});
            check(result==Json{{"ok",true},{"value",{{"source","backend"}}}},"Independent process reply");return 0;
        }
        const auto parent=std::filesystem::canonical("/tmp").string();
        auto pattern=parent+"/local-control-XXXXXX";std::vector<char> temporary(pattern.begin(),pattern.end());temporary.push_back(0);
        const auto created=mkdtemp(temporary.data());check(created,"Private temporary root");const std::string root=created;
        struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}}remove{root};
        const LocalTransportOptions options{root+"/run",geteuid(),static_cast<uid_t>(geteuid()+1),getegid()};
        std::atomic<unsigned> mutations{0};
        LocalControlServer server(options,std::string(32,'a'),[&](LocalPeer peer,const LocalRequest &request){
            if(request.operation==LocalOperation::ListenerRead){check(request.arguments.empty(),"Closed listener arguments");return Json{{"role","pi"}};}
            check(peer==LocalPeer::Administrator,"Actual local administrator peer");
            if(request.operation==LocalOperation::SettingsRead){check(request.arguments.empty(),"Closed read arguments");return Json{{"source","backend"}};}
            if(request.arguments==Json{{"value",18}}){++mutations;throw std::runtime_error("Fixture failure after effect");}
            if(request.operation!=LocalOperation::SettingsWrite||request.arguments!=Json{{"value",17}})
                throw LocalCommandRefused(LocalRefusal::InvalidRequest);
            ++mutations;return Json{{"saved",17}};
        });
        server.start();
        auto event_options=options;event_options.channel=LocalChannel::Events;
        LocalControlServer events(event_options,std::string(32,'a'),[](LocalPeer,const LocalRequest &request){
            check(request.operation==LocalOperation::EventsRead&&request.arguments.empty(),"Events endpoint has a closed operation set");
            return Json{{"message",std::string(64000,'x')}};
        });
        events.start();
        check(local_control_request(event_options,{LocalOperation::EventsRead,Json::object()})["value"]["message"].get_ref<const std::string &>().size()==64000,
            "Event reply has an independent bound without increasing control payload limits");
        {
            auto slow=connection(options.directory+"/events.sock");(void)receive(slow.fd);
            const auto began=std::chrono::steady_clock::now();
            check(local_control_request(options,{LocalOperation::SettingsRead,Json::object()})["ok"]==true,
                "A stalled event consumer cannot occupy a control worker");
            check(std::chrono::steady_clock::now()-began<std::chrono::seconds(2),"Independent traffic pools avoid event timeout on control");
        }
        events.stop();
        if(geteuid()==0){
            check(!::chmod(root.c_str(),0750),"Allow web fixture traversal through runtime parent only");
            check(testing::wait_software_child(testing::launch_software_child(argv[0],{"--web-client",options.directory}))==0&&mutations==0,
                "A different actual OS web identity cannot call administrator operations");
        }
        check(testing::wait_software_child(testing::launch_software_child(argv[0],{"--client",options.directory}))==0,"Independent credential-verified process");
        const auto saved=local_control_request(options,{LocalOperation::SettingsWrite,{{"value",17}}});
        check(saved==Json{{"ok",true},{"value",{{"saved",17}}}}&&mutations==1,"Exactly one admitted mutation");
        check(local_control_request(options,{LocalOperation::SettingsWrite,{{"value",true}}})==Json{{"ok",false},{"error","invalid_request"}}&&mutations==1,"Refused mutation has no effect");
        refusal([&]{local_control_request(options,{LocalOperation::SettingsWrite,{{"value",18}}});},LocalControlError::Code::UnknownOutcome);
        check(mutations==2,"Post-effect handler failure reports uncertainty without replay");
        const auto path=options.directory+"/control.sock";struct stat info{};
        check(!::lstat(path.c_str(),&info)&&(info.st_mode&0777)==0660,"Restricted socket permissions");
        check(!::lstat(options.directory.c_str(),&info)&&(info.st_mode&0777)==0750,"Restricted runtime directory");
        LocalControlServer duplicate(options,std::string(32,'b'),[](auto,const auto &){return Json::object();});
        refusal([&]{duplicate.start();},LocalControlError::Code::Unavailable);
        check(!::chmod(path.c_str(),0666),"Unsafe socket fixture");
        refusal([&]{local_control_request(options,{LocalOperation::SettingsRead,Json::object()});},LocalControlError::Code::Unauthorized);
        check(!::chmod(path.c_str(),0660),"Restore fixture socket permissions");
        {
            auto raw=connection(path);(void)receive(raw.fd);
            char excessive[]{0x7f,0x7f,0x7f,0x7f};transfer(raw.fd,excessive,4,true);
            char byte{};check(::recv(raw.fd,&byte,1,0)==0,"Oversize request closes before allocation/dispatch");
        }
        {
            auto raw=connection(path);const auto hello=Json::parse(receive(raw.fd));
            const auto bad=std::string("{\"protocol\":\"wsprrypi-local/1\",\"protocol\":\"wsprrypi-local/1\",\"boot\":\"")+hello["boot"].get<std::string>()+
                "\",\"connection\":\""+hello["connection"].get<std::string>()+"\",\"sequence\":\"1\",\"operation\":\"settings.write\",\"arguments\":{\"value\":17}}";
            frame(raw.fd,bad);char byte{};check(::recv(raw.fd,&byte,1,0)==0&&mutations==2,"Duplicate fields refused before mutation");
        }
        server.stop();check(!std::filesystem::exists(path),"Only owned socket removed on stop");
        // A real credential-verified peer accepts the complete mutation, then
        // loses its reply. The production client reports uncertainty and sends
        // no replay, independently of whether a database decision committed.
        Socket fake(::socket(AF_UNIX,SOCK_STREAM,0));const auto endpoint=address(path);
        check(!::bind(fake.fd,reinterpret_cast<const sockaddr *>(&endpoint),sizeof endpoint)&&!::chmod(path.c_str(),0660)&&!::listen(fake.fd,1),"Lost-reply peer fixture");
        std::atomic_bool accepted{false};
        std::thread lost([&]{
            Socket peer(::accept(fake.fd,nullptr,nullptr));
            frame(peer.fd,Json{{"protocol","wsprrypi-local/1"},{"boot",std::string(32,'c')},{"connection",std::string(32,'d')}}.dump());
            accepted=Json::parse(receive(peer.fd)).at("operation")=="settings.write";
            ::shutdown(peer.fd,SHUT_RDWR);
        });
        try{refusal([&]{local_control_request(options,{LocalOperation::SettingsWrite,{{"value",17}}});},LocalControlError::Code::UnknownOutcome);}
        catch(...){lost.join();throw;}
        lost.join();check(accepted,"Mutation reached peer exactly once before lost reply");
        std::cout<<"PASS: real local processes, OS peer verification, private sockets, bounded frames, closed input, mutation refusal and lost-reply uncertainty without replay\n";return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
