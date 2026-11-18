// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_admission.hpp"
#include <array>
#include <atomic>
#include <barrier>
#include <iostream>
#include <thread>
#include <vector>
using namespace wsprrypi;
namespace {
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("pair admission check "+std::to_string(checks));}
const std::string target(32,'a'),other(32,'b');
}
int main(){try{
    PiPairAdmission admission;
    std::vector<PiPairAdmission::Connection> incoming,outgoing;
    for(unsigned i=0;i<4;++i){auto a=admission.try_incoming(),b=admission.try_outgoing();check(a.has_value()&&b.has_value());incoming.push_back(std::move(*a));outgoing.push_back(std::move(*b));}
    check(!admission.try_incoming()&&!admission.try_outgoing());
    auto mutation=incoming[0].try_mutation(target);check(mutation.has_value());
    check(!outgoing[0].try_mutation(target)&&!incoming[0].try_mutation(other));
    auto second=outgoing[0].try_mutation(other);check(second.has_value());
    incoming[0].cancel();check(incoming[0].cancelled()&&!admission.try_incoming());
    incoming.erase(incoming.begin());check(!admission.try_incoming()); // Mutation still owns the actual slot.
    mutation.reset();check(admission.try_incoming().has_value());
    check(!outgoing[0].try_mutation(target));second.reset();check(outgoing[0].try_mutation(target).has_value());
    check(!outgoing[0].try_mutation("bad"));
    auto moved=std::move(outgoing[0]);check(outgoing[0].cancelled()&&!moved.cancelled());
    moved.cancel();check(moved.cancelled()&&!moved.try_mutation(target)&&!admission.try_outgoing());
    incoming.clear();outgoing.clear();
    // Concurrent admission holds all winners until every contender has tried.
    // A cancelled worker keeps capacity until it actually exits.
    for(unsigned round=0;round<8;++round){
        PiPairAdmission bounded;std::barrier gate(13);std::atomic_uint winners{0};std::vector<std::thread> workers;
        for(unsigned i=0;i<12;++i)workers.emplace_back([&]{
            gate.arrive_and_wait();auto slot=bounded.try_outgoing();if(slot){++winners;slot->cancel();}gate.arrive_and_wait();gate.arrive_and_wait();
        });
        gate.arrive_and_wait();gate.arrive_and_wait();check(winners==4&&!bounded.try_outgoing());gate.arrive_and_wait();
        for(auto &worker:workers)worker.join();
        check(bounded.try_outgoing().has_value());
    }
    {
        PiPairAdmission bounded;std::array<std::optional<PiPairAdmission::Connection>,4> slots;
        for(auto &slot:slots)slot=bounded.try_incoming();
        std::barrier gate(5);std::atomic_uint writers{0};std::vector<std::thread> workers;
        for(auto &slot:slots)workers.emplace_back([&slot,&gate,&writers]{
            gate.arrive_and_wait();auto mutation=slot->try_mutation(target);if(mutation)++writers;gate.arrive_and_wait();gate.arrive_and_wait();
        });
        gate.arrive_and_wait();gate.arrive_and_wait();check(writers==1);gate.arrive_and_wait();
        for(auto &worker:workers)worker.join();
        check(slots[0]->try_mutation(target).has_value());
        bounded.stop();for(auto &slot:slots)check(slot->cancelled()&&!slot->try_mutation(target));
        check(!bounded.try_incoming()&&!bounded.try_outgoing());slots={};check(!bounded.try_incoming());
    }
    std::optional<PiPairAdmission::Connection> surviving;
    {PiPairAdmission temporary;surviving=temporary.try_outgoing();check(surviving&&!surviving->cancelled());}
    check(surviving->cancelled()&&!surviving->try_mutation(target));surviving.reset();
    {
        PiPairAdmission restartable;
        std::array<std::optional<PiPairAdmission::Connection>,4> slots;
        for(auto &slot:slots)slot=restartable.try_outgoing();
        auto held=slots[0]->try_mutation(target);check(held.has_value());
        auto cancellation=slots[0]->cancellation();
        restartable.cancel_connections();
        for(auto &slot:slots)check(slot->cancelled()&&!slot->try_mutation(other));
        check(cancellation()&&!restartable.try_outgoing());
        slots={};check(!restartable.try_incoming()->try_mutation(target));
        // Both the mutation and the observer retain the old slot. Cancelling
        // must not free capacity behind a worker still observing its token.
        held.reset();
        std::vector<PiPairAdmission::Connection> fresh;
        for(unsigned i=0;i<3;++i){auto slot=restartable.try_outgoing();check(slot&&!slot->cancelled());fresh.push_back(std::move(*slot));}
        check(!restartable.try_outgoing());
        cancellation={};
        auto resumed=restartable.try_outgoing();check(resumed&&!resumed->cancelled()&&resumed->try_mutation(target));
        restartable.stop();restartable.cancel_connections();
        check(resumed->cancelled()&&!restartable.try_incoming()&&!restartable.try_outgoing());
    }
    admission.stop();check(!admission.try_incoming()&&!admission.try_outgoing()&&moved.cancelled());
    auto tick=std::chrono::steady_clock::time_point{};PiPairAdmission requests([&]{return tick;});
    for(unsigned i=0;i<20;++i)check(requests.allow_request("192.168.1.20"));
    check(!requests.allow_request("192.168.1.20")&&!requests.allow_request("::ffff:192.168.1.20"));
    tick+=std::chrono::milliseconds(100);check(requests.allow_request("192.168.1.20")&&!requests.allow_request("192.168.1.20"));
    tick+=std::chrono::seconds(2);for(unsigned i=0;i<20;++i)check(requests.allow_request("::ffff:192.168.1.20"));
    check(!requests.allow_request("192.168.1.20"));
    for(unsigned i=0;i<20;++i)check(requests.allow_request("fd00::1"));
    check(!requests.allow_request("fd00:0:0:0:0:0:0:1"));
    tick-=std::chrono::milliseconds(1);check(!requests.allow_request("fd00::1"));
    for(const auto &invalid:{std::string(),std::string("host.local"),std::string("192.168.1.20\0.extra",sizeof("192.168.1.20\0.extra")-1),std::string("fd00::1%zone")})
        check(!requests.allow_request(invalid));
    auto time=std::chrono::steady_clock::time_point{};PiPairAdmission capacity([&]{return time;});
    for(unsigned i=0;i<256;++i)check(capacity.allow_request("10.0.1."+std::to_string(i)));
    check(!capacity.allow_request("10.0.2.1"));time+=std::chrono::seconds(61);check(capacity.allow_request("10.0.2.1"));
    capacity.stop();check(!capacity.allow_request("10.0.2.1"));
    std::cout<<"PASS bounded shared pairing connections, cancellation and concurrent mutation admission: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
