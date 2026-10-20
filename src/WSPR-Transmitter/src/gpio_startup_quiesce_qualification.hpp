#pragma once

#include "wspr_transmit_backend_rpi.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gpio_startup_quiesce_qualification
{
inline constexpr std::size_t kMapSize = 0x210000;

struct Options
{
    int gpio{0};
    int count{0};
};

struct RegisterSnapshot
{
    std::uint32_t dma_control_status{0};
    std::uint32_t dma_control_block_address{0};
    std::uint32_t dma_transfer_information{0};
    std::uint32_t dma_source_address{0};
    std::uint32_t dma_destination_address{0};
    std::uint32_t dma_transfer_length{0};
    std::uint32_t dma_stride{0};
    std::uint32_t dma_next_control_block{0};
    std::uint32_t dma_debug{0};
    std::uint32_t pwm_control{0};
    std::uint32_t pwm_dma_configuration{0};
    std::uint32_t gpclk0_control{0};
    std::uint32_t gpio_function_select{0};
};

struct AuditReport
{
    std::vector<std::string> trace;
    int opens{0};
    int maps{0};
    int unmaps{0};
    int closes{0};
    bool lifecycle_balanced{true};
    bool forbidden_operation{false};
    std::string error;
};

struct Result
{
    bool ok{false};
    std::string error;
    RegisterSnapshot before;
    RegisterSnapshot after_first;
    RegisterSnapshot after_second;
    int backend_calls{0};
    AuditReport audit;
};

bool parseOptions(int argc, char **argv, Options &options, std::string &error);

std::shared_ptr<IRpiStartupQuiesceAccess> makeAuditingAccess(
    std::shared_ptr<IRpiStartupQuiesceAccess> delegate,
    int gpio,
    AuditReport &report);

Result run(
    const Options &options,
    std::shared_ptr<IRpiStartupQuiesceAccess> delegate);
} // namespace gpio_startup_quiesce_qualification
