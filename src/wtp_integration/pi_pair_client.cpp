// SPDX-License-Identifier: MIT
#include "pi_pair_client.hpp"
#include <string_view>
#include <array>
#include <chrono>
#include <thread>
#include <utility>
namespace wsprrypi {
namespace {
using Clock=std::chrono::steady_clock;
void require(bool value){if(!value)throw std::runtime_error("pairing_connection_unavailable");}
bool id(const std::string &value){return value.size()==32&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}

}
PiPairClient::PiPairClient(PiPairAdmission &budget,Access mode):admission_(budget),access_(mode){}
PiPairClientResult PiPairClient::exchange(const PiPairEndpoint &endpoint,const std::string &target,
    PiPairTlsChannel::Lookup lookup,const PiPairTlsIdentity &identity,const Admit &admit,
    const std::string &method,const std::string &path,const std::string &body,const httplib::Headers &metadata,
    const std::function<bool()> &external_cancel,const WriteAdmission &admission){
    PiPairClientResult result;
    try{
        require(id(target)&&lookup&&admit&&external_cancel&&endpoint.port&&endpoint.port<=65535&&
            (access_==Access::LoopbackTest||endpoint.port==31418));
        // Validate and freeze the entire request before admission or resolution.
        const auto host=(endpoint.host.find(':')==std::string::npos?endpoint.host:"["+endpoint.host+"]")+":"+std::to_string(endpoint.port);
        const auto wire=pi_pair_http_request(method,path,host,body,metadata);
        (void)pi_pair_tls_identity(identity);
        auto connection=admission_.try_outgoing();if(!connection)return result;
        struct Cancel {PiPairAdmission::Connection &connection;~Cancel(){connection.cancel();}} cancel{*connection};
        std::optional<PiPairAdmission::Mutation> mutation;
        if(method!="GET"){
            // The TLS peer is the managed Pi; a fixed gateway route can name
            // a distinct physical Pico. Count mutations against that exact
            // canonical resource target, preserving independent child slots.
            std::string managed=target;
            constexpr std::string_view prefix="/api/control/v1/targets/";
            if(path.starts_with(prefix)){
                const auto suffix=std::string_view(path).substr(prefix.size());
                require(suffix.size()>=34&&suffix[32]=='/'&&id(std::string(suffix.substr(0,32))));
                managed=suffix.substr(0,32);
            }
            mutation=connection->try_mutation(managed);if(!mutation)return result;
        }
        const auto started=Clock::now(),whole=started+std::chrono::seconds(24);
        const auto cancelled=[&]{return connection->cancelled()||external_cancel()||Clock::now()>=whole;};
        if(cancelled())return result;
        result.state=PiPairClientResult::State::Unavailable;
        auto socket=PiPairSocket::open(endpoint,access_==Access::PrivateLan?PiPairSocket::Access::PrivateLan:PiPairSocket::Access::LoopbackTest,whole,cancelled);
        PiPairTlsChannel channel(socket->release_fd(),std::move(lookup),identity);
        bool ready=false;
        while(!cancelled()){
            const auto state=channel.handshake();require(state!=PiPairTlsChannel::Handshake::Failed);
            if(state==PiPairTlsChannel::Handshake::Ready){ready=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(ready&&!cancelled()&&channel.principal()&&channel.principal()->peer_id()==target&&admit(*channel.principal()));
        const auto io_deadline=std::min(whole,Clock::now()+std::chrono::seconds(8));
        std::size_t sent=0;
        while(!cancelled()&&Clock::now()<io_deadline&&sent<wire.size()){
            // WouldBlock transferred no bytes: authority must still be current
            // at the next actual first handoff. A partial handoff is uncertain.
            if(!sent)require(admit(*channel.principal()));
            auto guard=!sent&&admission?admission(*channel.principal()):std::optional<PiPairMutationGuard>{};
            if(!sent&&admission)require(guard.has_value());
            // Final local admission may checkpoint a durable journal. Recheck
            // cancellation/deadlines after it returns, before any TLS write.
            require(!cancelled()&&Clock::now()<io_deadline);
            // SSL may have handed ciphertext to the socket before reporting
            // an incomplete record. Once a write is attempted, conservatively
            // retain uncertainty even if no plaintext progress was reported.
            if(method!="GET")result.state=PiPairClientResult::State::Uncertain;
            const auto wrote=channel.write(std::span(reinterpret_cast<const std::uint8_t *>(wire.data()+sent),wire.size()-sent));
            guard.reset();
            if(wrote.state==wtp::IoState::WouldBlock){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
            require(wrote.state==wtp::IoState::Progress&&wrote.count&&wrote.count<=wire.size()-sent);sent+=wrote.count;
        }
        require(sent==wire.size()&&!cancelled()&&Clock::now()<io_deadline);
        PiPairHttpResponseDecoder decoder(path);std::array<std::uint8_t,4096> bytes{};
        while(!cancelled()&&Clock::now()<io_deadline&&decoder.state()==PiPairHttpDecoder::State::More){
            const auto read=channel.read(bytes);
            if(read.state==wtp::IoState::WouldBlock){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
            require(read.state==wtp::IoState::Progress&&read.count&&read.count<=bytes.size());decoder.append(std::span(bytes.data(),read.count));
        }
        require(!cancelled()&&Clock::now()<io_deadline&&decoder.state()==PiPairHttpDecoder::State::Ready);
        result.reply=decoder.reply();result.principal=*channel.principal();result.state=PiPairClientResult::State::Replied;
    }catch(...){
        // A partially allocated response is not a completed authenticated
        // observation. Keep only the exact uncertainty boundary.
        result.reply.reset();result.principal.reset();
    }
    return result;
}
} // namespace wsprrypi
