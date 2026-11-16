// SPDX-License-Identifier: MIT
// Actual main with an explicitly selected WTP role and an always-offline typed
// software transport. This fixture cannot open a device or network connection.
#include "wtp_endpoint/runtime.hpp"
#include "wtp_runtime_bridge.hpp"
#include "wtp_integration/target_runtime.hpp"
#include "web_server.hpp"
#include <atomic>
#include <string_view>
int pi_managed_production_main(int,char **);
namespace {
struct Offline:wsprrypi::wtp::ByteStream {
    std::atomic<unsigned> writes{};
    wsprrypi::wtp::IoResult write(std::span<const std::uint8_t>)override{++writes;return {wsprrypi::wtp::IoState::Closed,0};}
    wsprrypi::wtp::IoResult read(std::span<std::uint8_t>)override{return {wsprrypi::wtp::IoState::Closed,0};}
    void close()noexcept override{}
};
}
int main(int argc,char **argv){
    if(argc<4)return 2;
    bool selected=false;
    for(int i=2;i<argc;++i){
        const std::string_view option(argv[i]);
        if(option=="--backend"){
            if(selected||i+1>=argc||std::string_view(argv[++i])!="wtp")return 2;
            selected=true;
        }else if(option.starts_with("--backend=")){
            if(selected||option!="--backend=wtp")return 2;
            selected=true;
        }
    }
    if(!selected)return 2;
    Offline stream;wsprrypi::WtpSystemScheduleClock clock{[]{return true;}};
    try{
        wsprrypi::testing::set_wtp_pi_private_state_path(argv[1]);
        wsprrypi::testing::set_wtp_runtime_factory([&](WtpSettings settings,std::function<bool()> admission,std::shared_ptr<void> lease){
            if(settings.transport!="network_plain"||settings.hostname!="127.0.0.1"||settings.tcp_port!=31417||settings.device_id!=std::string(32,'b'))
                throw std::runtime_error("Offline fixture endpoint refused");
            auto value=std::make_shared<wsprrypi::WtpTargetRuntime>(settings,clock,stream,
                wsprrypi::wtp::SessionOptions{std::string(32,'1'),std::string(32,'2'),settings.device_id},[]{return false;},std::move(admission));
            value->output_lease=std::move(lease);value->output_lease_device=settings.device_id;return value;
        });
        webServer.bind_loopback_for_test();
    }catch(...){return 2;}
    const auto result=pi_managed_production_main(argc-1,argv+1);
    select_wtp_runtime(std::nullopt);
    return stream.writes?3:result;
}
