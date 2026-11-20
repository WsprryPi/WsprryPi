// SPDX-License-Identifier: MIT
#include "application_recovery/browser_admission.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
using namespace wsprrypi;
void check(bool value){if(!value)throw std::runtime_error("Recovery browser admission assertion failed");}
int main(){try{
    char temporary[]="/tmp/recovery-browser-XXXXXX";check(::mkdtemp(temporary));const auto root=std::filesystem::canonical(temporary);
    struct Cleanup {std::filesystem::path root;~Cleanup(){std::filesystem::remove_all(root);}} cleanup{root};
    SupportRequestGuardSnapshot network{true,"wsprrypi",{},{{"192.168.50.10","255.255.255.0"}}};
    nlohmann::json admission;
    {
        RecoveryBrowserAdmission sessions(root.string(),std::string(32,'a'),[&]{return network;});admission=sessions.issue("wsprrypi:31415",false);
    }
    RecoveryBrowserAdmission sessions(root.string(),std::string(32,'a'),[&]{return network;});
    const auto cookie=admission["cookie"].get<std::string>();const auto id=cookie.substr(cookie.find('=')+1,64);
    check(cookie.find("; Path=/;")!=std::string::npos); // Covers the installed /wsprrypi alias as well as direct hosting.
    const auto file=PiManagementJournal::read_private_record((root/"browser-leases.json").string());check(bool(file));
    check(file->bytes.find(id)==std::string::npos&&file->bytes.find(admission["csrf"].get<std::string>())==std::string::npos);
    httplib::Request request;request.method="GET";request.path="/api/recovery/jobs";request.remote_addr="192.168.50.42";
    request.set_header("Host","wsprrypi:31415");request.set_header("Cookie",cookie.substr(0,cookie.find(';')));
    check(sessions.permits(request));
    request.method="POST";check(!sessions.permits(request));request.set_header("Origin","http://wsprrypi:31415");
    request.set_header("Content-Type","application/json");request.set_header("X-WsprryPi-Recovery-CSRF",admission["csrf"].get<std::string>());check(sessions.permits(request));
    request.remote_addr="192.168.51.42";check(!sessions.permits(request));request.remote_addr="192.168.50.42";
    request.headers.erase("Origin");request.set_header("Origin","http://evil.example");check(!sessions.permits(request));
    request.headers.erase("Origin");request.set_header("Origin","http://wsprrypi:31415");
    request.headers.emplace("Cookie",cookie.substr(0,cookie.find(';')));check(!sessions.permits(request));
    request.headers.erase("Cookie");request.set_header("Cookie",cookie.substr(0,cookie.find(';')));
    network.networks={{"10.20.30.2","255.255.255.0"}};check(!sessions.permits(request));
    network.networks={{"192.168.50.10","255.255.255.0"}};check(sessions.permits(request));
    for(unsigned index=0;index<10;++index)check(sessions.permits(request));
    check(PiManagementJournal::read_private_record((root/"browser-leases.json").string())->signature==file->signature);
    request.headers.erase("X-WsprryPi-Recovery-CSRF");request.set_header("X-WsprryPi-Recovery-CSRF",std::string(64,'0'));check(!sessions.permits(request));
    request.method="GET";check(sessions.permits(request));request.headers.erase("Cookie");check(!sessions.permits(request));
    std::cout<<"PASS restart-surviving maintenance admission, hashed private leases, enforced current network/Host/Origin, mutation CSRF, duplicate refusal and write-free polling\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
