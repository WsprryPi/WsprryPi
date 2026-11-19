// SPDX-License-Identifier: MIT
#include "pi_pair_admission.hpp"
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <utility>
#include <algorithm>
#include <arpa/inet.h>
namespace wsprrypi {
struct PiPairAdmission::State {
    std::mutex mutex;
    bool stopped{};
    std::array<std::weak_ptr<Slot>,8> slots;
    std::map<std::string,std::weak_ptr<Slot>> mutations;
    struct Rate {std::chrono::steady_clock::time_point seen;unsigned credit{20000};};
    std::map<std::string,Rate> rates;
    Clock clock;
    explicit State(Clock value):clock(std::move(value)){}
};
struct PiPairAdmission::Slot {
    std::shared_ptr<State> state;
    std::atomic_bool cancelled{false};
    bool mutating{}; // Protected by State::mutex, not a worker-owned flag.
    explicit Slot(std::shared_ptr<State> value):state(std::move(value)){}
};
PiPairAdmission::PiPairAdmission(Clock clock):state_(std::make_shared<State>(std::move(clock))){}
PiPairAdmission::~PiPairAdmission(){stop();}
PiPairAdmission &pi_pair_admission(){static PiPairAdmission admission;return admission;}
std::optional<PiPairAdmission::Connection> PiPairAdmission::acquire(bool outgoing){
    std::lock_guard lock(state_->mutex);if(state_->stopped)return {};
    const unsigned first=outgoing?4:0;
    for(unsigned i=first;i<first+4;++i)if(state_->slots[i].expired()){
        auto slot=std::make_shared<Slot>(state_);state_->slots[i]=slot;return Connection(std::move(slot));
    }
    return {};
}
std::optional<PiPairAdmission::Connection> PiPairAdmission::try_incoming(){return acquire(false);}
std::optional<PiPairAdmission::Connection> PiPairAdmission::try_outgoing(){return acquire(true);}
bool PiPairAdmission::allow_request(const std::string &peer) noexcept {
    try{
        std::array<char,INET6_ADDRSTRLEN> text{};in_addr v4{};in6_addr v6{};std::string canonical;
        if(inet_pton(AF_INET,peer.c_str(),&v4)==1){if(!inet_ntop(AF_INET,&v4,text.data(),text.size()))return false;}
        else if(inet_pton(AF_INET6,peer.c_str(),&v6)==1){
            if(IN6_IS_ADDR_V4MAPPED(&v6)){
                std::copy_n(v6.s6_addr+12,4,reinterpret_cast<unsigned char *>(&v4));
                if(!inet_ntop(AF_INET,&v4,text.data(),text.size()))return false;
            }else if(!inet_ntop(AF_INET6,&v6,text.data(),text.size()))return false;
        }else return false;
        canonical=text.data();if(peer.find('\0')!=std::string::npos)return false;
        std::lock_guard lock(state_->mutex);if(state_->stopped||!state_->clock)return false;
        const auto current=state_->clock();
        for(auto entry=state_->rates.begin();entry!=state_->rates.end();){
            if(current>=entry->second.seen&&current-entry->second.seen>std::chrono::seconds(60))entry=state_->rates.erase(entry);
            else ++entry;
        }
        auto found=state_->rates.find(canonical);
        if(found==state_->rates.end()){
            if(state_->rates.size()>=256)return false;
            found=state_->rates.emplace(canonical,State::Rate{current}).first;
        }
        auto &rate=found->second;if(current<rate.seen)return false;
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(current-rate.seen).count();
        rate.credit=elapsed>=2000?20000:std::min(20000U,rate.credit+static_cast<unsigned>(elapsed)*10);
        rate.seen=current;if(rate.credit<1000)return false;rate.credit-=1000;return true;
    }catch(...){return false;}
}
void PiPairAdmission::cancel_connections() noexcept {
    std::lock_guard lock(state_->mutex);
    for(const auto &entry:state_->slots)if(const auto slot=entry.lock())slot->cancelled.store(true);
}
void PiPairAdmission::stop() noexcept {
    std::lock_guard lock(state_->mutex);state_->stopped=true;
    for(const auto &entry:state_->slots)if(const auto slot=entry.lock())slot->cancelled.store(true);
}
PiPairAdmission::Connection::Connection(std::shared_ptr<Slot> value):slot_(std::move(value)){}
bool PiPairAdmission::Connection::cancelled() const noexcept{return !slot_||slot_->cancelled.load();}
std::function<bool()> PiPairAdmission::Connection::cancellation() const{
    const auto slot=slot_;return [slot]{return !slot||slot->cancelled.load();};
}
void PiPairAdmission::Connection::cancel() noexcept{if(slot_)slot_->cancelled.store(true);}
std::optional<PiPairAdmission::Mutation> PiPairAdmission::Connection::try_mutation(const std::string &target){
    if(target.size()!=32||target.find_first_not_of("0123456789abcdef")!=std::string::npos||!slot_)return {};
    Mutation reservation(slot_,target);
    std::lock_guard lock(slot_->state->mutex);
    if(slot_->state->stopped||slot_->cancelled.load()||slot_->mutating||slot_->state->mutations.contains(target))return {};
    slot_->state->mutations.emplace(target,slot_);slot_->mutating=true;reservation.admitted_=true;return reservation;
}
PiPairAdmission::Mutation::Mutation(std::shared_ptr<Slot> slot,std::string target):slot_(std::move(slot)),target_(std::move(target)){}
PiPairAdmission::Mutation::~Mutation(){release();}
void PiPairAdmission::Mutation::release() noexcept {
    if(!slot_)return;
    if(admitted_){
        std::lock_guard lock(slot_->state->mutex);
        slot_->state->mutations.erase(target_);slot_->mutating=false;
    }
    slot_.reset();target_.clear();admitted_=false;
}
PiPairAdmission::Mutation::Mutation(Mutation &&other) noexcept:slot_(std::move(other.slot_)),target_(std::move(other.target_)),admitted_(std::exchange(other.admitted_,false)){}
PiPairAdmission::Mutation &PiPairAdmission::Mutation::operator=(Mutation &&other) noexcept {
    if(this!=&other){release();slot_=std::move(other.slot_);target_=std::move(other.target_);admitted_=std::exchange(other.admitted_,false);}return *this;
}
} // namespace wsprrypi
