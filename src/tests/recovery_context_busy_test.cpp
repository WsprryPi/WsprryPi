// SPDX-License-Identifier: MIT
#include "application_recovery/validation_request.hpp"
#include "application_control/local_transport.hpp"
#include "application_storage/application_state.hpp"
#include "application_storage/state_migration.hpp"
#include "application_storage/migration_source.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "wtp_integration/pi_pair_owner.hpp"
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>
#include <unistd.h>
using namespace wsprrypi;
void check(bool value){if(!value)throw std::runtime_error("Recovery owner contention contract failed");}
int main(){try{
    char pattern[]="/tmp/recovery-context-busy-XXXXXX";const auto created=::mkdtemp(pattern);check(created);
    const auto root=std::filesystem::canonical(created).string();
    struct Remove {std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
    const std::string self(32,'a');
    auto database=initialize_application_state(root+"/state",self,StateRole::Controller,
        [&](const auto &name,const auto &installation){return initial_application_records(name,self,StateRole::Controller,installation);},validate_application_storage);
    ApplicationStateSelection selected(database);
    testing::set_wtp_pi_private_state_path(root+"/ledger");
    auto owner=prepare_pi_pair_owner(self);check(!owner->history()->available());
    const auto before=database->snapshot();
    std::promise<void> entered,release;auto ready=entered.get_future(),finish=release.get_future();
    std::exception_ptr failure;
    std::thread worker([&]{try{owner->with_backup_files([&](const auto &){entered.set_value();finish.wait();});}
        catch(...){failure=std::current_exception();entered.set_value();}});
    struct Release {
        std::promise<void> &promise;std::thread &worker;
        ~Release(){promise.set_value();if(worker.joinable())worker.join();}
    } wake{release,worker};
    check(ready.wait_for(std::chrono::seconds(5))==std::future_status::ready);
    if(failure)std::rethrow_exception(failure);
    bool busy=false;
    try{(void)recovery_destination_context(database);}
    catch(const LocalCommandRefused &error){busy=error.reason()==LocalRefusal::Busy;}
    check(busy&&database->snapshot()==before&&!std::filesystem::exists(root+"/ledger.pairing-"+self));
    std::cout<<"PASS recovery destination owner contention is busy before any write\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
