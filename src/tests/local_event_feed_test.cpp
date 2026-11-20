// SPDX-License-Identifier: MIT
#include "application_control/event_feed.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace wsprrypi;
namespace {void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}}
int main(){try{
    EventFeed feed;
    check(feed.read(0).events.empty(),"Empty observation does not create events");
    check(feed.publish("one")&&feed.publish("two"),"Publish bounded observations");
    const auto first=feed.read(0,1);
    check(!first.resync_required&&first.sequence==1&&first.head==2&&first.events.size()==1&&first.events[0].message=="one",
        "Partial delivery advances only through returned event");
    check(feed.read(first.sequence).events[0].message=="two","Independent cursor retains later event");
    check(!feed.publish(std::string(EventFeed::maximum_message+1,'x')),"Oversize observation discarded");
    const auto gap=feed.read(2);
    check(gap.resync_required&&gap.events.empty()&&gap.sequence==3,"Loss requires authoritative snapshot");
    check(feed.publish("four")&&!feed.read(gap.sequence).resync_required,"Snapshot cursor resumes after loss");
    for(unsigned index=0;index<50;++index)check(feed.publish("later"),"Bounded ring publish");
    check(feed.read(4).resync_required,"Lagging reader cannot silently skip evicted events");
    const auto retained=feed.read(22,32);
    check(!retained.resync_required&&retained.events.size()==32&&retained.sequence==54,"Count bound retains newest events");
    check(feed.read(55).resync_required,"Future cursor is not silently accepted");
    EventFeed bytes;
    for(unsigned index=0;index<3;++index)check(bytes.publish(std::string(EventFeed::maximum_message,'a')),"Maximum event accepted");
    check(bytes.read(0).resync_required&&bytes.read(1).events.size()==2,"Aggregate bytes are bounded independently of count");
    EventFeed concurrent;
    std::thread left([&]{for(unsigned index=0;index<5000;++index)(void)concurrent.publish("left");});
    std::thread right([&]{for(unsigned index=0;index<5000;++index)(void)concurrent.publish("right");});
    for(unsigned index=0;index<1000;++index)(void)concurrent.read(0);
    left.join();right.join();
    const auto final=concurrent.read(0);
    check(final.head==10000&&final.resync_required&&final.events.empty(),"Concurrent loss is explicit and cursors never reorder or wrap");
    check(concurrent.publish("tail")&&concurrent.read(final.head).events[0].message=="tail","Fresh observation resumes independently of old consumers");
    std::cout<<"PASS: event cursor ordering, byte/count bounds, explicit gaps, independent consumers and nonwaiting concurrent producers\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
