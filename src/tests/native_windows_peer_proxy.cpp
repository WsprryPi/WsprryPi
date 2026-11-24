// SPDX-License-Identifier: MIT
// Software peer relay only. No product endpoint, physical carrier, or RF path.
#include "wtp_integration/native_socket.hpp"
#include "native_windows_arguments.hpp"
#include "application_recovery/private_json.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
namespace ns=wsprrypi::native_socket;
namespace wf=wsprrypi::windows_file;
namespace {
using Clock=std::chrono::steady_clock;
void require(bool value){if(!value)throw std::runtime_error("Software peer relay unavailable");}
struct Socket {ns::Handle value{ns::invalid};~Socket(){ns::close(value);}};
void ready(ns::Handle fd,short events,Clock::time_point deadline,const std::atomic_bool &done){
    while(!done.load()&&Clock::now()<deadline){
        ns::PollItem item{fd,events,0};const auto result=ns::poll(&item,1,100);
        if(result<0){if(ns::interrupted(ns::error()))continue;throw std::runtime_error("Software peer poll failed");}
        if(result>0&&item.revents&events)return;
        require(!(item.revents&ns::failed));
    }
    throw std::runtime_error("Software peer relay deadline");
}
void send_all(ns::Handle fd,const char *data,std::size_t size,const std::atomic_bool &done){
    const auto deadline=Clock::now()+std::chrono::seconds(5);
    while(size){
        ready(fd,ns::writable,deadline,done);const auto count=ns::send(fd,data,size);
        if(count<0&&ns::retry(ns::error()))continue;
        require(count>0);data+=count;size-=static_cast<std::size_t>(count);
    }
}
}
int main(){try{
    const auto arguments=wsprrypi::native_windows_arguments();
    const std::vector<std::string> args(arguments.begin()+1,arguments.end());
    require(args==std::vector<std::string>({"0","a","settings","18443"})||
        args==std::vector<std::string>({"1","b","settings","18444","second"})||
        args==std::vector<std::string>({"7","a","settings","18443"}));
    std::array<wchar_t,32768> image{};const auto length=::GetModuleFileNameW(nullptr,image.data(),static_cast<DWORD>(image.size()));
    require(length>0&&length<image.size());const std::filesystem::path path(std::wstring(image.data(),length));
    require(path.filename()==L"pico_tls_server.exe");
    const auto bytes=wf::read_protected(wsprrypi::windows_argument_utf8((path.parent_path()/L"peer-session.json").native()),true,4096);
    const auto session=wsprrypi::parse_recovery_json(std::string(bytes.begin(),bytes.end()),4096,4);
    require(session.is_object()&&session.size()==3&&session.contains("schema")&&session.contains("nonce")&&session.contains("port"));
    require(session["schema"]=="issue475-software-peer/1"&&session["nonce"].is_string()&&session["port"].is_number_unsigned());
    const auto nonce=session["nonce"].get<std::string>();require(nonce.size()==32&&nonce.find_first_not_of("0123456789abcdef")==std::string::npos&&session["port"]==18445);
    std::atomic_bool done{},failed{};Socket socket;socket.value=ns::open_stream(AF_INET);require(socket.value!=ns::invalid);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(18445);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    const auto connection=::connect(socket.value,reinterpret_cast<sockaddr *>(&address),sizeof(address));
    require(connection==0||ns::pending(ns::error()));ready(socket.value,ns::writable,Clock::now()+std::chrono::seconds(5),done);require(ns::connected(socket.value));
    const auto header=nlohmann::json{{"schema","issue475-software-peer/1"},{"nonce",nonce},{"kind","tls"},{"args",args}}.dump()+"\n";
    send_all(socket.value,header.data(),header.size(),done);
    std::thread input([&]{try{
        std::array<char,65536> buffer{};std::uint64_t total{};
        while(!done.load()){
            DWORD count{};const auto ok=::ReadFile(::GetStdHandle(STD_INPUT_HANDLE),buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr);
            if(!ok||!count){if(!done.load())(void)::shutdown(socket.value,SD_SEND);break;}
            total+=count;require(total<=64*1024*1024);send_all(socket.value,buffer.data(),count,done);
        }
    }catch(...){failed=true;done=true;ns::shutdown(socket.value);}});
    try{
        const auto deadline=Clock::now()+std::chrono::seconds(600);std::array<char,65536> buffer{};std::uint64_t total{};
        while(!done.load()){
            ready(socket.value,ns::readable,deadline,done);const auto count=ns::receive(socket.value,buffer.data(),buffer.size());
            if(count<0&&ns::retry(ns::error()))continue;
            require(count>=0);if(!count)break;total+=static_cast<unsigned>(count);require(total<=64*1024*1024);
            DWORD offset{};
            while(offset<static_cast<DWORD>(count)){
                DWORD written{};require(::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE),buffer.data()+offset,static_cast<DWORD>(count)-offset,&written,nullptr)&&written>0);offset+=written;
            }
        }
    }catch(...){failed=true;}
    done=true;ns::shutdown(socket.value);(void)::CancelSynchronousIo(input.native_handle());input.join();return failed.load()?1:0;
}catch(...){std::fputs("Software peer relay unavailable\n",stderr);return 2;}}
