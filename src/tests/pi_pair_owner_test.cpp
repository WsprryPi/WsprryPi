// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_owner.hpp"
#include "wtp_integration/controller_retirement.hpp"
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace wsprrypi;
namespace {
unsigned checks{};void check(bool value){++checks;if(!value)throw std::runtime_error("pair owner check "+std::to_string(checks));}
std::string id(char c){return std::string(32,c);}
}
int main(){try{
    char pattern[]="/tmp/pi-pair-owner-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary);const std::string root=temporary;
    struct Remove {std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
    auto owner=std::make_shared<PiPairOwner>(root+"/pairing",id('a'));check(!owner->history()->available()&&!std::filesystem::exists(root+"/pairing"));
    unsigned proofs=0;
    {
        ControllerRetirementBook book(root+"/retirement",root+"/assignments.json",id('a'),nlohmann::json::array(),{},[&](PiPairInitializationProof proof){++proofs;owner->provision(std::move(proof));});
        check(proofs==1&&owner->history()->available());
    }
    auto worker=owner->history();const auto before=worker->snapshot();const auto selected=nlohmann::json::parse(before.authority->bytes);
    check(selected["initialization"]["source"]=="controller-establishment"&&nlohmann::json::parse(before.catalog.bytes)["active"].is_null());
    {
        ControllerRetirementBook book(root+"/retirement",root+"/assignments.json",id('a'),nlohmann::json::array(),{},[&](PiPairInitializationProof proof){++proofs;owner->provision(std::move(proof));});
        check(proofs==1&&worker->snapshot().authority->signature==before.authority->signature);
    }
    check(!owner->reopen_history());worker->begin_service();worker.reset();
    check(owner->reopen_history());worker=owner->history();check(worker->available()&&worker->unclean_start());
    // The established source never reissues a first-use event after selector
    // loss. Opening/polling cannot turn absence into another blank scope.
    check(std::filesystem::remove(root+"/pairing/authority.json"));check(!worker->available());
    {
        ControllerRetirementBook book(root+"/retirement",root+"/assignments.json",id('a'),nlohmann::json::array(),{},[&](PiPairInitializationProof proof){++proofs;owner->provision(std::move(proof));});
        check(proofs==1&&!owner->history()->available()&&!std::filesystem::exists(root+"/pairing/authority.json"));
    }
    // Even a separate positive fixture event cannot replace the owner behind
    // a worker. Its failure does not prevent retirement-book establishment.
    {
        unsigned attempted=0;ControllerRetirementBook book(root+"/another-retirement",root+"/another-assignments.json",id('a'),nlohmann::json::array(),{},[&](PiPairInitializationProof proof){++attempted;owner->provision(std::move(proof));});
        check(attempted==1&&owner->history()==worker&&!worker->available()&&!std::filesystem::exists(root+"/pairing/authority.json"));
    }
    const auto namespace_path=root+"/pairing.ns-"+selected["namespace_id"].get<std::string>();
    check(std::filesystem::exists(namespace_path+"/authority.json")&&std::filesystem::exists(namespace_path+"/peers.json"));
    std::cout<<"PASS positive first-use provisioning, read-only absence, established-source loss and active-owner isolation: "<<checks<<" checks\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
