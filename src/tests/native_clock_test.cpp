// SPDX-License-Identifier: MIT
#include "native_clock.hpp"
#include "native_calendar_time.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
int main(){try{
    using namespace wsprrypi;const auto check=[](bool value){if(!value)throw std::runtime_error("Native clock contract failed");};
    check(native_filetime_ns(116444736000000000ULL)==0);
    check(native_filetime_ns(116444736000000001ULL)==100);
    check(!native_filetime_ns(116444735999999999ULL));
    check(!native_filetime_ns(UINT64_MAX));check(!native_clock_scaled(UINT64_MAX,100));check(!native_clock_scaled(1,0));
    const std::time_t leap_day=951827696;std::tm calendar{},local{};
    check(native_calendar::utc(&leap_day,&calendar)==&calendar&&calendar.tm_year==100&&calendar.tm_mon==1&&calendar.tm_mday==29&&
        calendar.tm_hour==12&&calendar.tm_min==34&&calendar.tm_sec==56);
    check(native_calendar::local(&leap_day,&local)==&local&&std::mktime(&local)==leap_day);
    const auto before=read_native_clock(NativeClock::Monotonic),elapsed=read_native_clock(NativeClock::Elapsed),utc=read_native_clock(NativeClock::Utc);
    check(before&&elapsed&&utc);check(*utc>1735689600ULL*1000000000ULL&&*utc<4102444800ULL*1000000000ULL);
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    const auto after=read_native_clock(NativeClock::Monotonic),elapsed_after=read_native_clock(NativeClock::Elapsed);
    check(after&&elapsed_after&&*after>=*before&&*elapsed_after>=*elapsed);
    check(!read_native_clock(static_cast<NativeClock>(99)));
    std::cout<<"PASS: actual native UTC/monotonic/elapsed clocks, epoch/units, and overflow refusal; synchronization health is independent\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
