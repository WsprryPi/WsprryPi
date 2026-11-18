// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_history.hpp"
#include "pi_pair_discovery.hpp"
#include "pi_pair_bootstrap_client.hpp"
#include "pi_pair_client.hpp"
#include "local_browser_principal.hpp"
#include "management_browser_session.hpp"
namespace wsprrypi {
class PiPairConnectRefusal final:public std::runtime_error {
public:
    enum class Reason {Capacity};
    explicit PiPairConnectRefusal(Reason reason):std::runtime_error("pairing_connect_refused"),reason_(reason){}
    Reason reason() const noexcept{return reason_;}
private:Reason reason_;
};
// Bounded synchronous worker body. The production lifetime owner admits and
// joins workers; construction neither starts networking nor exposes a route.
class PiPairConnect {
public:
    PiPairConnect(PiPairHistory &,ManagementBrowserSessions &,PiPairDiscovery &,PiPairAdmission &,std::string self,std::string boot);
    // Explicit software-fixture endpoint substitution, never an operator
    // option or runtime fallback. Both transports enforce actual loopback.
    struct LoopbackTest {PiPairEndpoint bootstrap,management;};
    PiPairConnect(PiPairHistory &,ManagementBrowserSessions &,PiPairDiscovery &,PiPairAdmission &,std::string self,std::string boot,LoopbackTest);
    PicoHttpResponse connect(const BrowserRequest &,const LocalBrowserPrincipal &,const std::function<bool()> &cancelled=[] {return false;});
    // Exact stored operation readback only. No public commit, PAKE or retry.
    std::optional<nlohmann::json> recover(const std::string &operation_id,const std::function<bool()> &cancelled=[] {return false;});
private:
    PiPairHistory &history_;
    ManagementBrowserSessions &sessions_;
    PiPairDiscovery &discovery_;
    PiPairAdmission &admission_;
    std::string self_,boot_;
    std::optional<LoopbackTest> test_;
    PiPairBootstrapClient bootstrap_;
    PiPairClient client_;
};
}
