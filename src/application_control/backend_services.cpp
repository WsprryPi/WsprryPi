// SPDX-License-Identifier: MIT
#include "backend_services.hpp"
#include "controller_runtime.hpp"
#include "pi_managed_host_runtime.hpp"
#include "web_server_routes.hpp"
#include "wtp_integration/pi_pair_runtime.hpp"
#include "wtp_integration/pi_pair_machine_backend.hpp"
#include "support_bundle_runtime.hpp"
#include "support_bundle_job_manager.hpp"
#include "web_server.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "wtp_integration/discovery.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
namespace wsprrypi {
namespace {
std::mutex drain_mutex;
std::function<void()> request_drain;
class BackendServices {
public:
    void start(unsigned port,bool fixture){
        std::lock_guard lock(lifecycle_);
        if(pairing_thread_.joinable())throw std::logic_error("backend_services_already_started");
        if(!port||port>65535)throw std::invalid_argument("backend_browser_port_required");
        {std::lock_guard selected(selection_);stopped_=false;}
        port_=port;loopback_fixture_=fixture;
        if(!fixture)start_wtp_discovery();
        start_pairing_supervisor();
    }
    void stop() noexcept{
        std::lock_guard lock(lifecycle_);
        {std::lock_guard selected(selection_);stopped_=true;}
        stop_pairing_supervisor();
        std::shared_ptr<SupportBundleJobManager> jobs;
        {std::lock_guard selected(selection_);jobs=std::move(jobs_);}
        if(jobs)jobs->shutdown();
    }
    std::shared_ptr<PiPairLocalService> pairing(){
        std::lock_guard lock(pairing_mutex_);
        return pairing_runtime_?pairing_runtime_->local_service():nullptr;
    }
    std::shared_ptr<SupportBundleJobManager> jobs(){
        std::lock_guard lock(selection_);
        if(stopped_)throw std::runtime_error("backend_services_stopping");
        if(!jobs_)jobs_=SupportBundleRuntime::create_production();
        return jobs_;
    }
    ~BackendServices(){stop();}
private:
    void start_pairing_supervisor();
    void stop_pairing_supervisor() noexcept;
    std::mutex lifecycle_,selection_,pairing_mutex_;
    std::shared_ptr<SupportBundleJobManager> jobs_;
    std::shared_ptr<PiPairRuntime> pairing_runtime_;
    std::thread pairing_thread_;
    std::atomic_bool pairing_stopping_{true};
    unsigned port_{};
    bool loopback_fixture_{};
    bool stopped_{};
};
void BackendServices::start_pairing_supervisor(){
    if(pairing_thread_.joinable())throw std::logic_error("pairing_supervisor_already_started");
    pairing_stopping_.store(false);
    pairing_thread_=std::thread([this]{
        while(!pairing_stopping_.load()){
            try{
                const auto owner=wsprrypi::pi_pair_owner();
                const auto boot=controller_only_mode()?controller_only_process_boot():wsprrypi::pi_managed_host_process_boot();
                if(owner&&!boot.empty()){
                    const auto backend=web_server_routes::pairing_machine_backend();
                    const std::function<bool()> web_ready=[this]{return !pairing_stopping_.load()&&
                        (controller_only_mode()?controller_only_status().value("controller_ready",false):!wsprrypi::pi_managed_host_held());};
                    const wsprrypi::PiPairMachineService::Targets targets=[backend](const auto &principal,const auto &cancelled){return backend->targets(principal,cancelled);};
                    const wsprrypi::PiPairMachineService::Target target=[backend](const auto &principal,const auto &request,const auto &cancelled){return backend->request(principal,request,cancelled);};
                    // The existing explicit web-fixture seam also isolates its
                    // management listener: loopback/ephemeral, no native DNS-SD.
                    const wsprrypi::PiPairRuntime::RemoteFactory remote=[](auto history){return web_server_routes::pairing_remote_controller(std::move(history));};
                    const wsprrypi::PiPairRuntime::Handover handover=[](auto &history,const auto &cancelled){
                        if(const auto host=wsprrypi::pi_managed_host()){
                            wsprrypi::PiPairRetirementClient client(history,wsprrypi::pi_pair_admission());
                            host->advance_remote_handover(client,cancelled);
                            if(!cancelled())host->reclaim_pairing_retirements(history);
                        }
                    };
                    auto runtime=loopback_fixture_?
                        std::make_shared<wsprrypi::PiPairRuntime>(owner,boot,static_cast<unsigned>(port_),web_ready,targets,target,wsprrypi::PiPairRuntime::LoopbackTest{},remote,handover):
                        std::make_shared<wsprrypi::PiPairRuntime>(owner,boot,static_cast<unsigned>(port_),web_ready,targets,target,remote,handover);
                    {
                        std::lock_guard lock(pairing_mutex_);
                        if(pairing_stopping_.load())break;
                        pairing_runtime_=runtime;
                        if(!runtime->start()){pairing_runtime_.reset();throw std::runtime_error("pairing_supervisor_unavailable");}
                    }
                    while(!pairing_stopping_.load())std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    runtime->stop();
                    break;
                }
            }catch(...){/* Pairing holds never select another role or output owner. */}
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}
void BackendServices::stop_pairing_supervisor() noexcept{
    pairing_stopping_.store(true);
    {std::lock_guard lock(pairing_mutex_);if(pairing_runtime_)pairing_runtime_->request_stop();}
    if(pairing_thread_.joinable())pairing_thread_.join();
    wtp_pi_withdraw_management_http(this);
    std::lock_guard lock(pairing_mutex_);pairing_runtime_.reset();
}

BackendServices &services(){static BackendServices value;return value;}
}
void start_backend_services(unsigned port,bool fixture){services().start(port,fixture);}
BackendRequestDrain::BackendRequestDrain(std::function<void()> callback){
    std::lock_guard lock(drain_mutex);
    if(request_drain)throw std::logic_error("backend_request_drain_already_selected");
    request_drain=std::move(callback);
}
BackendRequestDrain::~BackendRequestDrain(){std::lock_guard lock(drain_mutex);request_drain={};}
void drain_backend_requests() noexcept{
    try{std::lock_guard lock(drain_mutex);if(request_drain)request_drain();}catch(...){std::terminate();}
}
void stop_backend_services() noexcept{drain_backend_requests();services().stop();}
std::shared_ptr<PiPairLocalService> backend_pairing_service(){return services().pairing();}
std::shared_ptr<SupportBundleJobManager> backend_support_jobs(){return services().jobs();}
}
