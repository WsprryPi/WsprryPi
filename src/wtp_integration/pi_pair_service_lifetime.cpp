// SPDX-License-Identifier: MIT
#include "pi_pair_service_lifetime.hpp"
#include <atomic>
#include <stdexcept>
#include <utility>
namespace wsprrypi {
struct PiPairServiceLifetime::State {
    std::mutex mutex;
    std::condition_variable drained;
    std::atomic_bool closed{true};
    std::size_t calls{};
    bool running{},opened{};
    std::shared_ptr<PiPairHistory> history;
};
PiPairServiceLifetime::PiPairServiceLifetime(std::shared_ptr<PiPairHistory> history,PiPairAdmission &admission,ManagementBrowserSessions &sessions)
    :history_(std::move(history)),admission_(admission),sessions_(sessions),state_(std::make_shared<State>()){
    if(!history_)throw std::invalid_argument("pairing_history_required");
    state_->history=history_;
}
PiPairServiceLifetime::~PiPairServiceLifetime(){
    // Only explicit owner-side drain after worker joins can attest clean exit.
    close();
}
bool PiPairServiceLifetime::open(){
    std::lock_guard lifecycle(lifecycle_);
    std::lock_guard state(state_->mutex);
    if(state_->opened||state_->calls||!state_->closed.load()||!history_->available()||!claim_history())return false;
    try{
        if(!history_->begin_service()){release_history();return false;}
        state_->running=true;state_->opened=true;state_->closed.store(false);return true;
    }catch(...){release_history();throw;}
}
bool PiPairServiceLifetime::claim_history(){
    std::lock_guard history(history_->mutex_);
    if(!history_->service_owner_.expired())return false;
    history_->service_owner_=state_;
    history_->service_closed_=std::shared_ptr<std::atomic_bool>(state_, &state_->closed);
    history_->service_bound_=true;return true;
}
void PiPairServiceLifetime::release_history(){
    std::lock_guard history(history_->mutex_);
    if(history_->service_owner_.lock().get()==state_.get())history_->service_owner_.reset();
}
bool PiPairServiceLifetime::open_for_recovery(){
    std::lock_guard lifecycle(lifecycle_);std::lock_guard state(state_->mutex);
    if(state_->opened||state_->calls||!state_->closed.load()||history_->available()||!claim_history())return false;
    state_->opened=true;state_->closed.store(false);return true;
}
bool PiPairServiceLifetime::ensure_history(){
    // An admitted HTTP call must be able to observe close while drain waits
    // for it; it cannot acquire the owner gate held by that drain.
    std::lock_guard state(state_->mutex);
    if(!state_->opened||state_->closed.load()||!history_->available())return false;
    if(state_->running)return true;
    if(!history_->begin_service())return false;
    state_->running=true;return true;
}
std::optional<PiPairServiceLifetime::Call> PiPairServiceLifetime::enter(){
    std::lock_guard state(state_->mutex);
    if(state_->closed.load()||!state_->opened)return {};
    ++state_->calls;return Call(state_);
}
void PiPairServiceLifetime::close() noexcept{
    // Hold this gate through cancellation so open cannot interleave between
    // closing the old lifetime and retiring its actual browser issuer.
    std::lock_guard state(state_->mutex);state_->closed.store(true);
    if(!state_->opened)return;
    admission_.cancel_connections();sessions_.retire();
}
void PiPairServiceLifetime::drain(){
    std::lock_guard lifecycle(lifecycle_);
    std::unique_lock state(state_->mutex);
    if(!state_->closed.load())throw std::logic_error("pairing_lifetime_not_closed");
    state_->drained.wait(state,[&]{return !state_->calls;});
    if(state_->running){history_->finish_service();state_->running=false;}
    state_->opened=false;release_history();
}
PiPairServiceLifetime::Call::Call(std::shared_ptr<State> state):state_(std::move(state)){}
PiPairServiceLifetime::Call::Call(Call &&other) noexcept:state_(std::move(other.state_)){}
PiPairServiceLifetime::Call::~Call(){
    if(!state_)return;
    std::lock_guard state(state_->mutex);
    if(!--state_->calls)state_->drained.notify_all();
}
bool PiPairServiceLifetime::Call::cancelled() const noexcept{return !state_||state_->closed.load();}
}
