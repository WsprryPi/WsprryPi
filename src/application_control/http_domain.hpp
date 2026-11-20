// SPDX-License-Identifier: MIT
#pragma once
#include "httplib.hpp"
#include "json.hpp"
#include <memory>
namespace wsprrypi {
struct BrowserHttpInput {
    std::string operation,method,path,target,peer;
    httplib::Headers headers;
    std::string body;
    int transport_descriptor{-1}; // Verified local transport, never JSON.
};
class BrowserHttpResource {
public:
    explicit BrowserHttpResource(std::unique_ptr<httplib::Response>,std::uint64_t maximum=128ULL*1024*1024);
    nlohmann::json metadata() const;
    std::string read(std::uint64_t offset,std::size_t maximum);
    void complete(bool delivered);
private:
    std::unique_ptr<httplib::Response> response_;
    std::uint64_t size_{},contiguous_{};
    bool completed_{};
};
// Bounded in-process HTTP parsing for the backend and narrow maintenance
// domains. It creates no listener; each owner registers only its closed routes.
class ApplicationHttpRouter:public httplib::Server {
public:
    ApplicationHttpRouter();
    std::unique_ptr<BrowserHttpResource> dispatch(BrowserHttpInput);
protected:
    void initialize_domain_capture();
private:
    static thread_local std::unique_ptr<httplib::Response> *capture_;
};
// Runs the existing owning domain handlers without an HTTP listener. Browser
// framing and streaming belong to the web process; authority remains here.
std::unique_ptr<BrowserHttpResource> dispatch_backend_http(BrowserHttpInput);
void stop_backend_http_domain() noexcept;
}
