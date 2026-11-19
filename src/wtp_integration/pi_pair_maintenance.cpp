// SPDX-License-Identifier: MIT
#include "pi_pair_maintenance.hpp"
#include "pi_pair_peers.hpp"
#include "privileged_network_runtime.hpp"
#include <algorithm>
#include <map>
#include <set>
namespace wsprrypi {
PiPairMaintenance::PiPairMaintenance(PiPairHistory &history,PiPairWindow &window,PiPairConnect &connect,PiPairRenewal &renewal,std::string self,Observe observe)
    :history_(history),window_(window),connect_(connect),renewal_(renewal),self_(std::move(self)),observe_(std::move(observe)){}
PiPairMaintenance::~PiPairMaintenance(){stop();}
bool PiPairMaintenance::start(){
    std::lock_guard lifecycle(lifecycle_);
    if(timer_.joinable()||outgoing_.joinable()||!history_.available())return false;
    stopping_.store(false);
    try{
        timer_=std::thread([this]{timer();});outgoing_=std::thread([this]{outgoing();});return true;
    }catch(...){
        stopping_.store(true);wake_.notify_all();
        if(timer_.joinable())timer_.join();
        if(outgoing_.joinable())outgoing_.join();
        window_.cancel();return false;
    }
}
void PiPairMaintenance::stop() noexcept{
    std::lock_guard lifecycle(lifecycle_);request_stop();window_.cancel();
    if(timer_.joinable())timer_.join();
    if(outgoing_.joinable())outgoing_.join();
}
void PiPairMaintenance::request_stop() noexcept{stopping_.store(true);wake_.notify_all();}
bool PiPairMaintenance::cancelled() const{
    if(stopping_.load())return true;
    const auto policy=privileged_network_runtime_state();
    return !policy.active_known||policy.active!=PrivilegedNetworkMode::enforced||policy.configured!=PrivilegedNetworkMode::enforced;
}
void PiPairMaintenance::wait(std::chrono::milliseconds duration){
    std::unique_lock wait(wait_mutex_);wake_.wait_for(wait,duration,[&]{return stopping_.load();});
}
void PiPairMaintenance::timer() noexcept{
    while(!stopping_.load()){
        try{if(cancelled())window_.cancel();else window_.advance();}catch(...){window_.cancel();}
        wait(std::chrono::milliseconds(100));
    }
}
std::vector<PiPairMaintenance::Task> PiPairMaintenance::tasks(){
    const auto state=history_.snapshot();if(!state.peers)throw std::runtime_error("pairing_storage_unavailable");
    const auto peers=parse_pi_pair_peers(state.peers->bytes,self_);std::vector<Task> result;
    for(const auto &row:peers["profiles"]){
        if(row["stage"]=="active"){
            result.push_back({row["profile_id"].get<std::string>(),true});
            if(observe_)result.push_back({row["profile_id"].get<std::string>(),false,true});
        }
        else if(row["stage"]=="awaiting_commit")result.push_back({row["initial_operation_id"].get<std::string>(),false});
    }
    for(const auto &row:peers["uncertain"])result.push_back({row["initial_operation_id"].get<std::string>(),false});
    // These are projections of the authoritative 64 profiles (renewal plus observation) and 32 recovery bounds, never a queue
    // of requests or a new persistent task ledger.
    if(result.size()>160)throw std::runtime_error("pairing_capacity");
    return result;
}
void PiPairMaintenance::outgoing() noexcept{
    std::map<std::string,Backoff> backoff;std::size_t cursor{};
    while(!stopping_.load()){
        try{
            if(cancelled()){wait(std::chrono::milliseconds(100));continue;}
            const auto work=tasks();std::set<std::string> live;
            for(const auto &task:work)live.insert((task.observation?"o:":task.renewal?"r:":"i:")+task.id);
            for(auto it=backoff.begin();it!=backoff.end();)if(!live.contains(it->first))it=backoff.erase(it);else ++it;
            for(std::size_t offset=0;offset<work.size();++offset){
                const auto index=(cursor+offset)%work.size();const auto &task=work[index];
                if(cancelled())break;
                auto &retry=backoff[(task.observation?"o:":task.renewal?"r:":"i:")+task.id];
                if(std::chrono::steady_clock::now()<retry.next)continue;
                bool ready=false;
                if(task.observation)ready=observe_(task.id,[this]{return cancelled();});
                else if(task.renewal)ready=renewal_.advance(task.id,[this]{return cancelled();})==PiPairRenewal::Result::Ready;
                else ready=connect_.recover(task.id,[this]{return cancelled();}).has_value();
                if(ready){retry.failures=0;retry.next=std::chrono::steady_clock::now()+std::chrono::seconds(task.observation?1:60);}
                else{
                    retry.failures=std::min(retry.failures+1,6U);
                    retry.next=std::chrono::steady_clock::now()+std::chrono::seconds(1U<<(retry.failures-1));
                }
                cursor=(index+1)%work.size();
                // At most one bounded exchange per selection pass. The next
                // pass advances other due targets instead of retrying this one.
                break;
            }
        }catch(...){wait(std::chrono::seconds(1));}
        wait(std::chrono::milliseconds(100));
    }
}
}
