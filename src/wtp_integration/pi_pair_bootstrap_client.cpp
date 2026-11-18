// SPDX-License-Identifier: MIT
#include "pi_pair_bootstrap_client.hpp"
#include "pi_pair_contract.hpp"
#include <array>
#include <thread>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Clock=std::chrono::steady_clock;
void require(bool value){if(!value)throw std::runtime_error("pairing_bootstrap_unavailable");}
}
PiPairBootstrapClient::PiPairBootstrapClient(PiPairSocket::Access access):access_(access){}
PiPairBootstrapResult PiPairBootstrapClient::request(const PiPairEndpoint &endpoint,const std::string &resource,
    const nlohmann::json &body,PiPairAdmission::Connection &connection,PiPairSocket::Time attempt_deadline,
    const std::function<bool()> &external_cancel,const WriteAdmission &admission){
    PiPairBootstrapResult result;
    try{
        const bool get=resource=="window";const auto method=get?"GET":"POST";
        require(bool(external_cancel)&&pi_pair_route(PiPairRoutePurpose::Bootstrap,method,resource));
        const auto family=resource=="exchange"?"exchange-request":resource;
        require(get?body.is_null():valid_pi_pair_document(body,family));
        const auto whole=std::min(attempt_deadline,Clock::now()+std::chrono::seconds(24));
        const auto cancelled=[&]{return connection.cancelled()||external_cancel()||Clock::now()>=whole;};
        if(cancelled())return result;
        result.state=PiPairBootstrapResult::State::Unavailable;
        auto socket=PiPairSocket::open(endpoint,access_,whole,cancelled);
        const auto path="/api/pair/v1/"+resource;
        // Use the actual pinned local address for the inherited HTTP Host
        // guard, not an untrusted discovery alias or an administrator-supplied
        // hostname exemption. PAKE separately binds the expected device/boot.
        const auto host=(socket->address().find(':')==std::string::npos?socket->address():"["+socket->address()+"]")+":"+std::to_string(endpoint.port);
        const auto wire=pi_pair_http_request(method,path,host,get?"":body.dump());std::size_t sent=0;
        while(!cancelled()&&sent<wire.size()){
            auto guard=!sent&&admission?admission():std::optional<PiPairMutationGuard>{};
            if(!sent&&admission)require(guard.has_value());
            if(!get)result.state=PiPairBootstrapResult::State::Uncertain;
            const auto wrote=socket->write(std::span(reinterpret_cast<const std::uint8_t *>(wire.data()+sent),wire.size()-sent));
            guard.reset();
            if(wrote.state==wtp::IoState::WouldBlock){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
            require(wrote.state==wtp::IoState::Progress&&wrote.count&&wrote.count<=wire.size()-sent);sent+=wrote.count;
        }
        require(sent==wire.size()&&!cancelled());PiPairHttpResponseDecoder decoder(path);std::array<std::uint8_t,4096> bytes{};
        while(!cancelled()&&decoder.state()==PiPairHttpDecoder::State::More){
            const auto read=socket->read(bytes);
            if(read.state==wtp::IoState::WouldBlock){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
            require(read.state==wtp::IoState::Progress&&read.count&&read.count<=bytes.size());decoder.append(std::span(bytes.data(),read.count));
        }
        require(!cancelled()&&decoder.state()==PiPairHttpDecoder::State::Ready);const auto &reply=decoder.reply();
        const auto response=resource=="hello"?"challenge":resource=="exchange"?"exchange-response":resource=="confirm"?"confirmed":resource=="commit"?"committed":"window";
        (void)parse_pi_pair_document(reply.payload.body,reply.payload.status==200?response:"result");
        result.reply=reply;result.state=PiPairBootstrapResult::State::Replied;
    }catch(...){/* Never retry or reinterpret an uncertain commit here. */}
    return result;
}
} // namespace wsprrypi
