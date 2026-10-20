// SPDX-License-Identifier: MIT
#include "rp1_gpclk_linux_provider.hpp"
#include "rp1_gpclk_uapi.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
const char* observationName(std::uint32_t value)
{
    switch (value)
    {
    case RP1_GPCLK_OBSERVATION_FALSE: return "false";
    case RP1_GPCLK_OBSERVATION_TRUE: return "true";
    default: return "unknown";
    }
}
}

int main(int argc, char** argv)
{
    if (argc != 2 || (std::string(argv[1]) != "4" && std::string(argv[1]) != "20"))
    {
        std::cerr << "usage: rp1_gpclk_admin_probe 4|20\n";
        return EXIT_FAILURE;
    }
    const std::uint32_t route = std::string(argv[1]) == "4"
        ? RP1_GPCLK_ROUTE_GPIO4 : RP1_GPCLK_ROUTE_GPIO20;
    wsprrypi::Rp1GpclkPosixIo io;
    wsprrypi::Rp1GpclkLinuxProvider provider(io);
    std::string error;
    wsprrypi::Rp1GpclkPassiveSnapshot snapshot;
    if (!provider.passiveSnapshot(snapshot, error))
    {
        std::cerr << "passive_snapshot=failed\nerror=" << error << '\n';
        return EXIT_FAILURE;
    }
    if (snapshot.route != route)
    {
        std::cerr << "passive_snapshot=rejected\nerror=provider route mismatch\n";
        return EXIT_FAILURE;
    }
    for (const auto observation : {snapshot.cleanup_fault, snapshot.owner_present,
            snapshot.lease_present, snapshot.output_inhibited,
            snapshot.operational_ready,
            snapshot.gpio_safe, snapshot.clock_quiescent,
            snapshot.dma_quiescent, snapshot.stable})
    {
        if (observation == RP1_GPCLK_OBSERVATION_UNKNOWN)
        {
            std::cerr << "passive_snapshot=rejected\nerror=indeterminate safety observation\n";
            return EXIT_FAILURE;
        }
    }
    std::cout << "passive_snapshot=ok"
              << "\nroute=" << snapshot.route
              << "\ncompatibility_state=" << snapshot.compatibility_state
              << "\ncompatibility_reason=" << snapshot.compatibility_reason
              << "\ncapabilities=" << snapshot.capabilities
              << "\noperational_ready=" << observationName(snapshot.operational_ready)
              << "\nmodule_id=" << snapshot.module_id
              << "\nbuild_id=" << snapshot.build_id
              << "\ncompatibility_id=" << snapshot.compatibility_id
              << "\noperation_state=" << snapshot.operation_state
              << "\nterminal_reason=" << snapshot.terminal_reason
              << "\ndrain_state=" << snapshot.drain_state
              << "\ngeneration=" << snapshot.generation
              << "\nowner_present=" << observationName(snapshot.owner_present)
              << "\nlease_present=" << observationName(snapshot.lease_present)
              << "\noutput_inhibited=" << observationName(snapshot.output_inhibited)
              << "\ndma_chunk_duration_ns=" << snapshot.dma_chunk_duration_ns
              << "\nmax_request_duration_ns=" << snapshot.max_request_duration_ns
              << "\nstable=" << observationName(snapshot.stable)
              << "\ncleanup_fault=" << observationName(snapshot.cleanup_fault)
              << "\ngpio_safe=" << observationName(snapshot.gpio_safe)
              << "\nclock_quiescent=" << observationName(snapshot.clock_quiescent)
              << "\ndma_quiescent=" << observationName(snapshot.dma_quiescent)
              << "\nread_only=true\nlease_token_exposed=false\n";
    return EXIT_SUCCESS;
}
