// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <map>
#include <set>
#include <string>
namespace wsprrypi {
// Private controller storage authority, independent of the public catalog and
// native Pico payloads. An enrolled catalog entry anchors each journal epoch.
struct PicoManagementHistoryView {
    std::map<std::string,std::string> epochs;
    std::map<std::string,std::string> retired_epochs;
    std::set<std::string> devices;
};
struct PicoManagementHistoryAuthority {
    std::function<PicoManagementHistoryView()> read;
    std::function<void(const std::string &,const std::string &)> publish;
    std::function<void(const std::string &,const std::string &)> retire;
    std::function<void(const std::string &,const std::string &)> finish_retirement;
    bool isolated_for_test{};
    bool complete() const {return bool(read)&&bool(publish)&&bool(retire)&&bool(finish_retirement);}
    static PicoManagementHistoryAuthority isolated() {PicoManagementHistoryAuthority value;value.isolated_for_test=true;return value;}
};
} // namespace wsprrypi
