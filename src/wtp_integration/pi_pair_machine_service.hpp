// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_server.hpp"
#include "pi_pair_history.hpp"
#include "management_browser_session.hpp"
namespace wsprrypi {
// Authenticated management dispatcher shared by the production listener and
// source-pair fixtures. Fixed native/gateway callbacks receive the actual TLS
// principal; they retain their own final transaction/first-write admission.
class PiPairMachineService {
public:
    using Cancellation=std::function<bool()>;
    using Targets=std::function<PicoHttpResponse(const PiPairPrincipal &,const Cancellation &)>;
    using Target=std::function<PiPairHttpReply(const PiPairPrincipal &,const httplib::Request &,const Cancellation &)>;
    PiPairMachineService(PiPairHistory &,ManagementBrowserSessions &,std::string self,Targets,Target);
    PiPairHttpReply handle(const PiPairPrincipal &,const httplib::Request &,PiPairAdmission::Connection &,
        const std::function<bool()> &cancelled);
private:
    PiPairHistory &history_;
    ManagementBrowserSessions &sessions_;
    std::string self_;
    Targets targets_;
    Target target_;
};
}
