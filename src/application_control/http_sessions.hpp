// SPDX-License-Identifier: MIT
#pragma once
#include "local_transport.hpp"
#include "http_domain.hpp"
#include <functional>
namespace wsprrypi {
class LocalHttpSessions {
public:
    using Dispatch=std::function<std::unique_ptr<BrowserHttpResource>(BrowserHttpInput)>;
    explicit LocalHttpSessions(Dispatch,std::function<bool()> admission);
    ~LocalHttpSessions();
    nlohmann::json request(const LocalRequest &);
    void disconnected(const std::string &) noexcept;
    LocalHttpSessions(const LocalHttpSessions &)=delete;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
nlohmann::json backend_http_request(const LocalRequest &);
void backend_http_disconnected(const std::string &) noexcept;
}
