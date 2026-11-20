// SPDX-License-Identifier: MIT
// Actual production entry point and OS identity, with an explicit private
// software-fixture storage namespace. No alternate production CLI or policy.
#include "software_control_runtime.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "web_server.hpp"
int pi_managed_production_main(int,char **);
int main(int argc,char **argv){
    if(argc<2)return 2;
    wsprrypi::testing::SoftwareControlRuntime control_runtime(argv[1],argc,argv);
    for(int index=2;index<argc;++index)if(std::string_view(argv[index])=="--web-service")return pi_managed_production_main(argc-1,argv+1);
    wsprrypi::testing::set_wtp_pi_private_state_path(argv[1]);
    webServer.bind_loopback_for_test();
    return pi_managed_production_main(argc-1,argv+1);
}
