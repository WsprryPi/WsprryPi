// SPDX-License-Identifier: MIT
#include "application_storage/persistent_configuration.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <unistd.h>

using namespace wsprrypi;
namespace {
using Json = nlohmann::json;
template<class Function> void refuses(Function action, std::source_location location = std::source_location::current()) {
    bool caught = false;
    try { action(); } catch (const StateStorageError &) { caught = true; }
    if (!caught) {
        std::cerr << "Expected refusal at " << location.file_name() << ':' << location.line() << '\n';
        std::abort();
    }
}
Json native_configuration(const char *path) {
    std::ifstream source(path); assert(source);
    Json vectors; source >> vectors;
    for (const auto &item : vectors.at("cases"))
        if (item.at("name") == "Pi closed structural save") return item.at("value").at("candidate").at("configuration");
    throw std::runtime_error("Current production Pi configuration vector is required");
}
void controller() {
    const Json source{{"Operation", {{"Web Port", 32001}, {"Socket Port", 32002}}}};
    const PrivateConfiguration policy{true, SavedNetworkSafety::Enforced, false, false};
    const auto encoded = encode_persistent_configuration(source, policy, StateRole::Controller);
    const auto decoded = decode_persistent_configuration(encoded, StateRole::Controller);
    assert(decoded.configuration == source && decoded.private_settings == policy);
    refuses([&] { decode_persistent_configuration(encoded, StateRole::Pi); });
    refuses([&] { encode_persistent_configuration(source,
        {true, SavedNetworkSafety::Enforced, true, false}, StateRole::Controller); });
    auto invalid = source;
    invalid["Operation"]["Socket Port"] = 32001;
    refuses([&] { encode_persistent_configuration(invalid, policy, StateRole::Controller); });
    invalid["Operation"]["Socket Port"] = "32002";
    refuses([&] { encode_persistent_configuration(invalid, policy, StateRole::Controller); });
    invalid["Operation"]["Socket Port"] = 80;
    refuses([&] { encode_persistent_configuration(invalid, policy, StateRole::Controller); });
    auto document = Json::parse(encoded);
    for (const auto &bad : {Json{{"extra", true}}, Json{{"schema", "local-settings/2"}},
             Json{{"private", {{"debug_logging", "true"}, {"network_safety", "enforced"}}}},
             Json{{"private", {{"debug_logging", true}, {"network_safety", "unknown"}}}}}) {
        auto changed = document; changed.update(bad);
        refuses([&] { decode_persistent_configuration(changed.dump(), StateRole::Controller); });
    }
    auto duplicate = encoded;
    duplicate.insert(1, "\"schema\":\"local-settings/1\",");
    refuses([&] { decode_persistent_configuration(duplicate, StateRole::Controller); });
    duplicate = encoded;
    const std::string operation_marker = "\"Operation\":{";
    const auto operation_position = duplicate.find(operation_marker); assert(operation_position != std::string::npos);
    duplicate.insert(operation_position + operation_marker.size(), "\"Web Port\":32001,");
    refuses([&] { decode_persistent_configuration(duplicate, StateRole::Controller); });
    refuses([&] { decode_persistent_configuration(std::string(SqliteStateStore::maximum_record_bytes + 1, ' '), StateRole::Controller); });
    document["private"]["allow_unqualified_frequency"] = false;
    refuses([&] { decode_persistent_configuration(document.dump(), StateRole::Controller); });
}
void native_and_database(const char *path) {
    auto source = native_configuration(path);
    source["Si5351"]["Adapter Identity"] = "";
    source["Si5351"]["TX Output"] = "CLK2";
    source["CW"]["Fade Shape"] = "raised_cosine";
    source["CW"]["Fade In Ms"] = 27;
    source["CW"]["Fade Out Ms"] = 31;
    source["CW"]["Fade Slice Ms"] = 2;
    source["CW"]["Message"] = "Custom snowman ☃";
    source["Operation"]["Web Port"] = 32001;
    source["Operation"]["Socket Port"] = 32002;
    const auto saved = source;
    source["Meta"] = {{"Use INI", true}, {"INI Filename", "/private/not-a-live-setting"}, {"TX Iterations", 37}};
    source["Platform"] = {{"Observation", "not-saved"}};
    source["Si5351"]["Unknown Field"] = "not-saved";
    const PrivateConfiguration policy{true, SavedNetworkSafety::InsecureDisabled, true, true, true, 27};
    const auto encoded = encode_persistent_configuration(source, policy, StateRole::Pi);
    const auto decoded = decode_persistent_configuration(encoded, StateRole::Pi);
    assert(decoded.configuration == saved && decoded.private_settings == policy);
    assert(encoded.find("not-saved") == std::string::npos && encoded.find("not-a-live-setting") == std::string::npos);
    assert(decoded.configuration["Si5351"]["TX Output"] == "CLK2" && decoded.configuration["CW"]["Fade Slice Ms"] == 2);
    auto missing = source; missing["CW"].erase("Fade Slice Ms");
    refuses([&] { encode_persistent_configuration(missing, policy, StateRole::Pi); });
    auto invalid = Json::parse(encoded); invalid["configuration"]["CW"]["Fade Slice Ms"] = "2";
    refuses([&] { decode_persistent_configuration(invalid.dump(), StateRole::Pi); });
    invalid = Json::parse(encoded); invalid["configuration"]["Si5351"]["TX Output"] = "CLK9";
    refuses([&] { decode_persistent_configuration(invalid.dump(), StateRole::Pi); });
    invalid = Json::parse(encoded); invalid["configuration"]["Unknown"] = Json::object();
    refuses([&] { decode_persistent_configuration(invalid.dump(), StateRole::Pi); });
    char pattern[] = "/tmp/wsprrypi-configuration-store-XXXXXX";
    const auto root = mkdtemp(pattern); assert(root);
    const std::string directory = std::filesystem::canonical(root).string();
    struct Cleanup { std::string directory; ~Cleanup() { std::filesystem::remove_all(directory); } } cleanup{directory};
    {
        SqliteStateStore store(directory, std::string(32, 'a'), StateRole::Pi, StateOpen::CreateNew);
        refuses([&] { configuration_from_snapshot(store.snapshot(), StateRole::Pi); });
        store.commit("0", {{StateRecord::Configuration, encoded}, {StateRecord::Operations, "existing durable receipt"}});
    }
    SqliteStateStore restarted(directory, std::string(32, 'a'), StateRole::Pi, StateOpen::Existing);
    const auto snapshot = restarted.snapshot();
    assert(configuration_from_snapshot(snapshot, StateRole::Pi) == decoded);
    assert(snapshot.records.at(StateRecord::Operations) == "existing durable receipt");
}
}
int main(int argc, char **argv) {
    assert(argc == 2);
    controller();
    native_and_database(argv[1]);
    std::cout << "Persistent configuration: current closed fields, advanced/private values, role isolation and SQLite roundtrip passed\n";
}
