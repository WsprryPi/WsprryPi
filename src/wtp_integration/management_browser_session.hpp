// SPDX-License-Identifier: MIT
#pragma once
#include "httplib.hpp"
#include <map>
#include <mutex>
namespace wsprrypi {
class ManagementBrowserSessions {
public:
    // Called only after the normal protected Host/Origin boundary and JSON
    // request-intent checks. Keys remain in process memory and an HttpOnly cookie.
    bool establish(const httplib::Request &,httplib::Response &);
    bool permits(const httplib::Request &);
    bool within_rate(const httplib::Request &);
private:
    struct Session {std::string host,token;std::uint64_t started{},seen{};};
    std::mutex mutex_;
    std::map<std::string,Session> sessions_;
    struct Rate {std::uint64_t seen{},credit{20000};};
    std::map<std::string,Rate> rates_;
};
} // namespace wsprrypi
