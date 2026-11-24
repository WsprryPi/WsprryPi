// SPDX-License-Identifier: MIT
// Actual collector policy, with its existing no-device fixture selection.
#include "native_windows_fixture.hpp"
#include "wtp_integration/usb_logging.hpp"
#include "wtp_integration/usb_discovery.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "wtp_settings_json.hpp"
#include <iostream>
#include <source_location>
namespace {
using namespace wsprrypi;
using Json=nlohmann::json;
void check(bool value,const std::source_location where=std::source_location::current()){
    if(!value)throw std::runtime_error("Native USB logging policy fixture failed at line "+std::to_string(where.line()));
}
std::string text(const std::filesystem::path &path){return NativeWindowsFixture::utf8(path);}
}
void test_native_windows_logging_policy(const std::filesystem::path &root){
    const auto unsupported=pico_management_usb_discovery(root,root);
    check(unsupported["available"]==false&&unsupported["candidates"].empty());
    const auto machine=text(root/L"logging-machine-id"),ini=text(root/L"logging.ini"),disk=ini+".usb-logs.json";
    PiManagementJournal::publish_private_record(machine,{},std::string(32,'1'));
    const std::string device(32,'a'),catalog_id(32,'c');
    WtpSettings settings;settings.path="\\\\?\\USB#VID_2E8A&PID_000A#policy-fixture";settings.usb_serial="1234567890ABCDEF";
    settings.vendor_id=0x2e8a;settings.product_id=0xa;settings.device_id=device;
    const Json catalog{{"version",1},{"profiles",{{{"id",catalog_id},{"name","Policy fixture Pico"},{"method","manual"},
        {"discovery_id",""},{"legacy_unverified",false},{"settings",wtp_settings_json(settings)}}}}};
    const auto start=[&]{testing::start_usb_logging_policy(ini,machine,[catalog]{return catalog;});};
    struct Stop {~Stop(){stop_usb_logging();}} stop;
    start();auto response=usb_logging_api("GET","","");check(response.status==200);
    const auto initial=Json::parse(response.body);check(initial["journal_available"]==false&&initial["attached"].empty()&&initial["candidates"].size()==1);
    const Json policy{{"schema","host-usb-logging/1"},{"collector_id",initial.at("collector_id")},
        {"sources",{{{"catalog_id",catalog_id},{"device_id",device},{"enabled",false},{"maximum_severity",6}}}}};
    check(usb_logging_api("PUT",policy.dump(),"stale").status==412&&!PiManagementJournal::read_private_record(disk));
    auto wrong=policy;wrong["collector_id"]=std::string(32,'f');check(usb_logging_api("PUT",wrong.dump(),response.etag).status==409);
    const auto saved=usb_logging_api("PUT",policy.dump(),response.etag);check(saved.status==200&&saved.etag!=response.etag);
    const auto observed=PiManagementJournal::read_private_record(disk);check(observed&&Json::parse(observed->bytes)==policy);
    stop_usb_logging();start();response=usb_logging_api("GET","","");check(response.status==200&&response.etag==saved.etag&&Json::parse(response.body)["sources"]==policy["sources"]);
    auto enabled=policy;enabled["sources"][0]["enabled"]=true;check(usb_logging_api("PUT",enabled.dump(),response.etag).status==409);
    auto invalid=policy;invalid["sources"][0]["maximum_severity"]=4294967302ULL;check(usb_logging_api("PUT",invalid.dump(),response.etag).status==409);
    invalid=policy;invalid["sources"][0]["catalog_id"]=std::string(32,'d');check(usb_logging_api("PUT",invalid.dump(),response.etag).status==409);
    check(PiManagementJournal::read_private_record(disk)->signature==observed->signature);
    const auto alias=root/L"logging-hardlink";check(::CreateHardLinkW(alias.c_str(),windows_file::path_from_utf8(disk).c_str(),nullptr));
    check(usb_logging_api("GET","","").status==409);check(::DeleteFileW(alias.c_str()));
    response=usb_logging_api("GET","","");check(response.status==200&&response.etag==saved.etag);
    const auto before=PiManagementJournal::read_private_record(disk);PiManagementJournal::publish_private_record(disk,before,"{\"schema\":1,\"schema\":2}");
    check(usb_logging_api("GET","","").status==409&&PiManagementJournal::read_private_record(disk)->bytes=="{\"schema\":1,\"schema\":2}");
    stop_usb_logging();
    std::cout<<"PASS native USB logging policy: private persistence and restart, stale/foreign/invalid-source refusal, unavailable journal enablement refusal, and hard-link/malformed-state holds; no USB inventory or open\n";
}
