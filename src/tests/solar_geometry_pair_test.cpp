// SPDX-License-Identifier: MIT
// Compare directly with the caller-selected production Pico geometry.
#include "solar_geometry.hpp"
#include "time/gps_calibration.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
int main(){
    using namespace std::chrono;
    unsigned count=0;
    for(int year:{2025,2028,2038,2096,2099})for(unsigned month:{1U,2U,3U,6U,9U,12U})
        for(unsigned day:{1U,28U,29U,31U})for(unsigned hour:{0U,6U,12U,18U,23U}){
            const year_month_day date{std::chrono::year{year},std::chrono::month{month},std::chrono::day{day}};
            if(!date.ok())continue;
            const auto utc=static_cast<std::uint64_t>(duration_cast<nanoseconds>(sys_days{date}.time_since_epoch()+hours{hour}+seconds{59}).count())+999999999;
            for(std::int32_t latitude:{-900000000,-800000000,-385000000,0,385000000,800000000,900000000})
                for(std::int32_t longitude:{-1800000000,-970000000,0,970000000,1800000000}){
                    const auto pico=wsprrypico::time::gps::solar_position({latitude,longitude,0},utc);
                    const auto pi=wsprrypi::solar_position({latitude/1e7,longitude/1e7},utc);
                    assert(pico&&pi);
                    assert(std::abs(pico->elevation_degrees-pi->elevation_degrees)<1e-10);
                    assert(std::abs(pico->daily_minimum_degrees-pi->daily_minimum_degrees)<1e-10);
                    assert(std::abs(pico->daily_maximum_degrees-pi->daily_maximum_degrees)<1e-10);
                    assert(pico->rising==pi->morning);++count;
                }
        }
    std::cout<<"PASS: "<<count<<" vectors against actual selected Pico solar geometry, including leap/date/longitude/polar branches\n";
}
