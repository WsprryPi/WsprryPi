// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_network.hpp"
#include "pi_pair_discovery.hpp"
#include <windns.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <deque>
#include <limits>
#include <set>
#include <thread>
namespace wsprrypi::windows_dnssd {
inline void require(bool value){if(!value)throw std::runtime_error("pair_discovery_unavailable");}
inline std::wstring bounded(const wchar_t *value,std::size_t maximum=1024){require(value);const auto length=::wcsnlen_s(value,maximum+1);require(length>0&&length<=maximum);return {value,length};}
inline std::string utf8(const std::wstring &value){
    require(value.size()<=2048);const auto length=::WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);require(length>0&&length<=8192);
    std::string result(static_cast<std::size_t>(length),'\0');require(::WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),length,nullptr,nullptr)==length);return result;
}
inline std::wstring folded(std::wstring value){for(auto &c:value)if(c>=L'A'&&c<=L'Z')c=static_cast<wchar_t>(c-L'A'+L'a');if(!value.empty()&&value.back()==L'.')value.pop_back();return value;}
inline constexpr wchar_t query[]=L"_wsprry-mgmt._tcp.local";
inline std::string instance(const std::wstring &full){
    const auto lower=folded(full),suffix=L"."+std::wstring(query);require(lower.size()>suffix.size()&&lower.ends_with(suffix));
    const auto label=full.substr(0,lower.size()-suffix.size());std::string result;
    for(std::size_t offset=0;offset<label.size();++offset){
        auto value=label[offset];require(value!=L'.'&&value!=0);
        if(value==L'\\'){
            require(++offset<label.size());value=label[offset];
            if(value>=L'0'&&value<=L'9'){
                require(offset+2<label.size()&&label[offset+1]>=L'0'&&label[offset+1]<=L'9'&&label[offset+2]>=L'0'&&label[offset+2]<=L'9');
                const auto byte=(value-L'0')*100+(label[offset+1]-L'0')*10+label[offset+2]-L'0';require(byte>0&&byte<=255);result+=static_cast<char>(byte);offset+=2;continue;
            }
        }
        std::wstring character(1,value);
        if(value>=0xd800&&value<=0xdbff){require(offset+1<label.size()&&label[offset+1]>=0xdc00&&label[offset+1]<=0xdfff);character+=label[++offset];}
        result+=utf8(character);
    }
    require(!result.empty()&&result.size()<=63&&::MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,result.data(),static_cast<int>(result.size()),nullptr,0)>0);return result;
}
struct Row {PiPairDiscovery::Key key;std::string host;unsigned port{};std::vector<std::string> fields;bool removed{};};
inline Row resolved_instance(const DNS_SERVICE_INSTANCE &value,const PiPairDiscovery::Key &key){
    require(value.dwInterfaceIndex==key.interface_index&&instance(bounded(value.pszInstanceName))==key.instance&&value.wPort==31418&&value.dwPropertyCount==7&&value.keys&&value.values);
    Row row{key,utf8(bounded(value.pszHostName,253)),value.wPort,{},false};std::size_t total{};
    for(DWORD index=0;index<value.dwPropertyCount;++index){auto field=utf8(bounded(value.keys[index],255))+"="+utf8(bounded(value.values[index],255));total+=field.size();require(total<=2048);row.fields.push_back(std::move(field));}return row;
}
struct Api {
    decltype(&windows_discovery_addresses) addresses=&windows_discovery_addresses;
    decltype(&::DnsServiceRegister) register_service=&::DnsServiceRegister;
    decltype(&::DnsServiceRegisterCancel) register_cancel=&::DnsServiceRegisterCancel;
    decltype(&::DnsServiceDeRegister) deregister=&::DnsServiceDeRegister;
    decltype(&::DnsServiceBrowse) browse=&::DnsServiceBrowse;
    decltype(&::DnsServiceBrowseCancel) browse_cancel=&::DnsServiceBrowseCancel;
    decltype(&::DnsServiceResolve) resolve=&::DnsServiceResolve;
    decltype(&::DnsServiceResolveCancel) resolve_cancel=&::DnsServiceResolveCancel;
};
struct Event {enum class Kind {Found,Resolved};Kind kind;PiPairDiscovery::Key key;std::wstring full;bool removed{};std::uint64_t revision{};Row row;};
struct Mailbox {
    std::mutex mutex;std::deque<Event> events;bool failed{};
    void push(Event event){std::lock_guard lock(mutex);if(events.size()>=256){failed=true;events.clear();return;}events.push_back(std::move(event));}
    void fail(){std::lock_guard lock(mutex);failed=true;events.clear();}
};
struct Context {virtual ~Context()=default;std::weak_ptr<Mailbox> sink;std::atomic_bool done{false};};
// OS callbacks retain only a bounded mailbox, never a cache or application
// pointer. A failed/missing cancellation acknowledgement retains one bounded
// slot; it cannot cause use-after-free or an unbounded retry queue.
inline std::mutex registry_mutex;
inline std::map<Context *,std::shared_ptr<Context>> registry;
inline void retain(const std::shared_ptr<Context> &context){std::lock_guard lock(registry_mutex);require(registry.size()<128);require(registry.emplace(context.get(),context).second);}
template<class T>std::shared_ptr<T> lookup(void *pointer){std::lock_guard lock(registry_mutex);const auto found=registry.find(static_cast<Context *>(pointer));if(found==registry.end())return {};return std::dynamic_pointer_cast<T>(found->second);}
inline void complete(const std::shared_ptr<Context> &context){context->done=true;std::lock_guard lock(registry_mutex);registry.erase(context.get());}
struct Browse:Context {DNS_SERVICE_BROWSE_REQUEST request{};DNS_SERVICE_CANCEL cancel{};unsigned interface_index{};Api api;
    static void WINAPI callback(DWORD status,void *pointer,DNS_RECORD *records)noexcept{
        struct Free {DNS_RECORD *value;~Free(){if(value)::DnsRecordListFree(value,DnsFreeRecordList);}} free{records};
        const auto self=lookup<Browse>(pointer);if(!self)return;
        if(status==ERROR_CANCELLED){complete(self);return;}
        const auto sink=self->sink.lock();if(!sink)return;
        try{require(status==ERROR_SUCCESS);unsigned count{};
            for(auto *record=records;record;record=record->pNext){require(++count<=128);if(record->wType!=DNS_TYPE_PTR)continue;
                const auto charset=static_cast<DNS_CHARSET>(record->Flags.S.CharSet);
                require(charset==DnsCharSetUnicode||charset==DnsCharSetUtf8||charset==DnsCharSetAnsi);
                auto *copy=::DnsRecordCopyEx(record,DnsCharSetUnknown,DnsCharSetUnicode);require(copy);
                Free copy_owner{copy};const auto *wide=reinterpret_cast<const DNS_RECORDW *>(copy);
                require(folded(bounded(wide->pName))==query);const auto full=bounded(wide->Data.PTR.pNameHost);
                const PiPairDiscovery::Key key{self->interface_index,instance(full),0};sink->push({Event::Kind::Found,key,full,record->dwTtl==0,0,{}});
            }
        }catch(...){sink->fail();}
    }
};
struct Resolve:Context {DNS_SERVICE_RESOLVE_REQUEST request{};DNS_SERVICE_CANCEL cancel{};PiPairDiscovery::Key key;std::wstring full;std::uint64_t revision{};Api api;
    static void WINAPI callback(DWORD status,void *pointer,DNS_SERVICE_INSTANCE *value)noexcept{
        struct Free {DNS_SERVICE_INSTANCE *value;~Free(){if(value)::DnsServiceFreeInstance(value);}} free{value};
        const auto self=lookup<Resolve>(pointer);if(!self)return;const auto sink=self->sink.lock();
        if(sink){try{require(status==ERROR_SUCCESS&&value);auto row=resolved_instance(*value,self->key);sink->push({Event::Kind::Resolved,self->key,{},false,self->revision,std::move(row)});}
            catch(...){sink->push({Event::Kind::Resolved,self->key,{},true,self->revision,{}});}}
        complete(self);
    }
};
class Browser {
public:
    explicit Browser(Api api={}):api_(api),mail_(std::make_shared<Mailbox>()){
        const auto addresses=api_.addresses();require(addresses.has_value());std::set<unsigned> interfaces;for(const auto &address:*addresses)interfaces.insert(address.interface_index);
        require(!interfaces.empty()&&interfaces.size()<=32);
        browses_.reserve(interfaces.size());
        try{for(const auto index:interfaces){auto item=std::make_shared<Browse>();item->api=api_;item->sink=mail_;item->interface_index=index;
            item->request.Version=DNS_QUERY_REQUEST_VERSION1;item->request.InterfaceIndex=index;item->request.QueryName=query;item->request.pBrowseCallback=Browse::callback;item->request.pQueryContext=item.get();
            retain(item);browses_.push_back(item);const auto status=api_.browse(&item->request,&item->cancel);if(status!=DNS_REQUEST_PENDING){complete(item);require(false);}}}
        catch(...){stop();throw;}
    }
    ~Browser(){stop();}
    Browser(const Browser &)=delete;
    std::vector<Row> poll(){
        std::deque<Event> events;{std::lock_guard lock(mail_->mutex);require(!mail_->failed);events.swap(mail_->events);}std::vector<Row> rows;
        for(auto &event:events){
            if(event.kind==Event::Kind::Found){
                if(event.removed){const auto found=services_.find(event.key);if(found!=services_.end()){cancel(found->second.query);services_.erase(found);}rows.push_back({event.key,{},0,{},true});}
                else if(!services_.contains(event.key)&&services_.size()<64)services_.emplace(event.key,Service{std::move(event.full),{},nullptr,0});
            }else{const auto found=services_.find(event.key);if(found==services_.end()||found->second.revision!=event.revision)continue;
                rows.push_back(event.removed?Row{event.key,{},0,{},true}:std::move(event.row));found->second.query.reset();}
        }
        const auto now=std::chrono::steady_clock::now();
        for(auto &[key,service]:services_){
            if(service.query&&now-service.attempted>=std::chrono::seconds(3)){cancel(service.query);service.query.reset();rows.push_back({key,{},0,{},true});}
            if(!service.query&&(service.attempted==std::chrono::steady_clock::time_point{}||now-service.attempted>=std::chrono::seconds(5))){
                require(next_!=std::numeric_limits<std::uint64_t>::max());auto item=std::make_shared<Resolve>();item->api=api_;item->sink=mail_;item->key=key;item->full=service.full;item->revision=++next_;
                item->request.Version=DNS_QUERY_REQUEST_VERSION1;item->request.InterfaceIndex=key.interface_index;item->request.QueryName=item->full.data();item->request.pResolveCompletionCallback=Resolve::callback;item->request.pQueryContext=item.get();
                retain(item);service.attempted=now;service.revision=item->revision;const auto status=api_.resolve(&item->request,&item->cancel);
                if(status!=DNS_REQUEST_PENDING){complete(item);rows.push_back({key,{},0,{},true});}else service.query=std::move(item);
            }
        }
        return rows;
    }
private:
    struct Service {std::wstring full;std::chrono::steady_clock::time_point attempted;std::shared_ptr<Resolve> query;std::uint64_t revision;};
    Api api_;std::shared_ptr<Mailbox> mail_;std::vector<std::shared_ptr<Browse>> browses_;std::map<PiPairDiscovery::Key,Service> services_;std::uint64_t next_{};
    static void cancel(const std::shared_ptr<Resolve> &item)noexcept{if(item&&!item->done.load())(void)item->api.resolve_cancel(&item->cancel);}
    void stop()noexcept{
        for(const auto &item:browses_)if(!item->done.load())(void)item->api.browse_cancel(&item->cancel);
        for(const auto &[key,service]:services_){(void)key;cancel(service.query);}services_.clear();browses_.clear();mail_.reset();
    }
};
inline std::wstring utf16(const std::string &text){
    require(!text.empty()&&text.size()<=2048);const auto length=::MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);require(length>0);
    std::wstring value(static_cast<std::size_t>(length),L'\0');require(::MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),value.data(),length)==length);return value;
}
inline std::wstring service_name(const std::string &name){
    require(!name.empty()&&name.size()<=63);auto result=utf16(name);std::wstring escaped;
    for(const auto c:result){require(c>=32&&c!=127);if(c==L'.'||c==L'\\')escaped+=L'\\';escaped+=c;}return escaped+L"."+query;
}
struct NativeInstance {
    DNS_SERVICE_INSTANCE *value{};
    ~NativeInstance(){if(value)::DnsServiceFreeInstance(value);}
    NativeInstance()=default;NativeInstance(const NativeInstance &)=delete;NativeInstance &operator=(const NativeInstance &)=delete;
};
struct Registration:Context {
    enum class Phase {Registering,Registered,Deregistering,Done};
    Api api;DNS_SERVICE_REGISTER_REQUEST request{};DNS_SERVICE_CANCEL cancel{};NativeInstance requested,active;
    std::mutex mutex;Phase phase=Phase::Registering;bool stopping{};std::atomic_bool confirmed{false},failed{false};
    PiPairDiscovery::Key key;std::string host;std::vector<std::string> fields;
    void stop()noexcept{
        bool remove=false,cancel_pending=false;
        {std::lock_guard lock(mutex);stopping=true;confirmed=false;
            if(phase==Phase::Registered){phase=Phase::Deregistering;remove=true;}
            else if(phase==Phase::Registering)cancel_pending=true;}
        if(cancel_pending)(void)api.register_cancel(&cancel);
        if(remove){const auto result=api.deregister(&request,nullptr);if(result!=DNS_REQUEST_PENDING)failed=true;}
    }
    static void WINAPI callback(DWORD status,void *pointer,DNS_SERVICE_INSTANCE *value)noexcept{
        NativeInstance release;release.value=value;const auto self=lookup<Registration>(pointer);if(!self)return;
        bool terminal=false,remove=false;
        {
            std::lock_guard lock(self->mutex);
            if(self->phase==Phase::Deregistering){if(status!=ERROR_SUCCESS)self->failed=true;self->phase=Phase::Done;terminal=true;}
            else if(self->phase==Phase::Registering){
                if(status!=ERROR_SUCCESS||!value){self->failed=true;self->phase=Phase::Done;terminal=true;}
                else{
                    self->active.value=std::exchange(release.value,nullptr);self->request.pServiceInstance=self->active.value;self->phase=Phase::Registered;
                    try{const auto row=resolved_instance(*self->active.value,self->key);auto expected=self->fields,actual=row.fields;
                        std::sort(expected.begin(),expected.end());std::sort(actual.begin(),actual.end());require(expected==actual&&folded(utf16(row.host))==folded(utf16(self->host)));
                        const auto same=[](const auto *a,const auto *b,std::size_t bytes){return (!a&&!b)||(a&&b&&std::memcmp(a,b,bytes)==0);};
                        require(same(self->requested.value->ip4Address,self->active.value->ip4Address,sizeof(IP4_ADDRESS))&&same(self->requested.value->ip6Address,self->active.value->ip6Address,sizeof(IP6_ADDRESS)));
                        self->confirmed=!self->stopping;}
                    catch(...){self->failed=true;self->stopping=true;}
                    remove=self->stopping;
                }
            }
        }
        if(remove)self->stop();if(terminal)complete(self);
    }
};
class Publisher {
public:
    Publisher(const std::string &name,const std::string &host,const std::vector<std::string> &fields,
        const std::vector<WindowsLanAddress> &addresses,Api api={}):api_(api){
        require(fields.size()==7&&!addresses.empty()&&addresses.size()<=128);std::map<unsigned,std::pair<std::string,std::string>> interfaces;
        for(const auto &address:addresses){require(address.interface_index&&interfaces.size()<=32);auto &pair=interfaces[address.interface_index];
            if(address.family==AF_INET){if(pair.first.empty())pair.first=address.address;}else if(address.family==AF_INET6){if(pair.second.empty())pair.second=address.address;}else require(false);}
        require(interfaces.size()<=32);std::vector<std::wstring> keys,values;for(const auto &field:fields){const auto equal=field.find('=');require(equal!=std::string::npos);keys.push_back(utf16(field.substr(0,equal)));values.push_back(utf16(field.substr(equal+1)));}
        std::vector<const wchar_t *> key_pointers,value_pointers;for(const auto &key:keys)key_pointers.push_back(key.c_str());for(const auto &value:values)value_pointers.push_back(value.c_str());
        const auto full=service_name(name),native_host=utf16(host);registrations_.reserve(interfaces.size());
        try{for(const auto &[index,pair]:interfaces){
            IP4_ADDRESS v4{};IP6_ADDRESS v6{};if(!pair.first.empty())require(::inet_pton(AF_INET,pair.first.c_str(),&v4)==1);if(!pair.second.empty())require(::inet_pton(AF_INET6,pair.second.c_str(),&v6)==1);
            auto item=std::make_shared<Registration>();item->api=api_;item->key={index,name,0};item->host=host;item->fields=fields;
            item->requested.value=::DnsServiceConstructInstance(full.c_str(),native_host.c_str(),pair.first.empty()?nullptr:&v4,pair.second.empty()?nullptr:&v6,31418,0,0,7,key_pointers.data(),value_pointers.data());require(item->requested.value);
            item->requested.value->dwInterfaceIndex=index;item->request.Version=DNS_QUERY_REQUEST_VERSION1;item->request.InterfaceIndex=index;item->request.pServiceInstance=item->requested.value;
            item->request.pRegisterCompletionCallback=Registration::callback;item->request.pQueryContext=item.get();item->request.unicastEnabled=FALSE;
            retain(item);registrations_.push_back(item);const auto result=api_.register_service(&item->request,&item->cancel);if(result!=DNS_REQUEST_PENDING){item->phase=Registration::Phase::Done;complete(item);require(false);}
        }}catch(...){stop();throw;}
    }
    ~Publisher(){stop();}
    Publisher(const Publisher &)=delete;
    bool failed()const{for(const auto &item:registrations_)if(item->failed.load())return true;return false;}
    bool published()const{if(registrations_.empty()||failed())return false;for(const auto &item:registrations_)if(!item->confirmed.load())return false;return true;}
private:
    Api api_;std::vector<std::shared_ptr<Registration>> registrations_;
    void stop()noexcept{for(const auto &item:registrations_)item->stop();registrations_.clear();}
};

}
#endif
