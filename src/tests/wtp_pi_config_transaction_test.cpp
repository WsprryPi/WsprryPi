#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "scheduling.hpp"
#include "wtp_pi_control.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
static void require(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
int main(){
    char directory[]="/tmp/wsprrypi-wtp-config.XXXXXX";
    require(mkdtemp(directory),"temporary directory");
    struct Cleanup{std::string path;~Cleanup(){clear_wtp_pi_control_hooks();std::filesystem::remove_all(path);}} cleanup{directory};
    init_default_config();
    set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    set_si5351_detection_override_for_test(true);
    set_raspberry_pi_generation_override_for_test(4);
    config.use_ini=true;config.ini_filename=std::string(directory)+"/test.ini";
    config.mode=ModeType::WSPR;config.transmit=false;
    config.callsign="AA0NT";config.grid_square="EM18";config.power_dbm=10;config.frequencies="20m";
    config.transmit_backend=TransmitBackendKind::SI5351;
    resolve_backend_specific_config(config);config_to_json();
    std::ofstream(config.ini_filename).close();iniFile.set_filename(config.ini_filename);json_to_ini();
    std::string executable="wsprrypi", ini_option="-i", ini_path=config.ini_filename,
                backend_option="--backend", backend_value=" SIMULATED ";
    char* arguments[]={executable.data(),ini_option.data(),ini_path.data(),
                       backend_option.data(),backend_value.data()};
    require(parse_command_line(5,arguments),"case-insensitive explicit simulated CLI parses with physical INI");
    require(config.simulated_backend_override && config.transmit_backend==TransmitBackendKind::SIMULATED,
            "early INI validation and regular CLI parsing agree on explicit simulation");
    unsigned aborts=0;
    set_wtp_pi_control_hooks({[]{return true;},[]{return true;},[]{return true;},[](bool){},
        [&](bool enabled){if(enabled)++aborts;},[](bool){},[]{return true;},[]{}});
    patch_all_from_web_revision({{"Operation",{{"Transmit",true}}}}, {}, LocalEnableAction::FinishCurrent,true);
    require(aborts==0,"interactive finish does not invoke noninteractive abort");
    PreparedConfigCandidate own;
    prepare_ini_config_candidate(config.ini_filename,own);
    require(own.valid,own.error_reason);
    require(own.preserve_interactive_takeover,"own file-monitor reload retains finish choice");
    commit_config_candidate(own);
    require(aborts==0,"own reload does not cancel remote job");
    patch_all_from_web({{"WTP Server",{{"Port",31418}}}});
    prepare_ini_config_candidate(config.ini_filename,own);
    require(own.valid&&own.preserve_interactive_takeover,"unrelated HTTP patch preserves finish choice");
    require(aborts==0,"unrelated HTTP patch does not cancel");
    patch_all_from_web({{"Operation",{{"Transmit",true}}}});
    require(aborts==1,"explicit direct HTTP Enable aborts immediately");
    prepare_ini_config_candidate(config.ini_filename,own);
    require(own.valid&&!own.preserve_interactive_takeover,"direct HTTP clears interactive exemption");
    patch_all_from_web_revision({{"Operation",{{"Transmit",true}}}}, {}, LocalEnableAction::FinishCurrent,true);
    std::ofstream(config.ini_filename,std::ios::app)<<"\n; external writer\n";
    prepare_ini_config_candidate(config.ini_filename,own);
    require(own.valid&&!own.preserve_interactive_takeover,"external INI transaction clears exemption");
    commit_config_candidate(own);
    require(aborts==2,"enabled external INI immediately aborts");
    require(config.transmit_backend==TransmitBackendKind::SIMULATED && config.simulated_backend_override,
            "explicit simulation survives HTTP and INI reloads");
    require(jConfig["Operation"]["Transmit Backend"]=="si5351","simulation never replaces saved physical selection");
    std::cout<<"Interactive, HTTP and INI transaction boundaries passed.\n";
}
