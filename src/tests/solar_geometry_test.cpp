// SPDX-License-Identifier: MIT
#include "solar_geometry.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
using namespace wsprrypi;
int main(){
    const auto center=solar_locator_center("EM18");assert(center&&center->latitude_degrees==38.5&&center->longitude_degrees==-97);
    const auto precise=solar_locator_center("AA00AA00");assert(precise&&precise->latitude_degrees> -90&&precise->longitude_degrees> -180);
    assert(solar_locator_center("RR99XX99"));
    for(const auto text:{"","EM1","EM18I","EM18IG0","EM18IG00X","em18","SS00","EM18YY","EM18IG0A"})assert(!solar_locator_center(text));
    assert(!solar_position({NAN,0},solar_first_utc_ns));assert(!solar_position({0,INFINITY},solar_first_utc_ns));
    assert(!solar_position({91,0},solar_first_utc_ns));assert(!solar_position({0,-181},solar_first_utc_ns));
    assert(!solar_position(*center,solar_first_utc_ns-1));assert(solar_position(*center,solar_first_utc_ns));
    assert(solar_position(*center,solar_end_utc_ns-1));assert(!solar_position(*center,solar_end_utc_ns));
    assert(!solar_position(*center,std::numeric_limits<std::uint64_t>::max()));
    assert(solar_morning(-std::numbers::pi)&&solar_morning(std::numbers::pi));
    assert(!solar_morning(0)&&solar_morning(-1e-12)&&!solar_morning(1e-12));
    SolarWindow morning{true,-6000,6000},evening=morning;
    assert(morning.qualifies(-6)&&!morning.qualifies(6));assert(!morning.qualifies(std::nextafter(-6.,-INFINITY)));
    assert(morning.qualifies(std::nextafter(6.,-INFINITY)));assert(!morning.qualifies(NAN));
    SolarPosition p{-0.833,0,false,-90,90};assert(solar_period(p,{},{})->period==SolarPeriod::Day);
    p.elevation_degrees=std::nextafter(-0.833,-INFINITY);assert(solar_period(p,{},{})->period==SolarPeriod::Night);
    p.elevation_degrees=0;p.morning=true;p.hour_angle_radians=-0.5;assert(solar_period(p,morning,evening)->period==SolarPeriod::Morning);
    p.morning=false;p.hour_angle_radians=0.5;assert(solar_period(p,morning,evening)->period==SolarPeriod::Evening);
    p.elevation_degrees=p.daily_minimum_degrees=p.daily_maximum_degrees=0;assert(solar_period(p,morning,evening)->prolonged);
    morning.lower_mdeg=morning.upper_mdeg;assert(!solar_period(p,morning,evening));
    for(double latitude:{-90.,-67.,0.,38.5,67.,90.})for(double longitude:{-180.,-97.,0.,179.9,180.})
        for(std::uint64_t days:{0,59,79,171,265,355,365,1154,27393}){
            const auto utc=solar_first_utc_ns+days*86400ULL*1'000'000'000;
            if(utc>=solar_end_utc_ns)continue;
            const auto observed=solar_position({latitude,longitude},utc);assert(observed);assert(solar_period(*observed,{},{}));
        }
    std::cout<<"PASS: locator centers, exact bounds/precision, solar hour partitions, polar windows, date edges, and invalid inputs\n";
}
