// SPDX-License-Identifier: MIT
#include "pi_pair_discovery.hpp"
#include "management_resolver.hpp"
#include "controller_contract.hpp"
#include "control_contract.hpp"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <set>
#include <thread>
#include <stdexcept>
#if defined(__APPLE__)
#include <dns_sd.h>
#elif defined(__linux__) && defined(WSPRRYPI_HAVE_AVAHI)
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/strlst.h>
#endif
namespace wsprrypi {
namespace {
using Clock=std::chrono::steady_clock;
void require(bool value){if(!value)throw std::runtime_error("pair_publication_unavailable");}
struct Address {unsigned interface;int family;std::string text;auto operator<=>(const Address &) const = default;};
std::vector<Address> addresses(){
    ifaddrs *list{};require(!getifaddrs(&list));struct Release {ifaddrs *value;~Release(){freeifaddrs(value);}} release{list};
    std::set<Address> all;
    for(auto *entry=list;entry;entry=entry->ifa_next){
        if(!entry->ifa_addr||!entry->ifa_name||!(entry->ifa_flags&IFF_UP)||!(entry->ifa_flags&IFF_MULTICAST)||(entry->ifa_flags&IFF_LOOPBACK))continue;
        const auto family=entry->ifa_addr->sa_family;std::array<char,INET6_ADDRSTRLEN> text{};const void *raw{};
        if(family==AF_INET)raw=&reinterpret_cast<const sockaddr_in *>(entry->ifa_addr)->sin_addr;
        if(family==AF_INET6)raw=&reinterpret_cast<const sockaddr_in6 *>(entry->ifa_addr)->sin6_addr;
        if(!raw||!inet_ntop(family,raw,text.data(),text.size())||!private_management_address(text.data(),{}))continue;
        const auto index=if_nametoindex(entry->ifa_name);require(index);all.insert({index,family,text.data()});require(all.size()<=128);
    }
    // Publish one explicit address per family/interface. Every advertised
    // answer remains private; transport still validates all answers and pins
    // one. This does not add hidden connect/address fallback.
    std::vector<Address> result;std::set<std::pair<unsigned,int>> seen;
    for(const auto &address:all)if(seen.emplace(address.interface,address.family).second)result.push_back(address);
    return result;
}
}
struct PiPairDiscoveryPublisher::State {
    PiPairAnnouncement announcement;
    std::function<bool()> usable;
    std::string host;
    std::vector<std::string> fields;
    std::atomic_bool stopping{true},published{false};
    std::mutex lifecycle;
    std::thread worker;
    bool failed{};
    std::vector<Address> current;
    State(PiPairAnnouncement value,std::function<bool()> ready):announcement(std::move(value)),usable(std::move(ready)),
        host("wsprry-management-"+announcement.device_id+".local."),fields(pi_pair_discovery_txt(announcement.device_id,announcement.boot_id,announcement.http_port)){
        require(bool(usable)&&!announcement.name.empty()&&announcement.name.size()<=63);
        nlohmann::json row{{"discovery_id",announcement.device_id},{"device_id",announcement.device_id},{"boot_id",announcement.boot_id},{"name",announcement.name},
            {"host",host},{"bootstrap_port",announcement.http_port},{"management_port",31418},{"suite","SPAKE2+-P256-SHA256-HKDF-HMAC"},{"control_schema_digest",control_schema_sha256}};
        require(valid_controller_document({{"contract","Wsprry-Control/1"},{"schema","pair-discovery"},{"available",true},{"discoveries",nlohmann::json::array({row})}},"pair-discovery"));
    }
    bool live(){return !stopping.load()&&!failed&&usable();}
    std::vector<Address> advertised(){
        auto result=addresses();if(!announcement.bootstrap_ipv6)std::erase_if(result,[](const auto &address){return address.family==AF_INET6;});
        return result;
    }
#if defined(__APPLE__)
    struct Service {
        DNSServiceRef ref{};State *owner{};bool confirmed{};
        ~Service(){if(ref)DNSServiceRefDeallocate(ref);}
    };
    DNSServiceRef records{};
    std::vector<std::unique_ptr<Service>> services;
    std::vector<DNSRecordRef> record_refs;
    unsigned confirmed_records{};
    static void record_event(DNSServiceRef,DNSRecordRef,DNSServiceFlags,DNSServiceErrorType error,void *context) noexcept{
        auto &self=*static_cast<State *>(context);if(error)self.failed=true;else ++self.confirmed_records;
    }
    static void service_event(DNSServiceRef,DNSServiceFlags,DNSServiceErrorType error,const char *,const char *,const char *,void *context) noexcept{
        auto &service=*static_cast<Service *>(context);if(error)service.owner->failed=true;else service.confirmed=true;
    }
    void native(){
        require(DNSServiceCreateConnection(&records)==kDNSServiceErr_NoError);
        TXTRecordRef txt{};TXTRecordCreate(&txt,0,nullptr);struct Txt {TXTRecordRef &value;~Txt(){TXTRecordDeallocate(&value);}} release{txt};
        for(const auto &field:fields){const auto equal=field.find('=');require(equal!=std::string::npos);
            const auto name=field.substr(0,equal),value=field.substr(equal+1);require(TXTRecordSetValue(&txt,name.c_str(),value.size(),value.data())==kDNSServiceErr_NoError);}
        std::set<unsigned> interfaces;
        for(const auto &address:current){
            std::array<unsigned char,16> bytes{};require(inet_pton(address.family,address.text.c_str(),bytes.data())==1);DNSRecordRef record{};
            require(DNSServiceRegisterRecord(records,&record,kDNSServiceFlagsUnique,address.interface,host.c_str(),address.family==AF_INET?kDNSServiceType_A:kDNSServiceType_AAAA,
                kDNSServiceClass_IN,address.family==AF_INET?4:16,bytes.data(),120,record_event,this)==kDNSServiceErr_NoError);record_refs.push_back(record);
            interfaces.insert(address.interface);
        }
        for(const auto interface:interfaces){
            auto service=std::make_unique<Service>();service->owner=this;
            require(DNSServiceRegister(&service->ref,kDNSServiceFlagsNoAutoRename,interface,announcement.name.c_str(),pi_pair_discovery_type.data(),"local.",host.c_str(),htons(31418),
                TXTRecordGetLength(&txt),TXTRecordGetBytesPtr(&txt),service_event,service.get())==kDNSServiceErr_NoError);services.push_back(std::move(service));
        }
        auto checked=Clock::now();
        while(live()){
            std::vector<pollfd> sources;std::vector<DNSServiceRef> refs{records};for(const auto &service:services)refs.push_back(service->ref);
            for(const auto ref:refs){const auto fd=DNSServiceRefSockFD(ref);require(fd>=0);sources.push_back({fd,POLLIN,0});}
            if(poll(sources.data(),sources.size(),50)>0)for(std::size_t i=0;i<sources.size();++i)if(sources[i].revents&&DNSServiceProcessResult(refs[i]))failed=true;
            bool complete=confirmed_records==record_refs.size()&&!services.empty();for(const auto &service:services)complete=complete&&service->confirmed;
            published=complete&&!failed;
            if(Clock::now()-checked>=std::chrono::seconds(5)){if(advertised()!=current)break;checked=Clock::now();}
        }
    }
    void cleanup(){published=false;services.clear();record_refs.clear();if(records)DNSServiceRefDeallocate(records);records=nullptr;confirmed_records=0;}
#elif defined(__linux__) && defined(WSPRRYPI_HAVE_AVAHI)
    AvahiSimplePoll *poll{};AvahiClient *client{};AvahiEntryGroup *group{};
    static void group_event(AvahiEntryGroup *,AvahiEntryGroupState state,void *context) noexcept{
        auto &self=*static_cast<State *>(context);self.published=state==AVAHI_ENTRY_GROUP_ESTABLISHED;
        if(state==AVAHI_ENTRY_GROUP_COLLISION||state==AVAHI_ENTRY_GROUP_FAILURE)self.failed=true;
    }
    static void client_event(AvahiClient *client,AvahiClientState state,void *context) noexcept{
        auto &self=*static_cast<State *>(context);self.client=client;
        try{
            if(state==AVAHI_CLIENT_FAILURE){self.failed=true;return;}
            if(state==AVAHI_CLIENT_S_COLLISION||state==AVAHI_CLIENT_S_REGISTERING){self.published=false;if(self.group)avahi_entry_group_reset(self.group);return;}
            if(state!=AVAHI_CLIENT_S_RUNNING)return;
            if(!self.group)self.group=avahi_entry_group_new(client,group_event,&self);
            require(self.group);
            if(!avahi_entry_group_is_empty(self.group))return;
            std::set<unsigned> interfaces;
            for(const auto &address:self.current){AvahiAddress value{};
                require(avahi_address_parse(address.text.c_str(),address.family==AF_INET?AVAHI_PROTO_INET:AVAHI_PROTO_INET6,&value));
                require(avahi_entry_group_add_address(self.group,static_cast<AvahiIfIndex>(address.interface),AVAHI_PROTO_UNSPEC,AVAHI_PUBLISH_NO_REVERSE,self.host.c_str(),&value)>=0);
                interfaces.insert(address.interface);
            }
            AvahiStringList *txt{};struct Txt {AvahiStringList *&value;~Txt(){if(value)avahi_string_list_free(value);}} release{txt};
            for(const auto &field:self.fields){auto *next=avahi_string_list_add(txt,field.c_str());require(next);txt=next;}
            for(const auto interface:interfaces)require(avahi_entry_group_add_service_strlst(self.group,static_cast<AvahiIfIndex>(interface),AVAHI_PROTO_UNSPEC,
                static_cast<AvahiPublishFlags>(0),self.announcement.name.c_str(),pi_pair_discovery_type.data(),"local",self.host.c_str(),31418,txt)>=0);
            require(!interfaces.empty()&&avahi_entry_group_commit(self.group)>=0);
        }catch(...){self.failed=true;self.published=false;}
    }
    void native(){
        poll=avahi_simple_poll_new();require(poll);int error{};
        client=avahi_client_new(avahi_simple_poll_get(poll),AVAHI_CLIENT_NO_FAIL,client_event,this,&error);require(client);auto checked=Clock::now();
        while(live()){
            if(avahi_simple_poll_iterate(poll,50)<0)break;
            if(Clock::now()-checked>=std::chrono::seconds(5)){if(advertised()!=current)break;checked=Clock::now();}
        }
    }
    void cleanup(){published=false;if(group)avahi_entry_group_free(group);group=nullptr;if(client)avahi_client_free(client);client=nullptr;if(poll)avahi_simple_poll_free(poll);poll=nullptr;}
#else
    void native(){throw std::runtime_error("pair_publication_unavailable");}
    void cleanup(){published=false;}
#endif
    void run() noexcept{
        while(!stopping.load()){
            try{failed=false;current=advertised();if(!current.empty()&&usable())native();}catch(...){}
            cleanup();for(unsigned i=0;i<50&&!stopping.load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};
PiPairDiscoveryPublisher::PiPairDiscoveryPublisher(PiPairAnnouncement announcement,std::function<bool()> usable)
    :state_(std::make_unique<State>(std::move(announcement),std::move(usable))){}
PiPairDiscoveryPublisher::~PiPairDiscoveryPublisher(){stop();}
bool PiPairDiscoveryPublisher::start(){
    std::lock_guard guard(state_->lifecycle);if(state_->worker.joinable())return false;
    if(const auto disabled=std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");disabled&&std::string_view(disabled)=="1")return false;
    state_->stopping=false;try{state_->worker=std::thread([state=state_.get()]{state->run();});return true;}catch(...){state_->stopping=true;return false;}
}
void PiPairDiscoveryPublisher::stop() noexcept{state_->stopping=true;std::lock_guard guard(state_->lifecycle);state_->stopping=true;if(state_->worker.joinable())state_->worker.join();}
bool PiPairDiscoveryPublisher::published() const noexcept{return state_->published.load();}
} // namespace wsprrypi
