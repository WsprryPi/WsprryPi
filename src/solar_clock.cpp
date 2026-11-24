// SPDX-License-Identifier: MIT
#include "solar_clock.hpp"
#include "native_clock.hpp"
#include <limits>
#include <stdexcept>
#ifdef __linux__
#include <sys/timex.h>
#endif
namespace wsprrypi {
namespace {
constexpr std::uint64_t second=1'000'000'000ULL,limit=250'000'000ULL;
std::uint64_t difference(std::uint64_t a,std::uint64_t b){return a>b?a-b:b-a;}
}
SolarClockObservation SolarClockTracker::observe(const SolarClockSample &sample){
    bool discontinuity=false;
    if(previous_){
        discontinuity=sample.monotonic_ns<previous_->monotonic_ns||sample.elapsed_ns<previous_->elapsed_ns;
        if(!discontinuity){
            const auto monotonic_delta=sample.monotonic_ns-previous_->monotonic_ns;
            const auto elapsed_delta=sample.elapsed_ns-previous_->elapsed_ns;
            // Avoid signed conversion/overflow for an arbitrary wall-clock step.
            discontinuity=sample.utc_ns<previous_->utc_ns||
                difference(sample.utc_ns-previous_->utc_ns,monotonic_delta)>100'000'000ULL||
                difference(elapsed_delta,monotonic_delta)>100'000'000ULL;
        }
    }
    if(discontinuity||sample.alarm||!sample.provider_available||!sample.maximum_error_ns)healthy_.reset();
    SolarClockObservation result;
    result.utc_ns=sample.utc_ns;
    const bool date=sample.utc_ns>=1'735'689'600ULL*second&&sample.utc_ns<4'102'444'800ULL*second;
    const bool bounded=sample.maximum_error_ns&&*sample.maximum_error_ns<=limit;
    if(sample.provider_available&&!sample.alarm&&date&&bounded){
        if(sample.synchronized){healthy_=sample;result.state=SolarClockState::Synchronized;result.maximum_error_ns=*sample.maximum_error_ns;}
        else if(healthy_&&sample.elapsed_ns>=healthy_->elapsed_ns){
            const auto age=sample.elapsed_ns-healthy_->elapsed_ns;
            if(age<=3600ULL*second){
                // 50,000 ppb, rounded upward, without multiplication overflow.
                const auto growth=age/20'000ULL+(age%20'000ULL!=0);
                const auto error=*healthy_->maximum_error_ns+growth;
                // SolarClockObservation carries the last kernel error; the
                // shared readiness predicate adds holdover growth exactly once.
                if(error<=limit){result.state=SolarClockState::Holdover;result.maximum_error_ns=*healthy_->maximum_error_ns;result.holdover_age_ns=age;}
            }
        }
    }else if(sample.alarm)result.state=SolarClockState::Alarm;
    result.healthy_sample_this_boot=healthy_.has_value();
    if(discontinuity||result.state!=state_){
        if(generation_==std::numeric_limits<std::uint64_t>::max())throw std::runtime_error("solar_clock_generation_exhausted");
        ++generation_;
    }
    result.generation=generation_;state_=result.state;previous_=sample;return result;
}
SolarClockSample read_solar_clock_sample() noexcept {
    SolarClockSample sample;
    const auto before=read_native_clock(NativeClock::Monotonic);
    const auto utc=read_native_clock(NativeClock::Utc),monotonic=read_native_clock(NativeClock::Monotonic);
    const auto elapsed=read_native_clock(NativeClock::Elapsed);
#ifdef __linux__
    timex status{};const int state=adjtimex(&status); // modes=0: read only
    if(state>=0&&status.maxerror>=0&&static_cast<std::uint64_t>(status.maxerror)<=UINT64_MAX/1000ULL){
        sample.provider_available=true;
        sample.maximum_error_ns=static_cast<std::uint64_t>(status.maxerror)*1000ULL;
        // TIME_ERROR with STA_UNSYNC is ordinary synchronization loss, for
        // which bounded same-boot holdover is allowed. Leap/clock alarms are
        // distinct and must never be treated as that loss.
        sample.alarm=bool(status.status&(STA_INS|STA_DEL|STA_CLOCKERR))||
            state==TIME_INS||state==TIME_DEL||state==TIME_OOP||state==TIME_WAIT||
            (state==TIME_ERROR&&!(status.status&STA_UNSYNC));
        sample.synchronized=state==TIME_OK&&!(status.status&STA_UNSYNC);
    }
#else
    // No approved native OS health provider on this platform. Ordinary solar-off
    // recurrence remains unaffected; solar admission reports unavailable.
#endif
    const auto after=read_native_clock(NativeClock::Monotonic);
    if(!before||!after||!utc||!monotonic||!elapsed||*after<*before||*after-*before>100'000'000ULL){sample.provider_available=false;return sample;}
    // Account conservatively for acquisition skew as well as kernel error.
    if(sample.maximum_error_ns){
        const auto skew=*after-*before;
        if(*sample.maximum_error_ns>UINT64_MAX-skew)sample.maximum_error_ns.reset();
        else *sample.maximum_error_ns+=skew;
    }
    sample.utc_ns=*utc;sample.monotonic_ns=*monotonic;sample.elapsed_ns=*elapsed;return sample;
}
SolarClockObservation SolarClockProvider::observe(bool commitment){
    std::lock_guard lock(mutex_);
    const auto monotonic=read_native_clock(NativeClock::Monotonic);
    if(!commitment&&last_&&monotonic&&*monotonic>=last_->monotonic_ns&&*monotonic-last_->monotonic_ns<second)return observation_;
    auto sample=read_solar_clock_sample();observation_=tracker_.observe(sample);last_=sample;return observation_;
}
}
