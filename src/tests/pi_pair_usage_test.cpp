// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_usage.hpp"
#include "wtp_integration/pi_pair_peers.hpp"
#include <iostream>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("pair usage check "+std::to_string(checks));}
template<class F>void refuses(F action){bool failed=false;try{action();}catch(...){failed=true;}check(failed);}
std::string id(char c){return std::string(32,c);}
Json pair(char identity,const std::string &connections="0"){return {{"schema","pi-management-pair/1"},{"self_id",id('a')},{"peer_id",id('b')},
    {"pair_id",id(identity)},{"authorization_generation","1"},{"role","manager"},{"state","active"},
    {"current",{{"epoch","1"},{"tls_psk",std::string(43,'A')},{"service_ms","0"},{"connections",connections}}},
    {"pending",nullptr},{"previous",nullptr},{"receipt",nullptr},{"retirement_verifier",std::string(43,'A')}};}
}
int main(){try{
    auto at=PiPairUsage::Time{};const auto clock=[&]{return at;};
    Json catalog{{"schema","pi-management-pairs/1"},{"self_id",id('a')},{"authorization_generation","1"},{"active",pair('c')},{"retired",Json::array()}};
    auto peers=empty_pi_pair_peers(id('a'));const PiPairTlsIdentity identity{PiPairTlsPurpose::Management,id('c'),"1","1"};
    PiPairUsage usage(id('a'),clock);check(usage.due(identity));usage.begin(catalog,peers,false);check(!usage.due(identity));
    const auto original=catalog.dump();usage.authenticated(identity);check(catalog.dump()==original);
    at+=std::chrono::milliseconds(PiPairUsage::service_limit_ms-1);check(!usage.due(identity));
    auto projected=usage.project(catalog,peers);check(projected.catalog["active"]["current"]["service_ms"]==std::to_string(PiPairUsage::service_limit_ms-1)&&
        projected.catalog["active"]["current"]["connections"]=="1");
    at+=std::chrono::milliseconds(1);check(usage.due(identity));
    auto again=usage.project(projected.catalog,projected.peers);check(again.catalog["active"]["current"]["service_ms"]==std::to_string(PiPairUsage::service_limit_ms));
    check(!usage.unclean_checkpoint_required());usage.stop();check(usage.due(identity));
    // Offline time before the next service start adds no service time.
    catalog["active"]["current"]["connections"]=std::to_string(PiPairUsage::connection_limit-1);
    at+=std::chrono::hours(24*365);PiPairUsage count(id('a'),clock);count.begin(catalog,peers,false);check(!count.due(identity));
    count.authenticated(identity);check(count.due(identity));check(count.project(catalog,peers).catalog["active"]["current"]["service_ms"]=="0");
    catalog["active"]["current"]["connections"]="18446744073709551615";PiPairUsage saturating(id('a'),clock);saturating.begin(catalog,peers,false);
    saturating.authenticated(identity);check(saturating.project(catalog,peers).catalog["active"]["current"]["connections"]=="18446744073709551615");
    catalog["active"]["current"]["connections"]="0";
    PiPairUsage dirty(id('a'),clock);dirty.begin(catalog,peers,true);check(dirty.due(identity)&&dirty.unclean_checkpoint_required());
    // An epoch created after startup inherits no old-epoch forced-renewal bit.
    catalog["active"]["current"]["epoch"]="2";dirty.reconcile(catalog,peers);
    const PiPairTlsIdentity second{PiPairTlsPurpose::Management,id('c'),"1","2"};check(!dirty.due(second)&&!dirty.unclean_checkpoint_required()&&dirty.due(identity));
    at+=std::chrono::milliseconds(10);dirty.authenticated(second);auto checkpoint=dirty.project(catalog,peers);
    check(checkpoint.catalog["active"]["current"]["service_ms"]=="10"&&checkpoint.catalog["active"]["current"]["connections"]=="1");
    at+=std::chrono::milliseconds(5);auto subsequent=dirty.project(checkpoint.catalog,checkpoint.peers);
    check(subsequent.catalog["active"]["current"]["service_ms"]=="15"&&subsequent.catalog["active"]["current"]["connections"]=="1");
    at-=std::chrono::milliseconds(1);check(dirty.due(second)&&dirty.unclean_checkpoint_required());refuses([&]{(void)dirty.project(catalog,peers);});
    const PiPairTlsIdentity wrong{PiPairTlsPurpose::Retirement,id('c'),"1",""};check(count.due(wrong));refuses([&]{count.authenticated(wrong);});
    auto invalid=catalog;invalid["active"]["current"]["connections"]=false;PiPairUsage malformed(id('a'),clock);
    refuses([&]{malformed.begin(invalid,peers,false);});check(malformed.due(second));
    // The same accounting includes outgoing active profiles, but excludes
    // awaiting-commit/uncertain keys which cannot authorize settings.
    catalog["active"]=nullptr;auto outgoing=pair('d');outgoing["authorization_generation"]="3";
    peers["profiles"].push_back({{"profile_id",id('e')},{"profile_generation","1"},{"target_id",id('b')},{"endpoint",{{"host","target.local"},{"port",31418}}},
        {"pair",outgoing},{"stage","active"},{"initial_operation_id",id('f')},{"initial_request_digest",std::string(64,'a')}});
    PiPairUsage both(id('a'),clock);both.begin(catalog,peers,true);const PiPairTlsIdentity remote{PiPairTlsPurpose::Management,id('d'),"3","1"};
    check(both.due(remote)&&both.unclean_checkpoint_required());both.authenticated(remote);
    check(both.project(catalog,peers).peers["profiles"][0]["pair"]["current"]["connections"]=="1");
    std::cout<<"PASS RAM epoch accounting, exact thresholds, offline time, unclean old-set and bounded projections: "<<checks<<" checks\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
