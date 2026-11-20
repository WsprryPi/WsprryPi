// SPDX-License-Identifier: MIT
#include "event_feed.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace wsprrypi {
bool EventFeed::publish(const std::string &message) noexcept{
    // A producer never waits for a slow reader, socket, or persistent owner.
    // Saturation is a permanent resync boundary, never sequence wraparound.
    auto previous=sequence_.load();
    while(previous!=std::numeric_limits<std::uint64_t>::max()&&
        !sequence_.compare_exchange_weak(previous,previous+1)){}
    const auto sequence=previous==std::numeric_limits<std::uint64_t>::max()?previous:previous+1;
    const auto lost=[&]{auto before=gap_.load();while(before<sequence&&!gap_.compare_exchange_weak(before,sequence)){};return false;};
    if(previous==std::numeric_limits<std::uint64_t>::max()||message.size()>maximum_message)return lost();
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())return lost();
    try{
        // Concurrent producers may acquire the lock in the opposite order.
        // Keep order explicit, rather than delivering reordered status.
        if(!events_.empty()&&events_.back().sequence>=sequence)return lost();
        while(!events_.empty()&&(events_.size()>=maximum_events||bytes_+message.size()>maximum_bytes)){
            bytes_-=events_.front().message.size();events_.pop_front();
        }
        events_.push_back({sequence,message});bytes_+=message.size();return true;
    }catch(...){return lost();}
}
LocalEvents EventFeed::read(std::uint64_t after,std::size_t count) const{
    if(!count||count>maximum_events)throw std::invalid_argument("event_read_bound");
    std::lock_guard lock(mutex_);
    const auto current=sequence_.load();
    LocalEvents result{current,current,after>current||gap_.load()>after,{}};
    if(!events_.empty()&&after<events_.front().sequence-1)result.resync_required=true;
    if(result.resync_required)return result;
    for(const auto &event:events_)if(event.sequence>after){
        result.events.push_back(event);if(result.events.size()==count)break;
    }
    // The cursor advances only through events actually returned. An event
    // currently being published is picked up by a later read or gap signal.
    result.sequence=result.events.empty()?after:result.events.back().sequence;
    return result;
}
EventFeed &backend_events(){static EventFeed value;return value;}
}
