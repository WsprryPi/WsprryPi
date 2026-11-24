// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "application_storage/state_migration.hpp"
#include "application_storage/persistent_configuration.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <cassert>
#include <iostream>
using namespace wsprrypi;
namespace {
template<class F>void refuses(F action){bool held=false;try{action();}catch(const std::exception &){held=true;}assert(held);}
std::vector<StateChange> seed(const std::string &,const std::string &){
    return {{StateRecord::Configuration,encode_persistent_configuration(
        nlohmann::json{{"Operation",{{"Web Port",32001},{"Socket Port",32002}}}},{},StateRole::Controller)},
        {StateRecord::Catalog,"{}"}};
}
void validate(const std::shared_ptr<SqliteStateStore> &database){
    assert(database->snapshot().revision=="1");
    assert(configuration_from_snapshot(database->snapshot(),StateRole::Controller).configuration["Operation"]["Web Port"]==32001);
}
}
int main(){try{
    const std::string self(32,'a');
    for(const auto point:{StateMigrationPoint::DatabaseCommitted,StateMigrationPoint::DatabasePublished,StateMigrationPoint::Initialized}){
        NativeWindowsFixture fixture;const auto root=NativeWindowsFixture::utf8(fixture.root/L"application-\u03a9");
        refuses([&]{initialize_application_state(root,self,StateRole::Controller,seed,validate,
            [&](auto current){if(current==point)throw std::runtime_error("interrupted");});});
        const auto before=PiManagementJournal::read_private_record(root+"/authority.json");assert(before);
        const auto proof=nlohmann::json::parse(before->bytes).at("source_revision");
        auto database=initialize_application_state(root,self,StateRole::Controller,seed,validate);validate(database);
        assert(nlohmann::json::parse(*database->record(StateRecord::Initialization).bytes).at("source_revision")==proof);
        refuses([&]{open_application_state(root,self,StateRole::Controller);});
        database.reset();database=open_application_state(root,self,StateRole::Controller);validate(database);database.reset();
        assert(::DeleteFileW(windows_file::api_path(windows_file::path_from_utf8(root+"/authority.json")).c_str()));
        refuses([&]{open_application_state(root,self,StateRole::Controller);});
        refuses([&]{initialize_application_state(root,self,StateRole::Controller,seed,validate);});
    }
    NativeWindowsFixture migration;
    const auto source=NativeWindowsFixture::utf8(migration.root/L"source.ini"),root=NativeWindowsFixture::utf8(migration.root/L"migrated");
    const std::string original="[Operation]\nWeb Port=32001\nSocket Port=32002\n";
    PiManagementJournal::publish_private_record(source,{},original);
    const auto capture=[&]{StateMigrationCapture image;image.source_namespace=source;
        image.source_revision=PiManagementJournal::digest(original);image.records=seed({},{});
        image.original_records={{StateRecord::Configuration,original}};return image;};
    auto database=migrate_application_state(root,self,StateRole::Controller,source,capture,validate);validate(database);database.reset();
    assert(!std::filesystem::exists(migration.root/L"source.ini")&&PiManagementJournal::read_ini_input(source+".sqlite-migrated").bytes==original);
    database=open_application_state(root,self,StateRole::Controller);validate(database);
    std::cout<<"Native generation preparation/publication restart, persistent nonce, owner exclusion, missing-authority hold, and source-retirement checks passed\n";
    return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
