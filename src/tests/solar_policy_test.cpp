// SPDX-License-Identifier: MIT
#include "solar_policy.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace wsprrypi;
int main(){
    const auto exact=solar_decimal_hz("18446744073.709551615");assert(exact&&*exact==UINT64_MAX);
    for(const auto text:{"0","01","1.0","1.",".1","nan","inf","1e3","-1","1.0000000001","18446744073.709551616","18446744074"})assert(!solar_decimal_hz(text));
    assert(solar_decimal_hz("0.000000001")==1);
    SolarPolicyConfig policy;SolarModeIntent ordinary;
    ordinary.wspr.entries={{"20m",14095600,17,true,false},{"skip",0,-1,false,false}};
    ordinary.wspr.profile="wrc15";ordinary.wspr.preferences={{"60m",std::string("60m:legacy")}};
    std::array<std::size_t,5> cursors{};
    const auto check=[](ModeType,std::span<const std::uint64_t> tones){return !tones.empty()&&tones.front()>0;};
    SolarEvaluationContext context;
    auto result=evaluate_solar_policy(policy,ordinary,context,cursors,check);
    assert(result.outcome==SolarOutcome::Allowed&&!result.selected_period);assert(result.intent->wspr_entry->selector_gpio==17);
    assert(result.intent->wspr_profile==ordinary.wspr.profile&&result.intent->wspr_preferences==ordinary.wspr.preferences);
    assert(result.intent->nominal_tones_nhz.front()==14097097802734375ULL);
    cursors[0]=1;result=evaluate_solar_policy(policy,ordinary,context,cursors,check);assert(result.outcome==SolarOutcome::Skipped&&result.intent->cursor_index==1);assert(cursors[0]==1);
    policy.solar_enabled=true;assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).waiting==SolarWait::Configuration);
    context.configuration_generation=context.location_generation=1;context.intended_utc_ns=solar_first_utc_ns;
    context.clock={SolarClockState::Synchronized,solar_first_utc_ns,1,250000000,0,true};
    assert(solar_clock_ready(context.clock));assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).waiting==SolarWait::Location);
    policy.locator="EM18";context.clock.state=SolarClockState::Unavailable;
    assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).waiting==SolarWait::Clock);
    context.clock.state=SolarClockState::Holdover;context.clock.maximum_error_ns=70000000;context.clock.holdover_age_ns=3600000000000ULL;
    assert(solar_clock_ready(context.clock));++context.clock.holdover_age_ns;assert(!solar_clock_ready(context.clock));
    context.clock={SolarClockState::Synchronized,solar_first_utc_ns,1,1,0,true};context.location_source=SolarLocationSource::UnsupportedGps;
    assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).waiting==SolarWait::UnsupportedLocation);
    context.location_source=SolarLocationSource::Locator;
    policy.day.allow=policy.night.allow=false;
    result=evaluate_solar_policy(policy,ordinary,context,cursors,check);assert(result.outcome==SolarOutcome::Held&&result.waiting==SolarWait::Period&&!result.intent);
    policy.morning_window={true,-90000,90000};policy.evening_window=policy.morning_window;
    policy.morning.wspr_override=ordinary.wspr;policy.evening.wspr_override=ordinary.wspr;
    for(auto &cursor:cursors){cursor=0;}
    cursors[0]=1;
    result=evaluate_solar_policy(policy,ordinary,context,cursors,check);assert(result.outcome==SolarOutcome::Allowed&&result.intent->cursor_scope!=SolarCursorScope::Ordinary);
    policy.morning.wspr_override.reset();policy.evening.wspr_override.reset();
    result=evaluate_solar_policy(policy,ordinary,context,cursors,check);assert(result.outcome==SolarOutcome::Skipped&&result.intent->cursor_scope==SolarCursorScope::Ordinary);
    ordinary.mode=ModeType::FSKCW;ordinary.cw_base_nhz=100;ordinary.cw_shift_nhz=99;
    result=evaluate_solar_policy(policy,ordinary,context,cursors,check);assert(result.outcome==SolarOutcome::Allowed&&result.intent->nominal_tones_nhz[1]==199);
    ordinary.cw_shift_nhz=0;assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).waiting==SolarWait::Frequency);
    ordinary.cw_base_nhz=UINT64_MAX;ordinary.cw_shift_nhz=1;assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).waiting==SolarWait::Frequency);
    ordinary.cw_base_nhz=100;ordinary.cw_shift_nhz=10;unsigned checked=0;
    assert(evaluate_solar_policy(policy,ordinary,context,cursors,[&](ModeType,std::span<const std::uint64_t> values){++checked;assert(values.size()==2&&values[1]==110);return false;}).waiting==SolarWait::Frequency);assert(checked==1);
    policy.solar_enabled=false;context={};ordinary.mode=ModeType::QRSS;
    result=evaluate_solar_policy(policy,ordinary,context,cursors,check);assert(result.outcome==SolarOutcome::Allowed&&result.intent->nominal_tones_nhz.size()==1);
    // Clock bounds fail closed independently from RF frequency calibration.
    auto clock=context.clock;clock={SolarClockState::Synchronized,solar_first_utc_ns,1,250000001,0,true};assert(!solar_clock_ready(clock));
    clock.maximum_error_ns=0;clock.generation=0;assert(!solar_clock_ready(clock));clock.generation=1;
    clock.state=SolarClockState::Alarm;assert(!solar_clock_ready(clock));clock.state=SolarClockState::Holdover;
    clock.healthy_sample_this_boot=false;assert(!solar_clock_ready(clock));clock.healthy_sample_this_boot=true;clock.holdover_age_ns=UINT64_MAX;assert(!solar_clock_ready(clock));
    clock.holdover_age_ns=1;clock.maximum_error_ns=250000000;assert(!solar_clock_ready(clock));
    assert(evaluate_solar_policy(policy,ordinary,context,cursors,[](ModeType,std::span<const std::uint64_t>)->bool{throw std::runtime_error("refused");}).waiting==SolarWait::Frequency);
    const auto before=cursors;
    for(unsigned n=0;n<100;++n){assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).outcome==SolarOutcome::Allowed);}
    assert(cursors==before);
    ordinary.mode=ModeType::DFCW;ordinary.cw_shift_nhz=INT64_MIN;assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).waiting==SolarWait::Frequency);
    ordinary.mode=ModeType::TONE;assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).waiting==SolarWait::Mode);
    ordinary.mode=ModeType::WSPR;ordinary.wspr.entries={{"200MHz",200000000,-1,false,true}};
    assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).outcome==SolarOutcome::Allowed); // No imported Pico ceiling.
    ordinary.wspr.entries[0].dial_frequency_hz=std::numeric_limits<double>::infinity();assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).waiting==SolarWait::Frequency);
    ordinary.wspr.entries[0].dial_frequency_hz=-1;assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).waiting==SolarWait::Frequency);
    ordinary.wspr.entries[0].dial_frequency_hz=1e30;assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).waiting==SolarWait::Frequency);
    policy.schedule_enabled=false;assert(evaluate_solar_policy(policy,ordinary,context,cursors,check).outcome==SolarOutcome::Disabled);
    std::cout<<"PASS: disabled baseline, exact decimal/overflow, precedence, inheritance/scoped cursors, skips, CW tones, and clock/location holds\n";
}
