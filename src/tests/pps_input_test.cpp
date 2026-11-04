// SPDX-License-Identifier: MIT
#include "pps_input.hpp"
#include "bcm_pps_calibration.hpp"
#include "rp1_pps_calibration.hpp"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <unistd.h>
using namespace wsprrypi;
using Clock=std::chrono::steady_clock;
void require(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
struct Directory {
    std::filesystem::path path;
    Directory() { char name[]="/tmp/wsprrypi-pps-input-XXXXXX"; const auto* p=::mkdtemp(name); require(p,"temporary fixture");path=p; }
    ~Directory() { std::filesystem::remove_all(path); }
    void source(const char* device,const char* name) { auto dir=path/device;std::filesystem::create_directory(dir);std::ofstream(dir/"name")<<name<<'\n'; }
};
template<class M> struct Queue {
    std::mutex mutex;std::condition_variable changed;
    std::deque<M> samples;unsigned calls=0;bool closed=false;std::atomic<unsigned> destroyed{0};
    void close() {std::lock_guard lock(mutex);closed=true;changed.notify_all();}
    void await(unsigned target) {std::unique_lock lock(mutex);changed.wait(lock,[&]{return calls>=target;});}
};
template<class Base,class M> struct Capture:Base {
    std::shared_ptr<Queue<M>> queue;
    explicit Capture(std::shared_ptr<Queue<M>> q):queue(std::move(q)){}
    ~Capture() override {++queue->destroyed;}
    M measure(const std::atomic<bool>&) override {
        std::unique_lock lock(queue->mutex);++queue->calls;queue->changed.notify_all();
        queue->changed.wait(lock,[&]{return queue->closed||!queue->samples.empty();});
        if(queue->closed)throw std::runtime_error("closed input");
        auto m=queue->samples.front();queue->samples.pop_front();return m;
    }
};
struct Cleanup {std::function<void()> f;~Cleanup(){f();}};
template<class Provider,class Base,class M,class Maker>
void pin_change(const char* family,Maker make) {
    std::cerr<<family<<": configured GPIO routing and history retirement\n";
    auto old=std::make_shared<Queue<M>>(), next=std::make_shared<Queue<M>>();
    for(unsigned i=0;i<3;++i) old->samples.push_back(make(i));
    next->samples.push_back(make(4));
    std::atomic<unsigned> seen{99};
    Provider provider(std::function<std::unique_ptr<Base>(unsigned)>{[&](unsigned pin)->std::unique_ptr<Base>{
        seen=pin;require(pin==17||pin==18,"factory must receive exact configured GPIO");
        return std::make_unique<Capture<Base,M>>(pin==17?old:next);
    }});
    Cleanup cleanup{[&]{old->close();next->close();provider.snapshot(false);}};
    provider.snapshot(true,Clock::now(),17);old->await(4);
    require(provider.snapshot(true,Clock::now(),17).qualification==FrequencyEstimateQualification::Qualified,"old pin qualified before replacement");
    old->close();
    provider.snapshot(true,Clock::now(),18);next->await(2);
    auto state=provider.snapshot(true,Clock::now(),18);
    require(seen==18&&old->destroyed==1,"pin change closes old capture and routes new configured GPIO");
    require(state.retained_source_samples==1&&state.qualification==FrequencyEstimateQualification::Converging,"new pin cannot inherit qualified history");
    next->close();
    auto invalid=provider.snapshot(true,Clock::now(),28);
    require(seen==18&&!invalid.frequency_ppm&&invalid.qualification==FrequencyEstimateQualification::Unavailable,"invalid GPIO fails closed without factory or pin fallback");
}
int main(){try{
    Directory root;root.source("pps0","pps@11.-1");root.source("pps1","serial0");root.source("pps7","pps@12.-1");
    require(resolve_pps_input(17,root.path.string()).device_name=="pps0","default GPIO17 source");
    auto selected=resolve_pps_input(18,root.path.string());require(selected.device_name=="pps7","GPIO18 need not be pps0");
    bool refused=false;try{resolve_pps_input(19,root.path.string());}catch(...){refused=true;}require(refused,"missing input never falls back");
    root.source("pps8","pps@12.-1");refused=false;try{resolve_pps_input(18,root.path.string());}catch(...){refused=true;}require(refused,"ambiguous input refused");
    std::filesystem::remove_all(root.path/"pps8");std::filesystem::rename(root.path/"pps7",root.path/"old");root.source("pps7","pps@12.-1");
    require(resolve_pps_input(18,root.path.string()).identity!=selected.identity,"replacement source identity changes");
    pin_change<Rp1PpsCalibration,Rp1PpsCapture,Rp1PpsMeasurement>("RP1",[](unsigned){Rp1PpsMeasurement m;m.iterations=999999989;m.completed_at=Clock::now();m.started_at=m.completed_at-std::chrono::seconds(10);return m;});
    pin_change<BcmPpsCalibration,BcmPpsCapture,BcmPpsMeasurement>("BCM",[](unsigned i){BcmPpsMeasurement m;m.first={10000000+i*121000000ULL,10000002+i*121000000ULL};m.last={m.first.low_us+120000000,m.first.high_us+120000000};m.nominal_timer_hz=1000000;m.processor=LegacyGpioProcessorProfile::Bcm2711;m.clock_identity="test";m.completed_at=Clock::now();return m;});
    std::cout<<"Configurable PPS source discovery, ambiguity, replacement, both-family pin changes and history retirement passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
