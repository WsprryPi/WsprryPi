#include "config_handler.hpp"
#include "gpio_output.hpp"
#include "scheduling.hpp"
#include "WSPR-Transmitter/src/wspr_transmit.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

ArgParserConfig cfg_for(EnableOnBootBehavior policy)
{
    init_default_config();
    auto cfg = config;
    // Exercise the non-WSPR launch gate with an explicit hardware-free profile.
    cfg.mode = ModeType::QRSS;
    cfg.transmit_backend = TransmitBackendKind::SIMULATED;
    cfg.schedule_repeat_minutes = 0;
    cfg.use_ini = true;
    cfg.transmit = true;
    cfg.enable_on_boot = policy;
    cfg.use_led = true;
    cfg.led_pin = 5;
    config = cfg;
    return cfg;
}

void reset()
{
    reset_startup_quiesce_for_test();
    GPIOOutput::setTestMode(true);
    set_scheduler_execution_suppressed_for_test(true);
    reset_tx_led_request_counts_for_test();
}

std::string read_file(const char* path)
{
    std::ifstream input(path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void test_generic_startup_gate()
{
    for (auto policy : {EnableOnBootBehavior::Never,
                        EnableOnBootBehavior::Follow,
                        EnableOnBootBehavior::Always})
    {
        reset();
        int calls = 0;
        set_startup_quiesce_invoker_for_test([&calls] {
            ++calls;
            return wsprrypi::StartupQuiesceResult{true, {}};
        });
        auto cfg = cfg_for(policy);
        require(run_startup_quiesce_gate_for_test(cfg), "success gate");
        require(calls == 1, "one quiesce per policy");
        require(!startup_quiesce_inhibited_for_test(), "success not inhibited");
        require(tx_led_deassert_request_count_for_test() > 0,
                "outputs deasserted");
    }

    reset();
    int calls = 0;
    set_startup_quiesce_invoker_for_test([&calls] {
        ++calls;
        return wsprrypi::StartupQuiesceResult{false, "fake quiesce failure"};
    });
    auto cfg = cfg_for(EnableOnBootBehavior::Follow);
    const bool transmit = cfg.transmit;
    const auto policy = cfg.enable_on_boot;
    require(!run_startup_quiesce_gate_for_test(cfg), "failure gate");
    require(calls == 1, "one failed quiesce");
    require(startup_quiesce_inhibited_for_test(), "failure inhibited");
    require(startup_quiesce_error_for_test() == "fake quiesce failure",
            "error retained");
    require(cfg.transmit == transmit && cfg.enable_on_boot == policy,
            "policy preserved");
    require(start_non_wspr_transmission_now_for_test(cfg),
            "blocked launch returns safely");
    require(!current_controller_request_for_test().has_value(),
            "manual launch must not configure transmitter");
    require(startup_quiesce_inhibited_for_test(), "latch retained");
}

void test_injected_simulator_failure_reaches_application_gate()
{
    reset();
    constexpr const char* trace_path =
        "/tmp/wsprrypi-startup-quiesce-parent-trace.json";
    WsprTransmitter transmitter;
    WsprTransmitter::SimulatedRuntimeConfig simulation;
    simulation.trace_path = trace_path;
    simulation.fail_startup_quiesce = true;
    transmitter.selectBackend(wsprrypi::BackendKind::SIMULATED, {}, simulation);
    set_startup_quiesce_invoker_for_test([&transmitter] {
        return transmitter.quiesceForStartup();
    });

    auto cfg = cfg_for(EnableOnBootBehavior::Follow);
    require(!run_startup_quiesce_gate_for_test(cfg),
            "injected simulator failure must fail application gate");
    require(startup_quiesce_inhibited_for_test(),
            "injected simulator failure must latch inhibition");
    require(startup_quiesce_error_for_test() ==
                "Injected simulated startup quiesce failure.",
            "injected simulator error must reach application gate");
    require(start_non_wspr_transmission_now_for_test(cfg),
            "inhibited application launch must return safely");
    require(!current_controller_request_for_test().has_value(),
            "inhibited application launch must not prepare a request");

    const auto trace = read_file(trace_path);
    require(trace.find("\"kind\":\"startup_quiesce_failure\"") !=
                std::string::npos,
            "application path must retain simulator failure trace");
    require(trace.find("\"detail\":\"injected\"") != std::string::npos,
            "application path must identify injected failure");
}
} // namespace

int main()
{
    test_generic_startup_gate();
    test_injected_simulator_failure_reaches_application_gate();
    reset_startup_quiesce_for_test();
    GPIOOutput::setTestMode(false);
    set_scheduler_execution_suppressed_for_test(false);
    std::cout << "startup_quiesce_parent_test passed\n";
}
