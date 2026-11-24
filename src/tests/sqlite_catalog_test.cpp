// SPDX-License-Identifier: MIT
#include "application_storage/sqlite_catalog.hpp"
#include "wtp_settings_json.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#ifdef _WIN32
#include "native_windows_fixture.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#else
#include <unistd.h>
#endif

using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
template<class Function> void refuses(Function action) {
    bool caught=false;try{action();}catch(const std::exception &){caught=true;}assert(caught);
}
}
int main() {
#ifdef _WIN32
    NativeWindowsFixture fixture;const auto root=NativeWindowsFixture::utf8(fixture.root);
#else
    char pattern[]="/tmp/wsprrypi-sqlite-catalog-XXXXXX";
    const auto created=mkdtemp(pattern);assert(created);
    const auto root=std::filesystem::canonical(created).string();
    struct Cleanup{std::string root;~Cleanup(){std::filesystem::remove_all(root);}} cleanup{root};
#endif
    const std::string self(32,'a'),history(32,'b');
    auto store=std::make_shared<SqliteStateStore>(root,self,StateRole::Pi,StateOpen::CreateNew);
    WtpCatalog catalog(sqlite_catalog_storage(store));
    auto anchor=catalog.pi_management_anchor(self,{});
    assert(!anchor.read());
    refuses([&]{catalog.snapshot({});});
    assert(store->snapshot()==(StateSnapshot{"0",{}}));
    const auto initial=WtpCatalog::initial_storage_image();
    store->commit("0",{{StateRecord::Catalog,initial}});
    const auto before=catalog.snapshot({});
    assert(before.first.at("profiles").empty() && !anchor.read());
    bool prepared=false;
    refuses([&]{anchor.coordinate(history,[&](const auto &old,const auto &after){
        assert(old==initial && Json::parse(after)["pi_management_history"]["history_id"]==history);
        prepared=true;throw std::runtime_error("before commit");
    });});
    assert(prepared && !anchor.read() && store->snapshot().revision=="1");
    anchor.coordinate(history,[&](const auto &old,const auto &after){
        const auto saved=store->snapshot();
        assert(saved.records.at(StateRecord::Catalog)==old);
        store->commit(saved.revision,{{StateRecord::Catalog,after},{StateRecord::Operations,"coordinated receipt"}});
    });
    assert(anchor.read()==std::optional<std::string>(history));
    const auto committed=store->snapshot();
    assert(committed.revision=="2" && committed.records.at(StateRecord::Operations)=="coordinated receipt");
    const auto anchored=catalog.snapshot({});
    assert(anchored.first==before.first); // private history is not projected
    assert(anchored.second!=before.second); // its revision still fences writes
    assert(catalog.current_storage_image()==committed.records.at(StateRecord::Catalog));
    refuses([&]{catalog.pi_management_anchor(std::string(32,'c'),{}).read();});
    assert(store->snapshot()==committed);
    // Validate real current profile mutation and persistence through the adapter.
    WtpSettings settings;
    settings.path="/dev/wsprrypi-software-fixture";settings.usb_serial="TEST-USB-1";
    settings.vendor_id=11914;settings.product_id=10;settings.device_id=std::string(32,'d');
    Json add{{"operation","add"},{"name","USB device"},{"method","manual"},{"discovery_id",""},
        {"consent_plain",false},{"settings",management_profile_settings_json(settings)}};
    const auto changed=catalog.mutate(add,anchored.second,{});
    assert(changed.first.at("profiles").size()==1);
    assert(store->snapshot().records.at(StateRecord::Operations)=="coordinated receipt");
    const auto saved=store->snapshot();
    const auto stored_catalog=Json::parse(saved.records.at(StateRecord::Catalog));
    assert(stored_catalog.at("version")==3 && stored_catalog.at("profiles").size()==1);
    assert(stored_catalog.at("profiles").at(0).at("settings")==management_profile_settings_json(settings));
    refuses([&]{catalog.mutate(add,anchored.second,{});});
    assert(store->snapshot()==saved);
    WtpCatalog same_owner(sqlite_catalog_storage(store));
    const auto reopened=same_owner.snapshot({});
    assert(reopened.first.at("profiles")==changed.first.at("profiles") && reopened.second==changed.second &&
        same_owner.pi_management_anchor(self,{}).read()==anchor.read());
#ifdef _WIN32
    // Explicit legacy-source owner, never a fallback for missing SQL records.
    NativeWindowsFixture legacy;
    const auto legacy_path=NativeWindowsFixture::utf8(legacy.root/L"source-\u03a9.wtp-devices.json");
    WtpCatalog legacy_owner(legacy_path);const auto empty=legacy_owner.snapshot({});
    assert(empty.first.at("profiles").empty());
    const auto inserted=legacy_owner.mutate(add,empty.second,{});assert(inserted.first.at("profiles").size()==1);
    const auto legacy_before=PiManagementJournal::read_private_record(legacy_path);assert(legacy_before);
    WtpCatalog legacy_reopened(legacy_path);const auto legacy_snapshot=legacy_reopened.snapshot({});
    assert(legacy_snapshot.first.at("profiles")==inserted.first.at("profiles")&&legacy_snapshot.second==inserted.second);
    assert(PiManagementJournal::read_private_record(legacy_path)->bytes==legacy_before->bytes);
    PiManagementJournal::publish_private_record(legacy_path,legacy_before,"{\"version\":999}");
    refuses([&]{legacy_reopened.snapshot({});});
    const auto missing=NativeWindowsFixture::utf8(legacy.root/L"lost");
    windows_file::PrivateSecurity directory_security(windows_file::current_sid());
    assert(::CreateDirectoryW(windows_file::api_path(windows_file::path_from_utf8(missing+".management")).c_str(),directory_security.attributes()));
    WtpCatalog lost(missing+".wtp-devices.json");refuses([&]{lost.snapshot({});});
    assert(!std::filesystem::exists(legacy.root/L"lost.wtp-devices.json"));
#endif
    std::cout<<"SQLite catalog: no read initialization, current validation, shared atomic anchor/receipt publication and revision refusal passed\n";
}
