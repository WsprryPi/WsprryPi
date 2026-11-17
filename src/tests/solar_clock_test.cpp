// SPDX-License-Identifier: MIT
#include "solar_clock.hpp"
#include <cassert>
using namespace wsprrypi;
namespace {
constexpr std::uint64_t sec=1'000'000'000ULL;
SolarClockSample initial(){return {1'800'000'000ULL*sec,10*sec,10*sec,10'000'000ULL,true,false,true};}
void advance(SolarClockSample &sample,std::uint64_t ns){sample.utc_ns+=ns;sample.monotonic_ns+=ns;sample.elapsed_ns+=ns;}
}
int main(){
    SolarClockTracker clock;
    auto sample=initial();sample.synchronized=false;
    assert(!solar_clock_ready(clock.observe(sample))); // no cross-boot holdover
    sample.synchronized=true;auto ready=clock.observe(sample);assert(solar_clock_ready(ready));
    const auto generation=ready.generation;
    advance(sample,sec);assert(clock.observe(sample).generation==generation);
    sample.synchronized=false;auto holdover=clock.observe(sample);
    assert(holdover.state==SolarClockState::Holdover&&holdover.generation>generation);
    advance(sample,3600*sec);holdover=clock.observe(sample);
    assert(solar_clock_ready(holdover)&&holdover.maximum_error_ns==10'000'000ULL&&holdover.holdover_age_ns==3600*sec);
    advance(sample,1);assert(!solar_clock_ready(clock.observe(sample)));

    SolarClockTracker errors;sample=initial();sample.maximum_error_ns=250'000'000ULL;
    assert(solar_clock_ready(errors.observe(sample)));
    sample.synchronized=false;advance(sample,1);assert(!solar_clock_ready(errors.observe(sample)));
    sample.synchronized=true;sample.maximum_error_ns.reset();assert(!solar_clock_ready(errors.observe(sample)));
    sample=initial();sample.alarm=true;assert(!solar_clock_ready(errors.observe(sample)));
    sample.alarm=false;sample.synchronized=false;assert(!solar_clock_ready(errors.observe(sample)));
    sample=initial();assert(solar_clock_ready(errors.observe(sample)));
    sample.provider_available=false;assert(!solar_clock_ready(errors.observe(sample)));
    sample.provider_available=true;sample.synchronized=false;assert(!solar_clock_ready(errors.observe(sample)));

    for(bool backward:{false,true}){
        SolarClockTracker steps;sample=initial();ready=steps.observe(sample);
        advance(sample,sec);sample.utc_ns=backward?sample.utc_ns-200'000'000ULL:sample.utc_ns+200'000'000ULL;
        assert(steps.observe(sample).generation>ready.generation);
    }
    SolarClockTracker suspended;sample=initial();ready=suspended.observe(sample);
    sample.elapsed_ns+=100*sec;sample.utc_ns+=100*sec;sample.synchronized=false;
    auto resumed=suspended.observe(sample);assert(resumed.generation>ready.generation&&!solar_clock_ready(resumed));
    for(auto date:{1'735'689'600ULL*sec-1,4'102'444'800ULL*sec}){
        SolarClockTracker range;sample=initial();sample.utc_ns=date;assert(!solar_clock_ready(range.observe(sample)));
    }
    // Polling yields one cached observation even if the caller polls rapidly.
    SolarClockProvider provider;const auto first=provider.observe();
    for(unsigned n=0;n<1000;++n)assert(provider.observe().utc_ns==first.utc_ns);
}
