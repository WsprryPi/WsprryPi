// SPDX-License-Identifier: MIT
// Actual host routes and native runtime; all output is explicitly simulated.
#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "runtime_config_bridge.hpp"
#include "wtp_runtime_bridge.hpp"
#include "version.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "web_server_routes.hpp"
#include "httplib.hpp"
#include "wtp_settings_json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <sys/stat.h>
using namespace wsprrypi;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(false)
int main(int,char**){
 try{
  char pattern[]="/tmp/wtp-retirement-runtime-XXXXXX";CHECK(mkdtemp(pattern));
  const std::string root=pattern,ini=root+"/station.ini";
  struct Cleanup{std::string path;~Cleanup(){stop_wtp_pi_endpoint();select_wtp_runtime(std::nullopt);std::filesystem::remove_all(path);}}cleanup{root};
  {std::ofstream initial(ini);initial<<"[Security]\nPrivileged Network Safety = enforced\n";}
  CHECK(chmod(ini.c_str(),0600)==0);
  init_default_config();config.use_ini=true;config.ini_filename=ini;iniFile.set_filename(ini);
  config.transmit_backend=TransmitBackendKind::SIMULATED;config.simulated_backend_override=true;config.transmit=false;
  config.use_led=config.use_amp=config.use_shutdown=false;
  config.callsign="AA0NT";config.grid_square="EM18";config.power_dbm=20;config.frequencies="20m";
  config.wtp.hostname="PrEsErVeD.LoCaL.";config.wtp.tcp_port=31499;config.wtp.start_uncertainty_ns=37000000;
  const auto legacy=wtp_settings_json(config.wtp);
  resolve_backend_specific_config(config);config_to_json();
  set_hardware_platform_detection_enabled(false);
  CHECK(get_raspberry_pi_generation()==-1);
  set_raspberry_pi_generation_override_for_test(4);
  CHECK(get_raspberry_pi_generation()==4&&platform_supports_gpio_clock_transmission());
  clear_raspberry_pi_generation_override_for_test();
  CHECK(get_raspberry_pi_generation()==-1);
  CHECK(get_compiled_backends().find("wtp")==std::string::npos);
  CHECK(!transmit_backend_is_compiled(TransmitBackendKind::WTP));
  CHECK(!get_public_config_json().contains("WTP")&&!get_public_config_json().contains("WTP Server"));
  testing::set_wtp_pi_private_state_path(root+"/revocation");
  std::string error;CHECK(start_wtp_pi_endpoint(config,&error));activate_wtp_pi_endpoint();poll_wtp_pi_endpoint();
  const auto observation=nlohmann::json::parse(wtp_pi_endpoint_status_json());
  CHECK(observation["enabled"]==false&&observation["listener_running"]==false&&observation["dns_sd_published"]==false);
  auto retired=config;retired.wtp_server.enabled=true;
  CHECK(!start_wtp_pi_endpoint(retired,&error)&&error.find("Migration")!=std::string::npos);
  bool refused=false;try{select_wtp_runtime(config.wtp);}catch(const std::exception&){refused=true;}
  CHECK(refused&&!wtp_runtime_selected());
  set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
  httplib::Server server;web_server_routes::register_control(server,[](httplib::Response&){});
  const int port=server.bind_to_any_port("127.0.0.1");CHECK(port>0);
  std::thread thread([&]{server.listen_after_bind();});
  struct Stop{httplib::Server& server;std::thread& thread;~Stop(){server.stop();thread.join();}}stop{server,thread};
  httplib::Client client("127.0.0.1",port);client.set_read_timeout(10,0);
  const std::string authority="127.0.0.1:"+std::to_string(port);
  const httplib::Headers read{{"Host",authority}};
  const auto before=client.Get("/api/control/v1/host/config",read);CHECK(before&&before->status==200);
  const auto revision=before->get_header_value("ETag");CHECK(!revision.empty());
  httplib::Headers write{{"Host",authority},{"Origin","http://"+authority},{"X-WsprryPico-Request","1"},{"If-Match",revision}};
  for(const auto body:{R"({"Operation":{"Transmit Backend":"wtp"}})",R"({"WTP":{"Hostname":"substituted.local"}})",R"({"WTP Server":{"Enabled":true}})"}){
   const auto response=client.Put("/api/control/v1/host/config",write,body,"application/json");CHECK(response&&response->status==400);
   CHECK(config.transmit_backend==TransmitBackendKind::SIMULATED&&wtp_settings_json(config.wtp)==legacy);
   CHECK(get_public_config_snapshot().second==revision);
  }
  const auto saved=client.Put("/api/control/v1/host/config",write,R"({"CW":{"Fade Shape":"raised_cosine","Fade In Ms":25,"Fade Out Ms":40,"Fade Slice Ms":7}})","application/json");
  CHECK(saved&&saved->status==200&&saved->get_header_value("ETag")!=revision);
  const auto stale=client.Put("/api/control/v1/host/config",write,"{}","application/json");CHECK(stale&&stale->status==412);
  write.erase("If-Match");CHECK(client.Put("/api/control/v1/host/config",write,"{}","application/json")->status==428);
  write.erase("Origin");CHECK(client.Put("/api/control/v1/host/config",write,"{}","application/json")->status==403);
  CHECK(wtp_settings_json(config.wtp)==legacy);
  RuntimeConfigCandidate restored;prepare_runtime_config_candidate(ini,restored);
  CHECK(restored.valid&&wtp_settings_json(restored.normalized_config.wtp)==legacy);
  CHECK(restored.normalized_config.cw_fade_in_ms==25&&restored.normalized_config.cw_fade_out_ms==40);
  CHECK(!wtp_runtime_selected());
  std::cout<<"PASS production host configuration, dormant settings preservation, native runtime, no listener/publication, and retired backend refusal\n";
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
