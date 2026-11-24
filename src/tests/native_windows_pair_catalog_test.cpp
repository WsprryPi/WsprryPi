// SPDX-License-Identifier: MIT
// Production catalog/WAL storage with synthetic private pairing records.
// This does not perform pairing or grant a live target authority.
#include "native_windows_fixture.hpp"
#include "wtp_integration/pi_pair_catalog_store.hpp"
#include "wtp_integration/pi_pair_records.hpp"
#include "wtp_integration/pi_pair_operations.hpp"
#include "wtp_integration/pi_pair_history_record.hpp"
#include "wtp_integration/pi_pair_peers.hpp"
#include <iostream>
#include <source_location>
namespace {
using namespace wsprrypi;
using Json=nlohmann::json;
void check(bool value,const std::source_location where=std::source_location::current()){
    if(!value)throw std::runtime_error("Native pair catalog fixture failed at line "+std::to_string(where.line()));
}
template<class F>void refuses(F action){bool refused=false;try{action();}catch(const std::exception &){refused=true;}check(refused);}
std::string text(const std::filesystem::path &path){return NativeWindowsFixture::utf8(path);}
void directory(const std::filesystem::path &path){windows_file::PrivateSecurity security(windows_file::current_sid());check(::CreateDirectoryW(windows_file::api_path(path).c_str(),security.attributes()));}
void put(const std::filesystem::path &path,const std::string &bytes){const auto before=PiManagementJournal::read_private_record(text(path));PiManagementJournal::publish_private_record(text(path),before,bytes);}
}
void test_native_windows_pair_catalog(const std::filesystem::path &root){
    const std::string self(32,'a'),peer(32,'b'),pair(32,'c'),secret(43,'A');
    // Recovery reconstructs the selector and its separately owned namespace.
    // Exercise that complete cold-read boundary, not only a standalone catalog.
    const auto owner=root/L"restored-history",selected_namespace=root/L"restored-history.ns-11111111111111111111111111111111";
    directory(owner);directory(selected_namespace);
    const Json empty{{"schema","pi-management-pairs/1"},{"self_id",self},{"authorization_generation","0"},{"active",nullptr},{"retired",Json::array()}};
    const auto operations=pi_pair_operations(self,Json::array()).document,peers=empty_pi_pair_peers(self);
    const auto selector=pi_pair_history_record(self,std::string(32,'1'),Json{{"kind","first"},{"operation_id",std::string(32,'2')},
        {"installation_id",std::string(32,'3')},{"source","controller-establishment"}},empty,operations,peers,false);
    put(owner/L"authority.json",selector.dump());put(selected_namespace/L"authority.json",empty.dump());
    put(selected_namespace/L"operations.json",operations.dump());put(selected_namespace/L"peers.json",peers.dump());
    for(unsigned cycle=0;cycle<2;++cycle){
        PiPairCatalogStore restored(text(selected_namespace),self,{},PiPairCatalogStore::Authority{text(owner/L"authority.json"),std::string(32,'1')});
        const auto snapshot=restored.snapshot();check(snapshot.authority&&snapshot.peers);
        check(snapshot.authority->bytes==selector.dump()&&snapshot.catalog.bytes==empty.dump()&&snapshot.peers->bytes==peers.dump());
    }
    const Json epoch{{"epoch","1"},{"tls_psk",secret},{"service_ms","0"},{"connections","0"}};
    const Json record{{"schema","pi-management-pair/1"},{"self_id",self},{"peer_id",peer},{"pair_id",pair},
        {"authorization_generation","1"},{"role","manager"},{"state","active"},{"current",epoch},
        {"pending",nullptr},{"previous",nullptr},{"receipt",nullptr},{"retirement_verifier",secret}};
    const Json original{{"schema","pi-management-pairs/1"},{"self_id",self},{"authorization_generation","1"},{"active",record},{"retired",Json::array()}};
    check(valid_pi_pair_catalog(original,self));
    const auto initialize=[&](const std::filesystem::path &path){directory(path);put(path/L"authority.json",original.dump());put(path/L"operations.json",pi_pair_operations(self,Json::array()).document.dump());};
    const auto selected=root/L"pair-catalog";
    refuses([&]{PiPairCatalogStore missing(text(selected),self);});check(!std::filesystem::exists(selected));
    directory(selected);refuses([&]{PiPairCatalogStore empty(text(selected),self);});check(std::filesystem::is_empty(selected));
    put(selected/L"authority.json",original.dump());put(selected/L"operations.json",pi_pair_operations(self,Json::array()).document.dump());
    {
        PiPairCatalogStore store(text(selected),self);
        refuses([&]{PiPairCatalogStore duplicate(text(selected),self);});
        const auto before=store.snapshot();auto next=original;next["active"]["current"]["connections"]="1";
        store.publish(before,next);check(parse_pi_pair_catalog(store.snapshot().catalog.bytes,self)==next);
        refuses([&]{store.publish(before,original);});check(parse_pi_pair_catalog(store.snapshot().catalog.bytes,self)==next);
        auto invalid=next;invalid["active"]["code"]="2345ABCD";refuses([&]{store.publish(store.snapshot(),invalid);});
        check(parse_pi_pair_catalog(store.snapshot().catalog.bytes,self)==next);
        put(selected/L"authority.json",next.dump());refuses([&]{store.snapshot();});check(!store.recover_own_failure());
    }
    for(const auto point:{PiPairCatalogWritePoint::Prepared,PiPairCatalogWritePoint::Committed,PiPairCatalogWritePoint::Published}){
        const auto path=root/(L"pair-failure-"+std::to_wstring(static_cast<int>(point)));initialize(path);
        auto next=original;next["authorization_generation"]="2";next["active"]["authorization_generation"]="2";
        {
            PiPairCatalogStore store(text(path),self,[&](auto current){if(current==point)throw std::runtime_error("Explicit fixture publication fault");});
            refuses([&]{store.publish(store.snapshot(),next);});check(store.recover_own_failure());
            check(parse_pi_pair_catalog(store.snapshot().catalog.bytes,self)==(point==PiPairCatalogWritePoint::Prepared?original:next));
            check(!store.recover_own_failure());
        }
        PiPairCatalogStore cold(text(path),self);check(parse_pi_pair_catalog(cold.snapshot().catalog.bytes,self)==(point==PiPairCatalogWritePoint::Prepared?original:next));
    }
    std::cout<<"PASS native pair catalog: no implicit initialization, nonrecursive ownership, stale/invalid proposal refusal, external-replacement hold, three WAL fault checkpoints, and cold reopen; synthetic records only\n";
}
