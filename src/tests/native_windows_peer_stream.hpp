// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "wtp_integration/native_socket.hpp"
#include "native_windows_arguments.hpp"
#include "application_recovery/private_json.hpp"
#include "WTP-Client/include/wtp/transport.hpp"
#include <chrono>
namespace wsprrypi::testing {
// Test transport only: raw bytes to the explicitly selected actual software
// Pico USB endpoint. No physical USB inventory, opening, or protocol substitute.
class NativeUsbPeerStream final:public wtp::ByteStream {
    native_socket::Handle socket_{native_socket::invalid};
    std::string session_;
public:
    explicit NativeUsbPeerStream(std::string session):session_(std::move(session)){restart();}
    ~NativeUsbPeerStream(){close();}
    NativeUsbPeerStream(const NativeUsbPeerStream &)=delete;
    NativeUsbPeerStream &operator=(const NativeUsbPeerStream &)=delete;
    void restart(){
        close();
        try{
            const auto path=windows_file::path_from_utf8(session_);
            windows_file::require(path.filename()==L"peer-session.json");
            const auto bytes=windows_file::read_protected(session_,true,4096);
            const auto session=parse_recovery_json(std::string(bytes.begin(),bytes.end()),4096,4);
            windows_file::require(session.is_object()&&session.size()==3&&session.contains("schema")&&session.contains("nonce")&&session.contains("port"));
            windows_file::require(session["schema"]=="issue475-software-peer/1"&&session["nonce"].is_string()&&session["port"].is_number_unsigned()&&session["port"]==18445);
            const auto nonce=session["nonce"].get<std::string>();
            windows_file::require(nonce.size()==32&&nonce.find_first_not_of("0123456789abcdef")==std::string::npos);
            socket_=native_socket::open_stream(AF_INET);windows_file::require(socket_!=native_socket::invalid);
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(18445);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
            const auto result=::connect(socket_,reinterpret_cast<sockaddr *>(&address),sizeof(address));
            windows_file::require(!result||native_socket::pending(native_socket::error()));
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            auto writable=[&]{for(;;){
                windows_file::require(std::chrono::steady_clock::now()<deadline);
                native_socket::PollItem item{socket_,native_socket::writable,0};const auto ready=native_socket::poll(&item,1,100);
                if(ready<0&&native_socket::interrupted(native_socket::error()))continue;
                windows_file::require(ready>=0&&!(item.revents&native_socket::failed));
                if(ready&&item.revents&native_socket::writable)return;
            }};
            writable();windows_file::require(native_socket::connected(socket_));
            const auto header=nlohmann::json{{"schema","issue475-software-peer/1"},{"nonce",nonce},{"kind","usb"},{"args",nlohmann::json::array()}}.dump()+"\n";
            std::size_t offset{};while(offset<header.size()){
                writable();const auto count=native_socket::send(socket_,header.data()+offset,header.size()-offset);
                if(count<0&&native_socket::retry(native_socket::error()))continue;
                windows_file::require(count>0);offset+=static_cast<std::size_t>(count);
            }
        }catch(...){close();throw;}
    }
    wtp::IoResult read(std::span<std::uint8_t> bytes)override{
        const auto count=native_socket::receive(socket_,reinterpret_cast<char *>(bytes.data()),std::min<std::size_t>(bytes.size(),17));
        if(count>0)return {wtp::IoState::Progress,static_cast<std::size_t>(count)};
        return {count<0&&native_socket::retry(native_socket::error())?wtp::IoState::WouldBlock:wtp::IoState::Closed,0};
    }
    wtp::IoResult write(std::span<const std::uint8_t> bytes)override{
        const auto count=native_socket::send(socket_,reinterpret_cast<const char *>(bytes.data()),std::min<std::size_t>(bytes.size(),23));
        if(count>0)return {wtp::IoState::Progress,static_cast<std::size_t>(count)};
        return {count<0&&native_socket::retry(native_socket::error())?wtp::IoState::WouldBlock:wtp::IoState::Failed,0};
    }
    void close()noexcept override{native_socket::close(socket_);socket_=native_socket::invalid;}
};
}
#endif
