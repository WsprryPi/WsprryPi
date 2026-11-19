// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee C. Bussy
// Geometry ported from WsprryPico src/time/gps_calibration.cpp (MIT),
// ef5b80924bee9d3d483d8f2fcd0c7f52a3538ea8. NOAA fractional-year approximation;
// provenance: WsprryPico engineering/development/gray-line-scheduling-plan.md.
// Keep solar-center elevation unrounded; refraction is only the day threshold.
#include "solar_geometry.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
namespace wsprrypi {
bool SolarWindow::valid() const noexcept {
    return lower_mdeg>=-90000&&upper_mdeg<=90000&&lower_mdeg<upper_mdeg;
}
bool SolarWindow::qualifies(double elevation) const noexcept {
    return valid()&&enabled&&std::isfinite(elevation)&&elevation>=static_cast<double>(lower_mdeg)/1000&&elevation<static_cast<double>(upper_mdeg)/1000;
}
std::optional<SolarSite> solar_locator_center(std::string_view text) noexcept {
    if(text.size()!=4&&text.size()!=6&&text.size()!=8)return {};
    if(text[0]<'A'||text[0]>'R'||text[1]<'A'||text[1]>'R'||
       text[2]<'0'||text[2]>'9'||text[3]<'0'||text[3]>'9')return {};
    double longitude=-180+20*(text[0]-'A')+2*(text[2]-'0');
    double latitude=-90+10*(text[1]-'A')+(text[3]-'0'),width=2,height=1;
    if(text.size()>=6){
        if(text[4]<'A'||text[4]>'X'||text[5]<'A'||text[5]>'X')return {};
        width/=24;height/=24;longitude+=width*(text[4]-'A');latitude+=height*(text[5]-'A');
    }
    if(text.size()==8){
        if(text[6]<'0'||text[6]>'9'||text[7]<'0'||text[7]>'9')return {};
        width/=10;height/=10;longitude+=width*(text[6]-'0');latitude+=height*(text[7]-'0');
    }
    return SolarSite{latitude+height/2,longitude+width/2};
}
bool solar_morning(double hour_angle) noexcept {
    const auto angle=std::remainder(hour_angle,2*std::numbers::pi);
    // Noon belongs to evening; either representation of midnight to morning.
    return angle<0||std::abs(angle)==std::numbers::pi;
}
std::optional<SolarPosition> solar_position(SolarSite site,std::uint64_t utc) noexcept {
    if(!std::isfinite(site.latitude_degrees)||!std::isfinite(site.longitude_degrees)||
       std::abs(site.latitude_degrees)>90||std::abs(site.longitude_degrees)>180||
       utc<solar_first_utc_ns||utc>=solar_end_utc_ns)return {};
    using namespace std::chrono;
    constexpr std::uint64_t second=1'000'000'000;
    const sys_days date{days{utc/second/86400}};
    const year_month_day ymd{date};
    const auto day=(date-sys_days{ymd.year()/January/1}).count()+1;
    const auto minute=double(utc/second%86400)/60;
    constexpr double rad=std::numbers::pi/180;
    const double gamma=2*std::numbers::pi/(ymd.year().is_leap()?366:365)*(day-1+(minute/60-12)/24);
    const double equation=229.18*(0.000075+0.001868*std::cos(gamma)-0.032077*std::sin(gamma)-
        0.014615*std::cos(2*gamma)-0.040849*std::sin(2*gamma));
    const double declination=0.006918-0.399912*std::cos(gamma)+0.070257*std::sin(gamma)-
        0.006758*std::cos(2*gamma)+0.000907*std::sin(2*gamma)-0.002697*std::cos(3*gamma)+0.00148*std::sin(3*gamma);
    const double latitude=site.latitude_degrees*rad;
    const double hour_angle=((minute+equation+4*site.longitude_degrees)/4-180)*rad;
    const double center=std::sin(latitude)*std::sin(declination),span=std::abs(std::cos(latitude)*std::cos(declination));
    const auto degrees=[](double sine){return std::asin(std::clamp(sine,-1.0,1.0))/rad;};
    return SolarPosition{degrees(center+std::cos(latitude)*std::cos(declination)*std::cos(hour_angle)),
        std::remainder(hour_angle,2*std::numbers::pi),solar_morning(hour_angle),degrees(center-span),degrees(center+span)};
}
std::optional<SolarPeriodObservation> solar_period(const SolarPosition &position,
    const SolarWindow &morning,const SolarWindow &evening) noexcept {
    if(!morning.valid()||!evening.valid()||!std::isfinite(position.hour_angle_radians)||!std::isfinite(position.elevation_degrees)||
       position.morning!=solar_morning(position.hour_angle_radians)||
       !std::isfinite(position.daily_minimum_degrees)||!std::isfinite(position.daily_maximum_degrees)||
       position.daily_minimum_degrees < -90 || position.daily_maximum_degrees > 90 ||
       position.elevation_degrees<position.daily_minimum_degrees||position.elevation_degrees>position.daily_maximum_degrees)return {};
    const auto &window=position.morning?morning:evening;
    if(window.qualifies(position.elevation_degrees))return SolarPeriodObservation{
        position.morning?SolarPeriod::Morning:SolarPeriod::Evening,
        window.qualifies(position.daily_minimum_degrees)&&window.qualifies(position.daily_maximum_degrees)};
    const bool day=position.elevation_degrees>=-0.833;
    return SolarPeriodObservation{day?SolarPeriod::Day:SolarPeriod::Night,
        day?position.daily_minimum_degrees>=-0.833:position.daily_maximum_degrees< -0.833};
}
}
