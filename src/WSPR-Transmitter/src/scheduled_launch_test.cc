// SPDX-License-Identifier: MIT
#include "scheduled_launch.hpp"
#include <cassert>
#include <stdexcept>
int main(){
    using namespace std::chrono;
    using wsprrypi::ScheduledLaunch;
    const auto start=ScheduledLaunch::Clock::time_point(seconds(1800000000));
    ScheduledLaunch ordinary;assert(!ordinary.bound()&&ordinary.current(start));
    unsigned calls=0;
    ScheduledLaunch bound{start,[&]{++calls;return true;}};
    assert(bound.bound()&&!bound.current(start-microseconds(1))&&!calls);
    assert(bound.current(start)&&calls==1);
    assert(bound.current(start+milliseconds(50))&&calls==2);
    assert(!bound.current(start+milliseconds(51))&&calls==2);
    bound.guard=[]{return false;};assert(!bound.current(start));
    bound.guard=[]()->bool{throw std::runtime_error("stale generation");};assert(!bound.current(start));
    // The bound start is immutable under refusal: no new window is invented.
    assert(bound.start==start);
}
