// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_service_lifetime.hpp"
#include "wtp_integration/controller_retirement.hpp"
#include "wtp_integration/browser_http.hpp"
#include <barrier>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>
#include <unistd.h>
using namespace wsprrypi;
namespace {
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("pair lifetime check "+std::to_string(checks));}
const std::string self(32,'a');
bool clean(PiPairHistory &history){return nlohmann::json::parse(history.snapshot().authority->bytes)["clean_shutdown"].get<bool>();}
}
int main(){try{
    char pattern[]="/tmp/pi-pair-lifetime-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary);
    const std::string root=temporary;
    struct Remove {std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
    PiPairAdmission admission;ManagementBrowserSessions sessions;
    {
        auto absent=std::make_shared<PiPairHistory>(root+"/absent",self);
        PiPairServiceLifetime lifetime(absent,admission,sessions);
        check(!lifetime.open()&&!lifetime.enter()&&!std::filesystem::exists(root+"/absent"));
        check(lifetime.open_for_recovery()&&!lifetime.ensure_history()&&!std::filesystem::exists(root+"/absent"));
        auto call=lifetime.enter();check(call&&!call->cancelled());
        {
            PiPairServiceLifetime duplicate(absent,admission,sessions);
            check(!duplicate.open_for_recovery()&&!duplicate.open());
        }
        check(!call->cancelled());
        httplib::Request request;request.method="POST";request.path="/api/control/v1/host/pairing/window";request.target=request.path;request.remote_addr="192.168.1.20";
        request.headers={{"Host","device.local"},{"Origin","http://device.local"},{"Content-Type","application/json"},{"X-WsprryPico-Request","1"}};
        request.body=nlohmann::json{{"contract","Wsprry-Control/1"},{"schema","pair-window"},{"operation_id",std::string(32,'e')},{"action","recover"},{"expected_authorization_generation","0"}}.dump();
        httplib::Response response;check(sessions.establish(request,response));const auto cookie=response.get_header_value("Set-Cookie");
        request.headers.emplace("Cookie",cookie.substr(0,cookie.find(';')));request.headers.emplace("X-WsprryPi-CSRF",response.get_header_value("X-WsprryPi-CSRF"));
        const auto principal=sessions.authorize_local_mutation(request,{true,"device.local",{},{{"192.168.1.10","255.255.255.0"}}});check(principal.has_value());
        absent->recover_access(*wtp_browser_http_request(request),*principal,sessions);
        check(lifetime.ensure_history()&&!clean(*absent)&&lifetime.ensure_history());
        call.reset();
        lifetime.close();lifetime.drain();
        check(clean(*absent)&&!lifetime.ensure_history());
    }
    std::shared_ptr<PiPairHistory> history;
    {
        ControllerRetirementBook book(root+"/retirement",root+"/assignments.json",self,nlohmann::json::array(),{},
            [&](PiPairInitializationProof proof){history=std::shared_ptr<PiPairHistory>(PiPairHistory::initialize(root+"/pairing",std::move(proof)));});
    }
    check(history&&clean(*history));
    {
        PiPairServiceLifetime lifetime(history,admission,sessions);
        check(!lifetime.enter()&&lifetime.open()&&!clean(*history)&&!lifetime.open());
        bool refused=false;try{lifetime.drain();}catch(const std::logic_error &){refused=true;}check(refused);
        auto call=lifetime.enter();check(call&&!call->cancelled());
        auto moved=std::move(*call);check(call->cancelled()&&!moved.cancelled());call.reset();
        auto connection=admission.try_outgoing();check(connection&&!connection->cancelled());
        const auto selected=history->snapshot().authority->signature;
        {
            PiPairServiceLifetime duplicate(history,admission,sessions);
            check(!duplicate.open()&&!duplicate.enter()&&history->snapshot().authority->signature==selected);
        }
        check(!connection->cancelled()&&!moved.cancelled());
        lifetime.close();check(moved.cancelled()&&connection->cancelled()&&!lifetime.enter()&&!clean(*history));
        std::barrier entered(2);std::promise<void> done;auto finished=done.get_future();
        std::thread owner([&]{entered.arrive_and_wait();try{lifetime.drain();done.set_value();}catch(...){done.set_exception(std::current_exception());}});
        entered.arrive_and_wait();
        // The call can report cancellation while the owner waits for it.
        // This used to deadlock if drain held the same gate needed by close.
        lifetime.close();check(!lifetime.ensure_history());
        const bool waiting=finished.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout;
        const bool still_dirty=!clean(*history);
        {auto last=std::move(moved);}
        owner.join();finished.get();check(waiting&&still_dirty&&clean(*history));
        lifetime.drain();check(clean(*history));
        check(lifetime.open()&&!clean(*history));
        auto resumed=lifetime.enter();check(resumed&&!resumed->cancelled());
        resumed.reset();lifetime.close();lifetime.drain();check(clean(*history));
    }
    history.reset();history=std::make_shared<PiPairHistory>(root+"/pairing",self);check(!history->unclean_start());
    {
        PiPairServiceLifetime abandoned(history,admission,sessions);check(abandoned.open());
        // Destruction cancels access but must never invent joined-worker proof.
    }
    check(!clean(*history));history.reset();
    history=std::make_shared<PiPairHistory>(root+"/pairing",self);check(history->unclean_start());
    std::cout<<"PASS service lifetime cancellation, HTTP drain before clean checkpoint, reopen and abandoned-owner evidence: "<<checks<<" checks\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
