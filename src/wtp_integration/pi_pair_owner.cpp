// SPDX-License-Identifier: MIT
#include "pi_pair_owner.hpp"
#include "wtp_endpoint/runtime.hpp"
#include <stdexcept>
namespace wsprrypi {
namespace {
struct Slot {std::mutex mutex;std::weak_ptr<PiPairOwner> owner;};
Slot &slot(){static Slot value;return value;}
void require(bool value){if(!value)throw std::runtime_error("pairing_storage_unavailable");}
}
PiPairOwner::PiPairOwner(std::string directory,std::string self):directory_(std::move(directory)),self_(std::move(self)),
    history_(std::make_shared<PiPairHistory>(directory_,self_)){}
std::shared_ptr<PiPairHistory> PiPairOwner::history(){std::lock_guard guard(mutex_);return history_;}
bool PiPairOwner::reopen_history(){
    std::lock_guard guard(mutex_);
    if(!history_||history_.use_count()!=1)return false;
    return history_->reopen_service_history();
}
void PiPairOwner::provision(PiPairInitializationProof proof){
    std::lock_guard guard(mutex_);require(proof.valid()&&proof.device_id()==self_);
    if(history_&&history_->available())return;
    require(!history_||history_.use_count()==1);history_.reset();
    try{history_=std::shared_ptr<PiPairHistory>(PiPairHistory::initialize(directory_,std::move(proof)));}
    catch(...){history_=std::make_shared<PiPairHistory>(directory_,self_);throw;}
}
std::shared_ptr<PiPairOwner> prepare_pi_pair_owner(const std::string &self){
    auto &state=slot();std::lock_guard guard(state.mutex);
    if(const auto current=state.owner.lock()){require(current->self()==self);return current;}
    auto created=std::make_shared<PiPairOwner>(private_device_state_directory(PrivateDeviceDomain::Pairing,self),self);state.owner=created;return created;
}
std::shared_ptr<PiPairOwner> pi_pair_owner(){auto &state=slot();std::lock_guard guard(state.mutex);return state.owner.lock();}
}
