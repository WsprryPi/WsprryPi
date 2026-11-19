// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_history.hpp"
#include "pi_pair_client.hpp"
#include "management_browser_session.hpp"
#include <map>
namespace wsprrypi {
// One bounded step of automatic maintenance, not a settings dispatcher or work
// queue. The lifetime owner schedules/backoffs steps and joins its workers.
class PiPairRenewal {
public:
    enum class Result {Ready,Pending,Held,Busy};
    PiPairRenewal(PiPairHistory &,ManagementBrowserSessions &,PiPairAdmission &,std::string self);
    struct LoopbackTest {PiPairEndpoint endpoint;};
    PiPairRenewal(PiPairHistory &,ManagementBrowserSessions &,PiPairAdmission &,std::string self,LoopbackTest);
    // Called only after strict context validation reports target-owned renewal
    // on this actual channel. Hints bind its exact pair/generation/epoch.
    bool target_requires(const std::string &profile,const PiPairPrincipal &,const nlohmann::json &context);
    Result advance(const std::string &profile,const std::function<bool()> &cancelled=[] {return false;});
private:
    struct Selection {
        std::string profile,generation,target,pair,authorization,current,pending,previous;
        PiPairEndpoint endpoint;
        std::vector<std::string> live;
    };
    struct Entry {std::mutex operation,hints;std::string signature,hint_epoch;bool commit_next{};};
    Selection selected(const std::string &);
    std::shared_ptr<Entry> entry(const Selection &);
    PiPairHistory &history_;
    ManagementBrowserSessions &sessions_;
    std::string self_;
    std::optional<LoopbackTest> test_;
    PiPairClient client_;
    std::mutex mutex_;
    std::map<std::string,std::shared_ptr<Entry>> entries_;
};
}
