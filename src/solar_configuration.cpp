// SPDX-License-Identifier: MIT
#include "solar_configuration.hpp"
#include "arg_parser.hpp"
#include "backend_capabilities.hpp"
#include "wtp_integration/controller_contract.hpp"
#include <sstream>
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool v){if(!v)throw std::invalid_argument("invalid_solar_configuration");}
SolarWsprList parse_list(const Json &v,const ArgParserConfig &ordinary){
    SolarWsprList result;result.frequencies=v.at("Frequency").get<std::string>();result.profile=v.at("Frequency Profile").get<std::string>();
    for(const auto &[key,value]:v.at("Band Preferences").items()){
        if(value.is_string())result.preferences[key]=value.get<std::string>();
        else result.preferences[key]=value.get<std::uint64_t>();
    }
    ArgParserConfig candidate=ordinary;candidate.mode=ModeType::WSPR;candidate.transmit=true;candidate.gpio_use_direct_pps_calibration=false;
    candidate.wspr.frequency_profile=result.profile;candidate.wspr.band_preferences=result.preferences;
    auto raw=result.frequencies;std::replace(raw.begin(),raw.end(),',',' ');std::istringstream stream(raw);std::string token;
    while(stream>>token){
        const auto at=token.find('@');int gpio=kSelectorGpioUnset;bool active_high=false;
        candidate.frequencies=token.substr(0,at);
        if(at!=std::string::npos){
            require(at>0&&token.find('@',at+1)==std::string::npos);
            auto suffix=token.substr(at+1);require(!suffix.empty());
            if(suffix.back()=='H'||suffix.back()=='h'||suffix.back()=='L'||suffix.back()=='l'){
                active_high=suffix.back()=='H'||suffix.back()=='h';suffix.pop_back();
            }
            std::size_t used=0;gpio=std::stoi(suffix,&used);require(used==suffix.size()&&gpio>=0&&gpio<=27);
        }
        require(set_frequencies(candidate)&&candidate.wspr_frequency_entries.size()==1);
        auto entry=candidate.wspr_frequency_entries.front();entry.selector_gpio=gpio;
        entry.selector_gpio_active_high=active_high;entry.allow_band_gpio_fallback=gpio==kSelectorGpioUnset;
        result.entries.push_back(std::move(entry));
    }
    require(!result.entries.empty());
    // Validate unused preference tokens as well, without silently dropping any.
    for(const auto &[band,value]:result.preferences){(void)value;candidate.frequencies=band;require(set_frequencies(candidate)&&candidate.wspr_frequency_entries.size()==1);}
    return result;
}
}
void validate_stored_solar(const Json &value){
    require(valid_pi_scheduling_shape(value));const auto &solar=value.at("solar");
    const auto &locator=solar.at("location").at("locator");
    require(locator.is_null()||solar_locator_center(locator.get<std::string>()).has_value());
    for(const auto *name:{"day","night","morning","evening"}){
        const auto &period=solar.at(name);
        if(period.contains("lower_mdeg"))require(SolarWindow{true,period.at("lower_mdeg").get<std::int32_t>(),period.at("upper_mdeg").get<std::int32_t>()}.valid());
        if(!period.at("cw_base_hz").is_null())require(solar_decimal_hz(period.at("cw_base_hz").get<std::string>()).has_value());
        if(!period.at("wspr_override").is_null())(void)parse_list(period.at("wspr_override"),ArgParserConfig{});
    }
}
SolarPolicyConfig parse_solar_configuration(const Json &value,const ArgParserConfig &ordinary,const Json *previous,bool validate_changes){
    validate_stored_solar(value);SolarPolicyConfig result;result.schedule_enabled=value.at("enabled").get<bool>();
    const auto &solar=value.at("solar");const auto &location=solar.at("location").at("locator");
    result.solar_enabled=solar.at("enabled").get<bool>();
    if(!location.is_null())result.locator=location.get<std::string>();
    for(const auto &[name,period]:std::initializer_list<std::pair<const char *,SolarPeriodPolicy *>>{{"day",&result.day},{"night",&result.night},{"morning",&result.morning},{"evening",&result.evening}}){
        const auto &source=solar.at(name);const auto old=previous?&previous->at("solar").at(name):nullptr;
        if(source.contains("allow"))period->allow=source.at("allow").get<bool>();
        if(source.contains("lower_mdeg")){
            auto &window=std::string_view(name)=="morning"?result.morning_window:result.evening_window;
            window={source.at("enabled").get<bool>(),source.at("lower_mdeg").get<std::int32_t>(),source.at("upper_mdeg").get<std::int32_t>()};
        }
        if(!source.at("wspr_override").is_null()){
            period->wspr_override=parse_list(source.at("wspr_override"),ordinary);
            if(validate_changes&&ordinary.transmit_backend==TransmitBackendKind::WTP&&(!old||source.at("wspr_override")!=old->at("wspr_override")))
                for(const auto &entry:period->wspr_override->entries)require(entry.selector_gpio==kSelectorGpioUnset);
            if(validate_changes&&(!old||source.at("wspr_override")!=old->at("wspr_override"))){
                for(const auto &entry:period->wspr_override->entries)if(entry.selector_gpio!=kSelectorGpioUnset){
                    require(has_ancillary_gpio());
                    require(!(ordinary.transmit_backend==TransmitBackendKind::RP1_GPCLK&&ordinary.gpio_use_direct_pps_calibration&&entry.selector_gpio==17));
                }
                const auto validate_list=[&](const SolarWsprList &list){
                    SolarModeIntent mode;mode.wspr=list;mode.wspr_audio_offset_hz=ordinary.wspr.audio_offset_hz;
                    const auto tones_allowed=[&](auto m,auto tones){
                        if(!solar_backend_frequencies(ordinary,m,tones))return false;
                        if(!ordinary.use_offset)return true;
                        for(const auto placement:{-80'000'000'000LL,80'000'000'000LL}){
                            std::vector<std::uint64_t> shifted(tones.begin(),tones.end());
                            for(auto &tone:shifted){
                                if(placement<0){if(tone<=static_cast<std::uint64_t>(-placement))return false;tone-=static_cast<std::uint64_t>(-placement);}
                                else {if(tone>UINT64_MAX-static_cast<std::uint64_t>(placement))return false;tone+=static_cast<std::uint64_t>(placement);}
                            }
                            if(!solar_backend_frequencies(ordinary,m,shifted))return false;
                        }
                        return true;
                    };
                    for(std::size_t index=0;index<list.entries.size();++index){
                        std::array<std::size_t,5> cursors{};cursors[0]=index;
                        const auto observed=evaluate_solar_policy({},mode,{},cursors,tones_allowed);
                        require(observed.outcome==SolarOutcome::Allowed||observed.outcome==SolarOutcome::Skipped);
                    }
                };
                validate_list(*period->wspr_override);
                // Unused preferences receive the same full tone checks.
                for(const auto &[band,pref]:period->wspr_override->preferences){
                    (void)pref;auto probe=source.at("wspr_override");probe["Frequency"]=band;
                    validate_list(parse_list(probe,ordinary));
                }
            }
        }
        if(!source.at("cw_base_hz").is_null()){
            period->cw_base_nhz=solar_decimal_hz(source.at("cw_base_hz").get<std::string>());require(period->cw_base_nhz.has_value());
            if(validate_changes&&(!old||source.at("cw_base_hz")!=old->at("cw_base_hz"))){
                SolarModeIntent intent;intent.mode=ordinary.mode==ModeType::FSKCW||ordinary.mode==ModeType::DFCW?ordinary.mode:ModeType::QRSS;intent.cw_base_nhz=*period->cw_base_nhz;
                const auto shift=std::round(ordinary.modulation_fsk_offset_hz*1e9L);require(std::isfinite(shift)&&shift>=static_cast<long double>(INT64_MIN)&&shift<9223372036854775808.0L);intent.cw_shift_nhz=static_cast<std::int64_t>(shift);
                require(evaluate_solar_policy({},intent,{}, {},[&](auto m,auto tones){return solar_backend_frequencies(ordinary,m,tones);}).outcome==SolarOutcome::Allowed);
            }
        }
    }
    return result;
}
}
