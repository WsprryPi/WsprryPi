// SPDX-License-Identifier: MIT
#include "pi_pair_discovery.hpp"
#include "pi_pair_discovery_avahi.hpp"
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <cstdlib>
#include <arpa/inet.h>
#include <poll.h>
#if defined(__APPLE__)
#include <dns_sd.h>
#elif defined(__linux__) && defined(WSPRRYPI_HAVE_AVAHI)
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/strlst.h>
#endif
namespace wsprrypi {
namespace {
using Clock=std::chrono::steady_clock;
[[maybe_unused]] void require(bool value){if(!value)throw std::runtime_error("pair_discovery_unavailable");}
bool local_domain(const std::string &value){return value=="local"||value=="local.";}
[[maybe_unused]] bool service_type(std::string value){for(auto &c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    if(!value.empty()&&value.back()=='.')value.pop_back();
    return value==pi_pair_discovery_type;}
}
struct PiPairDiscoveryBrowser::State {
    PiPairDiscovery &cache;
    std::atomic_bool stopping{true};
    std::thread worker;
    std::mutex lifecycle;
    bool failed{};
    struct Service {
        PiPairDiscovery::Key key;
        std::string domain;
        State *owner{};
        Clock::time_point attempted{};
        bool done{};
#if defined(__APPLE__)
        DNSServiceRef resolver{};
        ~Service(){if(resolver)DNSServiceRefDeallocate(resolver);}
#elif defined(__linux__) && defined(WSPRRYPI_HAVE_AVAHI)
        AvahiServiceResolver *resolver{};
        ~Service(){if(resolver)avahi_service_resolver_free(resolver);}
#endif
    };
    std::map<PiPairDiscovery::Key,std::unique_ptr<Service>> services;
    explicit State(PiPairDiscovery &value):cache(value){}
    void found(PiPairDiscovery::Key key,std::string domain,bool add){
        if(!key.interface_index||key.instance.empty()||key.instance.size()>63||!local_domain(domain))return;
        if(!add){services.erase(key);cache.removed(key);return;}
        if(services.contains(key))return;
        if(services.size()>=64)return;
        auto service=std::make_unique<Service>();service->key=std::move(key);service->domain=std::move(domain);service->owner=this;
        const auto identity=service->key;services.emplace(identity,std::move(service));
    }
#if defined(__APPLE__)
    DNSServiceRef browser{};
    static void browse(DNSServiceRef,DNSServiceFlags flags,std::uint32_t interface,DNSServiceErrorType error,
        const char *name,const char *type,const char *domain,void *context) noexcept{
        auto &self=*static_cast<State *>(context);
        try{require(!error&&name&&type&&domain&&service_type(type));
            self.found({interface,name,0},domain,(flags&kDNSServiceFlagsAdd)!=0);}
        catch(...){self.failed=true;}
    }
    static void resolved(DNSServiceRef,DNSServiceFlags,std::uint32_t interface,DNSServiceErrorType error,
        const char *,const char *host,std::uint16_t port,std::uint16_t size,const unsigned char *txt,void *context) noexcept{
        auto &service=*static_cast<Service *>(context);service.done=true;
        try{
            require(!error&&host&&interface==service.key.interface_index&&size<=2048&&(txt||!size));
            std::vector<std::string> fields;
            for(std::size_t offset=0;offset<size;){const auto length=txt[offset++];require(offset+length<=size&&fields.size()<7);
                fields.emplace_back(reinterpret_cast<const char *>(txt+offset),length);offset+=length;}
            if(!service.owner->cache.resolved(service.key,service.domain,host,ntohs(port),fields))service.owner->cache.removed(service.key);
        }catch(...){service.owner->cache.removed(service.key);}
    }
    void release(Service &service){if(service.resolver)DNSServiceRefDeallocate(service.resolver);service.resolver=nullptr;}
    void resolve(Service &service){
        service.attempted=Clock::now();service.done=false;
        const auto result=DNSServiceResolve(&service.resolver,0,service.key.interface_index,service.key.instance.c_str(),pi_pair_discovery_type.data(),service.domain.c_str(),resolved,&service);
        if(result){release(service);cache.removed(service.key);}
    }
    void native(){
        failed=false;require(DNSServiceBrowse(&browser,0,0,pi_pair_discovery_type.data(),"local.",browse,this)==kDNSServiceErr_NoError);
        cache.available(true);
        while(!stopping.load()&&!failed){
            refresh();const auto browser_fd=DNSServiceRefSockFD(browser);require(browser_fd>=0);
            std::vector<pollfd> sources{{browser_fd,POLLIN,0}};
            std::vector<DNSServiceRef> refs{browser};
            for(const auto &[key,service]:services){(void)key;if(service->resolver){const auto fd=DNSServiceRefSockFD(service->resolver);require(fd>=0);sources.push_back({fd,POLLIN,0});refs.push_back(service->resolver);}}
            const auto ready=poll(sources.data(),sources.size(),50);if(ready<0)continue;
            // Browse removal may destroy resolver refs. Process it alone and
            // rebuild the poll set before touching any captured resolver ref.
            if(sources[0].revents){if(DNSServiceProcessResult(browser))failed=true;continue;}
            for(std::size_t i=1;i<sources.size();++i)if(sources[i].revents){if(DNSServiceProcessResult(refs[i]))failed=true;}
        }
    }
    void cleanup(){services.clear();if(browser)DNSServiceRefDeallocate(browser);browser=nullptr;}
#elif defined(__linux__) && defined(WSPRRYPI_HAVE_AVAHI)
    AvahiSimplePoll *poll{};
    AvahiClient *client{};
    AvahiServiceBrowser *browser{};
    static void browse(AvahiServiceBrowser *,AvahiIfIndex interface,AvahiProtocol protocol,AvahiBrowserEvent event,
        const char *name,const char *type,const char *domain,AvahiLookupResultFlags flags,void *context) noexcept{
        auto &self=*static_cast<State *>(context);
        try{
            if(event==AVAHI_BROWSER_FAILURE){self.failed=true;return;}
            if(event!=AVAHI_BROWSER_NEW&&event!=AVAHI_BROWSER_REMOVE)return;
            require(interface>0&&name&&type&&domain&&service_type(type)&&!(flags&AVAHI_LOOKUP_RESULT_WIDE_AREA));
            self.found({static_cast<unsigned>(interface),name,static_cast<int>(protocol)},domain,event==AVAHI_BROWSER_NEW);
        }catch(...){self.failed=true;}
    }
    static void client_event(AvahiClient *client,AvahiClientState state,void *context) noexcept{
        auto &self=*static_cast<State *>(context);self.client=client;
        try{
            if(state==AVAHI_CLIENT_FAILURE){self.failed=true;return;}
            if(state!=AVAHI_CLIENT_S_RUNNING)return;
            if(!self.browser)self.browser=avahi_service_browser_new(client,AVAHI_IF_UNSPEC,AVAHI_PROTO_UNSPEC,
                pi_pair_discovery_type.data(),"local",static_cast<AvahiLookupFlags>(0),browse,&self);
            require(self.browser);self.cache.available(true);
        }catch(...){self.failed=true;}
    }
    static void resolved(AvahiServiceResolver *,AvahiIfIndex interface,AvahiProtocol protocol,AvahiResolverEvent event,const char *,
        const char *,const char *domain,const char *host,const AvahiAddress *,std::uint16_t port,AvahiStringList *txt,AvahiLookupResultFlags flags,void *context) noexcept{
        auto &service=*static_cast<Service *>(context);service.done=true;
        try{
            require(event==AVAHI_RESOLVER_FOUND&&interface>0&&static_cast<unsigned>(interface)==service.key.interface_index&&
                static_cast<int>(protocol)==service.key.protocol&&domain&&host&&!(flags&AVAHI_LOOKUP_RESULT_WIDE_AREA));
            const auto fields=pi_pair_discovery_avahi_fields(txt);
            if(!service.owner->cache.resolved(service.key,domain,host,port,fields))service.owner->cache.removed(service.key);
        }catch(...){service.owner->cache.removed(service.key);}
    }
    void release(Service &service){if(service.resolver)avahi_service_resolver_free(service.resolver);service.resolver=nullptr;}
    void resolve(Service &service){
        service.attempted=Clock::now();service.done=false;
        service.resolver=avahi_service_resolver_new(client,static_cast<AvahiIfIndex>(service.key.interface_index),static_cast<AvahiProtocol>(service.key.protocol),
            service.key.instance.c_str(),pi_pair_discovery_type.data(),service.domain.c_str(),AVAHI_PROTO_UNSPEC,static_cast<AvahiLookupFlags>(0),resolved,&service);
        if(!service.resolver)cache.removed(service.key);
    }
    void native(){
        failed=false;poll=avahi_simple_poll_new();require(poll);int error{};
        client=avahi_client_new(avahi_simple_poll_get(poll),AVAHI_CLIENT_NO_FAIL,client_event,this,&error);require(client);
        while(!stopping.load()&&!failed){refresh();if(avahi_simple_poll_iterate(poll,50)<0)break;}
    }
    void cleanup(){services.clear();if(browser)avahi_service_browser_free(browser);browser=nullptr;if(client)avahi_client_free(client);client=nullptr;if(poll)avahi_simple_poll_free(poll);poll=nullptr;}
#else
    void native(){throw std::runtime_error("pair_discovery_unavailable");}
    void cleanup(){services.clear();}
    void release(Service &){}
    void resolve(Service &){}
#endif
    void refresh(){
        const auto now=Clock::now();
        for(auto &[key,service]:services){(void)key;
#if defined(__APPLE__) || defined(WSPRRYPI_HAVE_AVAHI)
            if(service->resolver&&(service->done||now-service->attempted>=std::chrono::seconds(3))){if(!service->done)cache.removed(service->key);release(*service);}
            if(!service->resolver&&(service->attempted==Clock::time_point{}||now-service->attempted>=std::chrono::seconds(5)))resolve(*service);
#else
            (void)service;(void)now;
#endif
        }
    }
    void run() noexcept{
        while(!stopping.load()){
            try{cache.available(false);native();}catch(...){}
            cleanup();try{cache.available(false);}catch(...){}
            for(unsigned i=0;i<50&&!stopping.load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};
PiPairDiscoveryBrowser::PiPairDiscoveryBrowser(PiPairDiscovery &cache):state_(std::make_unique<State>(cache)){}
PiPairDiscoveryBrowser::~PiPairDiscoveryBrowser(){stop();}
bool PiPairDiscoveryBrowser::start(){
    std::lock_guard guard(state_->lifecycle);if(state_->worker.joinable())return false;
    if(const auto disabled=std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");disabled&&std::string_view(disabled)=="1"){
        state_->cache.available(false);return false;
    }
    state_->stopping=false;try{state_->worker=std::thread([state=state_.get()]{state->run();});return true;}catch(...){state_->stopping=true;return false;}
}
void PiPairDiscoveryBrowser::stop() noexcept{
    state_->stopping=true;std::lock_guard guard(state_->lifecycle);state_->stopping=true;
    if(state_->worker.joinable())state_->worker.join();
}
} // namespace wsprrypi
