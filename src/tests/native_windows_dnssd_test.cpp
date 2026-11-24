// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_discovery_windows.hpp"
#include "wtp_integration/control_contract.hpp"
#include <cassert>
#include <iostream>
namespace dns=wsprrypi::windows_dnssd;
namespace fixture {
std::vector<DNS_SERVICE_BROWSE_REQUEST *> browses;
std::vector<DNS_SERVICE_RESOLVE_REQUEST *> resolves;
std::vector<DNS_SERVICE_REGISTER_REQUEST *> registrations,deregistrations;
unsigned cancelled_registrations{};
DWORD WINAPI register_service(DNS_SERVICE_REGISTER_REQUEST *request,DNS_SERVICE_CANCEL *){assert(request->InterfaceIndex==7&&!request->unicastEnabled&&!request->hCredentials);registrations.push_back(request);return DNS_REQUEST_PENDING;}
DWORD WINAPI deregister(DNS_SERVICE_REGISTER_REQUEST *request,DNS_SERVICE_CANCEL *cancel){assert(!cancel);deregistrations.push_back(request);return DNS_REQUEST_PENDING;}
DWORD WINAPI register_cancel(DNS_SERVICE_CANCEL *){++cancelled_registrations;return ERROR_SUCCESS;}

std::optional<std::vector<wsprrypi::WindowsLanAddress>> addresses(){return std::vector<wsprrypi::WindowsLanAddress>{{7,AF_INET,"10.88.0.1"}};}
DNS_STATUS WINAPI browse(DNS_SERVICE_BROWSE_REQUEST *request,DNS_SERVICE_CANCEL *){assert(request->InterfaceIndex==7&&request->Version==DNS_QUERY_REQUEST_VERSION1);browses.push_back(request);return DNS_REQUEST_PENDING;}
DNS_STATUS WINAPI resolve(DNS_SERVICE_RESOLVE_REQUEST *request,DNS_SERVICE_CANCEL *){assert(request->InterfaceIndex==7&&request->Version==DNS_QUERY_REQUEST_VERSION1);resolves.push_back(request);return DNS_REQUEST_PENDING;}
DNS_STATUS WINAPI browse_cancel(DNS_SERVICE_CANCEL *cancel){
    for(auto *request:browses){auto *context=static_cast<dns::Browse *>(request->pQueryContext);if(&context->cancel==cancel){request->pBrowseCallback(ERROR_CANCELLED,request->pQueryContext,nullptr);return ERROR_SUCCESS;}}
    return ERROR_INVALID_PARAMETER;
}
DNS_STATUS WINAPI resolve_cancel(DNS_SERVICE_CANCEL *){return ERROR_SUCCESS;} // Deliberately delayed completion.
dns::Api api(){dns::Api value;value.addresses=addresses;value.browse=browse;value.browse_cancel=browse_cancel;value.resolve=resolve;value.resolve_cancel=resolve_cancel;value.register_service=register_service;value.deregister=deregister;value.register_cancel=register_cancel;return value;}
std::wstring fullname=L"Fixture._wsprry-mgmt._tcp.local.";
void found(bool removed=false){
    DNS_RECORDW record{};record.pName=const_cast<wchar_t *>(dns::query);record.wType=DNS_TYPE_PTR;record.wDataLength=sizeof(DNS_PTR_DATAW);record.dwTtl=removed?0:120;
    record.Flags.S.CharSet=DnsCharSetUnicode;record.Data.PTR.pNameHost=fullname.data();
    auto *copy=::DnsRecordCopyEx(reinterpret_cast<DNS_RECORD *>(&record),DnsCharSetUnicode,DnsCharSetUnicode);assert(copy);
    auto *request=browses.back();request->pBrowseCallback(ERROR_SUCCESS,request->pQueryContext,copy);
}
DNS_SERVICE_INSTANCE *instance(){
    const std::vector<std::wstring> keys{L"txtvers",L"binding",L"id",L"boot",L"http-port",L"suite",L"control-sha256"};
    const std::wstring digest(wsprrypi::control_schema_sha256.begin(),wsprrypi::control_schema_sha256.end());
    const std::vector<std::wstring> values{L"1",L"Wsprry-Pair/1",std::wstring(32,L'a'),std::wstring(32,L'b'),L"31417",L"SPAKE2+-P256-SHA256-HKDF-HMAC",digest};
    std::vector<const wchar_t *> key_pointers,value_pointers;for(const auto &key:keys)key_pointers.push_back(key.c_str());for(const auto &value:values)value_pointers.push_back(value.c_str());
    IP4_ADDRESS address{};assert(::inet_pton(AF_INET,"10.88.0.1",&address)==1);
    auto *result=::DnsServiceConstructInstance(fullname.c_str(),L"wsprry-management-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local.",&address,nullptr,31418,0,0,7,key_pointers.data(),value_pointers.data());assert(result);result->dwInterfaceIndex=7;return result;
}
void finish(DNS_SERVICE_RESOLVE_REQUEST *request,DNS_SERVICE_INSTANCE *value){request->pResolveCompletionCallback(ERROR_SUCCESS,request->pQueryContext,value);}
template<class F>void refuses(F action){bool refused=false;try{action();}catch(const std::exception &){refused=true;}assert(refused);}
}
int main(){try{
    assert(dns::instance(L"Fixture._WSPRRY-MGMT._TCP.LOCAL.")=="Fixture");
    assert(dns::instance(L"A\\046B._wsprry-mgmt._tcp.local.")=="A.B");
    assert(dns::instance(L"\\206\\169._wsprry-mgmt._tcp.local.")=="\xce\xa9");
    for(const auto *value:{L"Fixture._other._tcp.local",L"Fixture._wsprry-mgmt._tcp.example",L"A.B._wsprry-mgmt._tcp.local",L"\\000._wsprry-mgmt._tcp.local",L"\\255._wsprry-mgmt._tcp.local"})fixture::refuses([&]{(void)dns::instance(value);});
    {
        dns::Browser browser(fixture::api());fixture::found();assert(browser.poll().empty()&&fixture::resolves.size()==1);
        auto *old=fixture::resolves.back();fixture::found(true);auto rows=browser.poll();assert(rows.size()==1&&rows[0].removed);
        fixture::found();assert(browser.poll().empty()&&fixture::resolves.size()==2);auto *current=fixture::resolves.back();
        fixture::finish(old,fixture::instance());assert(browser.poll().empty()); // An old resolve cannot revive a removed observation.
        fixture::finish(current,fixture::instance());rows=browser.poll();assert(rows.size()==1&&!rows[0].removed&&rows[0].key.interface_index==7);
        wsprrypi::PiPairDiscovery cache(std::string(32,'c'));cache.available(true);assert(cache.resolved(rows[0].key,"local.",rows[0].host,rows[0].port,rows[0].fields));assert(cache.snapshot()["discoveries"].size()==1);
    }
    assert(dns::registry.empty());fixture::browses.clear();fixture::resolves.clear();
    DNS_SERVICE_RESOLVE_REQUEST *pending{};
    {dns::Browser browser(fixture::api());fixture::found();browser.poll();pending=fixture::resolves.back();}
    assert(dns::registry.size()==1);fixture::finish(pending,fixture::instance());assert(dns::registry.empty()); // Completion after owner destruction only retires OS data.
    const auto fields=wsprrypi::pi_pair_discovery_txt(std::string(32,'a'),std::string(32,'b'),31417);
    const std::vector<wsprrypi::WindowsLanAddress> addresses{{7,AF_INET,"10.88.0.1"}};
    {
        dns::Publisher publisher("Fixture","wsprry-management-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local.",fields,addresses,fixture::api());assert(!publisher.published());
        auto *request=fixture::registrations.back();request->pRegisterCompletionCallback(ERROR_SUCCESS,request->pQueryContext,fixture::instance());assert(publisher.published()&&!publisher.failed());
    }
    assert(dns::registry.size()==1&&!fixture::deregistrations.empty());
    auto *deregistered=fixture::deregistrations.back();deregistered->pRegisterCompletionCallback(ERROR_SUCCESS,deregistered->pQueryContext,fixture::instance());assert(dns::registry.empty());
    // A successful registration arriving after destruction must deregister;
    // it cannot resurrect the publication flag or access the old owner.
    {dns::Publisher publisher("Fixture","wsprry-management-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local.",fields,addresses,fixture::api());}
    assert(fixture::cancelled_registrations>0&&dns::registry.size()==1);
    auto *late=fixture::registrations.back();late->pRegisterCompletionCallback(ERROR_SUCCESS,late->pQueryContext,fixture::instance());
    deregistered=fixture::deregistrations.back();deregistered->pRegisterCompletionCallback(ERROR_SUCCESS,deregistered->pQueryContext,fixture::instance());assert(dns::registry.empty());
    {
        dns::Publisher publisher("Fixture","wsprry-management-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local.",fields,addresses,fixture::api());auto *request=fixture::registrations.back();
        const auto original=fixture::fullname;fixture::fullname=L"Renamed._wsprry-mgmt._tcp.local.";
        auto *renamed=fixture::instance();fixture::fullname=original;
        request->pRegisterCompletionCallback(ERROR_SUCCESS,request->pQueryContext,renamed);assert(!publisher.published()&&publisher.failed());
        deregistered=fixture::deregistrations.back();deregistered->pRegisterCompletionCallback(ERROR_SUCCESS,deregistered->pQueryContext,fixture::instance());assert(publisher.failed()&&!publisher.published());
    }
    assert(dns::registry.empty());
    std::vector<std::shared_ptr<dns::Context>> retained;for(unsigned count=0;count<128;++count){auto item=std::make_shared<dns::Context>();dns::retain(item);retained.push_back(std::move(item));}
    fixture::refuses([]{dns::retain(std::make_shared<dns::Context>());});for(const auto &item:retained)dns::complete(item);assert(dns::registry.empty());
    std::cout<<"PASS native DNS-SD record conversion, exact contract consumption, interface and generation binding, registration/readback, rename refusal, bounded cancellation retention, and late completion after owner destruction; API calls injected, no LAN browsing or publication\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
