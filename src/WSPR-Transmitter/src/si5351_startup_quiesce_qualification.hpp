#pragma once

#include "si5351_device.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace si5351_startup_quiesce_qualification
{
struct Options
{
    std::string device_path;
    int bus{0};
    std::uint8_t address{0};
};

struct Result
{
    bool ok{false};
    std::string error;
    std::uint8_t before{0};
    std::uint8_t after_first{0};
    std::uint8_t after_second{0};
    bool first_quiesce_ok{false};
    bool second_quiesce_ok{false};
    std::string first_quiesce_error;
    std::string second_quiesce_error;
    std::vector<std::string> trace;
};

bool parse_options(int argc, char** argv, Options& options, std::string& error);
Result run(const Options& options,
           std::shared_ptr<Si5351Device::I2CAdapter> system_adapter);
std::shared_ptr<Si5351Device::I2CAdapter> make_system_adapter();
} // namespace si5351_startup_quiesce_qualification
