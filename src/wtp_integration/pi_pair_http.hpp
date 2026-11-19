// SPDX-License-Identifier: MIT
#pragma once
#include "httplib.hpp"
#include "network_http.hpp"
#include <span>
#include <cstdint>
namespace wsprrypi {
// One bounded HTTP/1.1 JSON exchange inside an already authenticated channel.
// Framing grants no authority. The dispatcher still requires the actual TLS
// principal and current owner/resource admission; no browser cookie is accepted.
class PiPairHttpDecoder {
public:
    enum class State {More,Ready,Refused};
    State append(std::span<const std::uint8_t>);
    State state() const noexcept{return state_;}
    const httplib::Request &request() const;
private:
    void header();
    std::string bytes_;
    httplib::Request request_;
    std::size_t boundary_{},length_{};
    bool header_ready_{};
    State state_{State::More};
};
// Fixed metadata only; never a general HTTP/USB forwarding envelope.
struct PiPairHttpReply {
    PicoHttpResponse payload;
    std::string location,recovery_resource;
    // Existing target-bound Pico metadata, with the same P2-03/P2-04 meaning.
    std::string context{},operation{},readback_revision{};
};
// Retains the bounded recovery metadata needed after an uncertain mutation.
// Redirects, cookies, compression and arbitrary forwarding headers are refused.
class PiPairHttpResponseDecoder {
public:
    using State=PiPairHttpDecoder::State;
    explicit PiPairHttpResponseDecoder(const std::string &request_path);
    State append(std::span<const std::uint8_t>);
    State state() const noexcept{return state_;}
    const PiPairHttpReply &reply() const;
private:
    void header();
    std::string bytes_;
    PiPairHttpReply reply_;
    std::size_t boundary_{},length_{},limit_{},depth_{};
    bool header_ready_{};
    State state_{State::More};
};
std::string pi_pair_http_request(const std::string &method,const std::string &path,
    const std::string &host,const std::string &body={},const httplib::Headers &metadata={});
std::string pi_pair_http_response(const PiPairHttpReply &);
} // namespace wsprrypi
