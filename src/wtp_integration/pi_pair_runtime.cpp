// SPDX-License-Identifier: MIT
#include "pi_pair_runtime.hpp"
#include "privileged_network_runtime.hpp"
#include <condition_variable>
#include <thread>
#include <algorithm>
namespace wsprrypi {
struct PiPairRuntime::State {
    std::shared_ptr<PiPairOwner> owner;
    std::string boot;
    unsigned web_port;
    std::function<bool()> web_ready;
    PiPairMachineService::Targets targets;
    PiPairMachineService::Target target;
    bool loopback{};
    RemoteFactory remote_factory;
    Handover handover;
    std::mutex lifecycle,selection,wait_mutex;
    std::condition_variable wake;
    std::thread supervisor;
    std::atomic_bool stopping{true},usable{false};
    std::atomic_uint bound_port{0};
    std::shared_ptr<PiPairLocalService> local;
    std::shared_ptr<PiPairHistory> history;
    PiPairDiscovery discovery;
    std::unique_ptr<PiPairMachineService> machine;
    std::unique_ptr<PiPairServer> server;
    std::unique_ptr<PiPairDiscoveryBrowser> browser;
    std::unique_ptr<PiPairDiscoveryPublisher> publisher;
    State(std::shared_ptr<PiPairOwner> selected,std::string identity,unsigned port,std::function<bool()> web,
        PiPairMachineService::Targets list,PiPairMachineService::Target dispatch,bool fixture,RemoteFactory remote,Handover migration)
        :owner(std::move(selected)),boot(std::move(identity)),web_port(port),web_ready(std::move(web)),targets(std::move(list)),
        target(std::move(dispatch)),loopback(fixture),remote_factory(std::move(remote)),handover(std::move(migration)),discovery(owner?owner->self():std::string{}){
        if(!owner||boot.size()!=32||boot.find_first_not_of("0123456789abcdef")!=std::string::npos||
            !web_port||web_port>65535||!web_ready||!targets||!target)throw std::invalid_argument("pairing_runtime_owner_required");
    }
    bool allowed(){
        if(stopping.load())return false;
        const auto policy=privileged_network_runtime_state();
        return policy.active_known&&policy.active==PrivilegedNetworkMode::enforced&&policy.configured==PrivilegedNetworkMode::enforced&&web_ready();
    }
    std::shared_ptr<PiPairLocalService> selected(){std::lock_guard lock(selection);return local;}
    void cancel() noexcept{
        usable.store(false);bound_port.store(0);
        if(const auto service=selected())service->request_stop();
    }
    void close() noexcept{
        cancel();
        if(publisher)publisher->stop();
        if(browser)browser->stop();
        if(server)server->stop();
        std::shared_ptr<PiPairLocalService> service;
        {std::lock_guard lock(selection);service=std::move(local);}
        // Existing HTTP calls retain the owner and its Call lease. No new call
        // can select it; only the supervisor waits for them and marks clean.
        if(service)try{service->stop_and_drain();}catch(...){/* Preserve dirty/corrupt evidence. */}
        publisher.reset();browser.reset();server.reset();machine.reset();service.reset();history.reset();
    }
    bool open(){
        // No worker reference survives close. Reopen only this selected history;
        // absence/corruption remain holds until protected recovery, never defaults.
        (void)owner->reopen_history();history=owner->history();
        auto service=std::make_shared<PiPairLocalService>(history,management_browser_sessions(),pi_pair_admission(),discovery,owner->self(),boot,remote_factory?remote_factory(history):nullptr);
        if(!service->begin()){history.reset();return false;}
        machine=std::make_unique<PiPairMachineService>(*history,management_browser_sessions(),owner->self(),targets,target);
        server=std::make_unique<PiPairServer>(pi_pair_admission(),
            [this](const auto &identity){if(!allowed())return std::shared_ptr<const PiPairTlsCredential>{};return identity.purpose==PiPairTlsPurpose::Retirement?history->retirement_server_credential(identity):history->management_credential(identity);},
            [this](const auto &principal){return allowed()&&(principal.identity().purpose==PiPairTlsPurpose::Retirement?history->retirement_finished(principal,true):history->accept_finished(principal,&management_browser_sessions())!=PiPairHistory::ManagementAccess::Denied);},
            [this](const auto &principal,const auto &request,auto &connection,const auto &cancelled){return machine->handle(principal,request,connection,[&]{return cancelled()||!allowed();});},
            loopback?PiPairServer::Access::LoopbackTest:PiPairServer::Access::PrivateLan);
        {std::lock_guard lock(selection);local=std::move(service);}
        return true;
    }
    void run() noexcept{
        unsigned failures=0;auto retry=std::chrono::steady_clock::time_point{},maintenance_tick=retry,handover_tick=retry;
        while(!stopping.load()){
            try{
                if(!allowed()){
                    if(selected())close();
                    failures=0;retry={};
                }else{
                    const auto now=std::chrono::steady_clock::now();
                    if(!selected()&&now>=retry&&!open())throw std::runtime_error("pairing_owner_unavailable");
                    if(const auto service=selected()){
                        if(!server->ready()&&now>=retry){
                            if(!server->start())throw std::runtime_error("pairing_listener_unavailable");
                            if(!loopback){
                                browser=std::make_unique<PiPairDiscoveryBrowser>(discovery);(void)browser->start();
                                publisher=std::make_unique<PiPairDiscoveryPublisher>(PiPairAnnouncement{owner->self(),boot,"WsprryPi "+owner->self().substr(0,8),web_port,false},[this]{return usable.load()&&!stopping.load();});
                            }
                        }
                        if(server->ready()&&allowed()&&(!usable.load()||now>=maintenance_tick)){
                            if(!service->transport_ready()){close();throw std::runtime_error("pairing_accounting_unavailable");}
                            maintenance_tick=now+std::chrono::seconds(1);
                            usable.store(true);bound_port.store(server->port());failures=0;
                            if(publisher&&!publisher->published())(void)publisher->start();
                        }
                        if(handover&&usable.load()&&now>=handover_tick&&allowed()){
                            handover(*history,[this]{return !allowed();});
                            handover_tick=std::chrono::steady_clock::now()+std::chrono::seconds(1);
                        }
                    }
                }
            }catch(...){
                // A listener conflict leaves protected local status/removal
                // usable; no alternate port/carrier or identity is selected.
                usable.store(false);bound_port.store(0);failures=std::min(failures+1,6U);
                retry=std::chrono::steady_clock::now()+std::chrono::seconds(1U<<(failures-1));
                if(!selected())close();
            }
            std::unique_lock wait(wait_mutex);wake.wait_for(wait,std::chrono::milliseconds(100),[this]{return stopping.load();});
        }
        close();
    }
};
PiPairRuntime::PiPairRuntime(std::shared_ptr<PiPairOwner> owner,std::string boot,unsigned port,std::function<bool()> web,
    PiPairMachineService::Targets targets,PiPairMachineService::Target target,RemoteFactory remote,Handover handover)
    :state_(std::make_unique<State>(std::move(owner),std::move(boot),port,std::move(web),std::move(targets),std::move(target),false,std::move(remote),std::move(handover))){}
PiPairRuntime::PiPairRuntime(std::shared_ptr<PiPairOwner> owner,std::string boot,unsigned port,std::function<bool()> web,
    PiPairMachineService::Targets targets,PiPairMachineService::Target target,LoopbackTest,RemoteFactory remote,Handover handover)
    :state_(std::make_unique<State>(std::move(owner),std::move(boot),port,std::move(web),std::move(targets),std::move(target),true,std::move(remote),std::move(handover))){}
PiPairRuntime::~PiPairRuntime(){stop();}
bool PiPairRuntime::start(){
    std::lock_guard lock(state_->lifecycle);if(state_->supervisor.joinable())return false;
    state_->stopping.store(false);
    try{state_->supervisor=std::thread([this]{state_->run();});return true;}
    catch(...){state_->stopping.store(true);return false;}
}
void PiPairRuntime::request_stop() noexcept{state_->stopping.store(true);state_->cancel();state_->wake.notify_all();}
void PiPairRuntime::stop() noexcept{
    std::lock_guard lock(state_->lifecycle);request_stop();if(state_->supervisor.joinable())state_->supervisor.join();
}
std::shared_ptr<PiPairLocalService> PiPairRuntime::local_service(){return state_->selected();}
bool PiPairRuntime::ready() const noexcept{return state_->usable.load()&&!state_->stopping.load();}
unsigned PiPairRuntime::port() const noexcept{return ready()?state_->bound_port.load():0;}
}
