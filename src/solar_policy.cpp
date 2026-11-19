// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee C. Bussy
#include "solar_policy.hpp"
#include <charconv>
#include <cmath>
#include <limits>
namespace wsprrypi {
namespace {
constexpr std::uint64_t second=1'000'000'000,max_error=250'000'000;
std::optional<std::uint64_t> nano_hz(long double hz){
    if(!std::isfinite(hz)||hz<=0)return {};
    const auto whole=std::floor(hz);
    if(whole>static_cast<long double>(UINT64_MAX/second))return {};
    const auto base=static_cast<std::uint64_t>(whole)*second;
    const auto fraction=static_cast<std::uint64_t>(std::floor((hz-whole)*second+0.5L));
    if(base>UINT64_MAX-fraction||base+fraction==0)return {};
    return base+fraction;
}
std::optional<std::uint64_t> shifted(std::uint64_t base,std::int64_t shift){
    if(shift>=0){if(base>UINT64_MAX-static_cast<std::uint64_t>(shift))return {};return base+static_cast<std::uint64_t>(shift);}
    const auto magnitude=static_cast<std::uint64_t>(-(shift+1))+1;
    if(base<=magnitude)return {};
    return base-magnitude;
}
}
SolarEvaluation evaluate_solar_occurrence(const SolarPolicyConfig &policy,const SolarModeIntent &ordinary,
    const SolarEvaluationContext &snapshot,std::span<const std::uint64_t> starts,
    const std::array<std::size_t,5> &cursors,const SolarFrequencyPredicate &allowed){
    const auto refusal=[&](SolarWait reason){SolarEvaluation result;result.observation=snapshot;result.waiting=reason;return result;};
    if(starts.empty()||starts.size()>2||(ordinary.mode!=ModeType::WSPR&&starts.size()!=1))return refusal(SolarWait::Configuration);
    for(std::size_t i=0;i<starts.size();++i)if(!starts[i]||(i&&starts[i]<=starts[i-1]))return refusal(SolarWait::Date);
    auto context=snapshot;context.intended_utc_ns=starts.front();
    auto first=evaluate_solar_policy(policy,ordinary,context,cursors,allowed);
    if(first.outcome!=SolarOutcome::Allowed&&first.outcome!=SolarOutcome::Skipped)return first;
    for(std::size_t i=1;i<starts.size();++i){
        context.intended_utc_ns=starts[i];const auto next=evaluate_solar_policy(policy,ordinary,context,cursors,allowed);
        if(next.outcome!=first.outcome||next.selected_period!=first.selected_period||!first.intent||!next.intent)return refusal(SolarWait::Period);
        const auto &a=*first.intent,&b=*next.intent;
        if(a.mode!=b.mode||a.cursor_scope!=b.cursor_scope||a.cursor_index!=b.cursor_index||a.wspr_profile!=b.wspr_profile||
            a.wspr_preferences!=b.wspr_preferences||a.nominal_tones_nhz!=b.nominal_tones_nhz||a.wspr_entry.has_value()!=b.wspr_entry.has_value())return refusal(SolarWait::Period);
        if(a.wspr_entry){
            const auto &x=*a.wspr_entry,&y=*b.wspr_entry;
            if(x.token!=y.token||x.dial_frequency_hz!=y.dial_frequency_hz||x.selector_gpio!=y.selector_gpio||
                x.selector_gpio_active_high!=y.selector_gpio_active_high||x.allow_band_gpio_fallback!=y.allow_band_gpio_fallback)return refusal(SolarWait::Period);
        }
    }
    return first;
}
std::optional<std::uint64_t> solar_decimal_hz(std::string_view text) noexcept {
    if(text.empty()||text.size()>30)return {};
    const auto dot=text.find('.');const auto integer=text.substr(0,dot);
    if(integer.empty()||(integer.size()>1&&integer.front()=='0')||integer.find_first_not_of("0123456789")!=std::string_view::npos)return {};
    std::uint64_t whole{};const auto parsed=std::from_chars(integer.data(),integer.data()+integer.size(),whole);
    if(parsed.ec!=std::errc{}||parsed.ptr!=integer.data()+integer.size()||whole>UINT64_MAX/second)return {};
    std::uint64_t fraction{};
    if(dot!=std::string_view::npos){
        const auto digits=text.substr(dot+1);
        if(digits.empty()||digits.size()>9||digits.back()=='0'||digits.find_first_not_of("0123456789")!=std::string_view::npos)return {};
        const auto result=std::from_chars(digits.data(),digits.data()+digits.size(),fraction);
        if(result.ec!=std::errc{}||result.ptr!=digits.data()+digits.size())return {};
        for(std::size_t n=digits.size();n<9;++n)fraction*=10;
    }
    const auto value=whole*second;if(value>UINT64_MAX-fraction||value+fraction==0)return {};
    return value+fraction;
}
bool solar_clock_ready(const SolarClockObservation &clock) noexcept {
    if(!clock.generation||clock.utc_ns<solar_first_utc_ns||clock.utc_ns>=solar_end_utc_ns||clock.maximum_error_ns>max_error)return false;
    if(clock.state==SolarClockState::Synchronized)return clock.holdover_age_ns==0;
    if(clock.state!=SolarClockState::Holdover||!clock.healthy_sample_this_boot||clock.holdover_age_ns>3600*second)return false;
    // Approved 50,000 ppb growth, rounded upward without overflow.
    const auto growth=clock.holdover_age_ns/20000+(clock.holdover_age_ns%20000!=0);
    return growth<=max_error-clock.maximum_error_ns;
}
std::optional<std::uint64_t> solar_frequency_nhz(long double hz) noexcept {return nano_hz(hz);}
SolarEvaluation evaluate_solar_policy(const SolarPolicyConfig &policy,const SolarModeIntent &ordinary,
    const SolarEvaluationContext &context,const std::array<std::size_t,5> &cursors,
    const SolarFrequencyPredicate &allowed){
    SolarEvaluation result;result.observation=context;
    const auto held=[&](SolarWait reason){result.outcome=SolarOutcome::Held;result.waiting=reason;result.intent.reset();return result;};
    if(!policy.morning_window.valid()||!policy.evening_window.valid()||
       (policy.locator&&!solar_locator_center(*policy.locator)))return held(SolarWait::Configuration);
    if(!policy.schedule_enabled){result.outcome=SolarOutcome::Disabled;result.waiting=SolarWait::None;return result;}
    const SolarPeriodPolicy *period=nullptr;
    SolarCursorScope explicit_scope=SolarCursorScope::Ordinary;
    if(policy.solar_enabled){
        if(!context.configuration_generation)return held(SolarWait::Configuration);
        if(context.intended_utc_ns<solar_first_utc_ns||context.intended_utc_ns>=solar_end_utc_ns)return held(SolarWait::Date);
        if(!solar_clock_ready(context.clock))return held(SolarWait::Clock);
        if(context.location_source!=SolarLocationSource::Locator)return held(SolarWait::UnsupportedLocation);
        if(!policy.locator||!context.location_generation)return held(SolarWait::Location);
        result.position=solar_position(*solar_locator_center(*policy.locator),context.intended_utc_ns);
        if(!result.position)return held(SolarWait::Date);
        const auto observation=solar_period(*result.position,policy.morning_window,policy.evening_window);
        if(!observation)return held(SolarWait::Configuration);
        result.selected_period=observation->period;result.prolonged=observation->prolonged;
        switch(observation->period){
            case SolarPeriod::Day:period=&policy.day;explicit_scope=SolarCursorScope::Day;break;
            case SolarPeriod::Night:period=&policy.night;explicit_scope=SolarCursorScope::Night;break;
            case SolarPeriod::Morning:period=&policy.morning;explicit_scope=SolarCursorScope::Morning;break;
            case SolarPeriod::Evening:period=&policy.evening;explicit_scope=SolarCursorScope::Evening;break;
        }
        // Gray-line selection is the override; its qualification itself permits
        // the window. Only ordinary day/night expose independent allow flags.
        if((observation->period==SolarPeriod::Day||observation->period==SolarPeriod::Night)&&!period->allow){
            return held(SolarWait::Period);
        }
    }
    SolarFrozenIntent intent{};intent.mode=ordinary.mode;
    if(ordinary.mode==ModeType::WSPR){
        const auto &list=period&&period->wspr_override?*period->wspr_override:ordinary.wspr;
        intent.wspr_profile=list.profile;intent.wspr_preferences=list.preferences;
        if(list.entries.empty())return held(SolarWait::Frequency);
        if(period&&period->wspr_override)intent.cursor_scope=explicit_scope;
        intent.cursor_index=cursors[static_cast<std::size_t>(intent.cursor_scope)]%list.entries.size();
        const auto &entry=list.entries[intent.cursor_index];intent.wspr_entry=entry;
        if(entry.dial_frequency_hz==0){result.outcome=SolarOutcome::Skipped;result.waiting=SolarWait::None;result.intent=intent;return result;}
        if(entry.dial_frequency_hz<0||!std::isfinite(entry.dial_frequency_hz)||!std::isfinite(ordinary.wspr_audio_offset_hz))return held(SolarWait::Frequency);
        for(unsigned tone=0;tone<4;++tone){
            const auto value=nano_hz(static_cast<long double>(entry.dial_frequency_hz)+ordinary.wspr_audio_offset_hz+(static_cast<long double>(tone)-1.5L)*1.46484375L);
            if(!value)return held(SolarWait::Frequency);
            intent.nominal_tones_nhz.push_back(*value);
        }
    }else if(ordinary.mode==ModeType::QRSS||ordinary.mode==ModeType::FSKCW||ordinary.mode==ModeType::DFCW){
        const auto base=period&&period->cw_base_nhz?*period->cw_base_nhz:ordinary.cw_base_nhz;
        if(!base)return held(SolarWait::Frequency);
        intent.nominal_tones_nhz.push_back(base);
        if(ordinary.mode!=ModeType::QRSS){
            // Saved Pi CW policy requires a positive mark/dash shift.
            if(ordinary.cw_shift_nhz<=0)return held(SolarWait::Frequency);
            const auto value=shifted(base,ordinary.cw_shift_nhz);if(!value)return held(SolarWait::Frequency);
            intent.nominal_tones_nhz.push_back(*value);
        }
    }else return held(SolarWait::Mode);
    try{if(!allowed||!allowed(ordinary.mode,intent.nominal_tones_nhz))return held(SolarWait::Frequency);}
    catch(...){return held(SolarWait::Frequency);}
    result.outcome=SolarOutcome::Allowed;result.waiting=SolarWait::None;result.intent=std::move(intent);return result;
}
}
