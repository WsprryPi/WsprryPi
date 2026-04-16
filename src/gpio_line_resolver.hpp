#ifndef GPIO_LINE_RESOLVER_HPP
#define GPIO_LINE_RESOLVER_HPP

#include "gpio_include.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct ResolvedGPIOLine
{
    int bcm = -1;
    std::filesystem::path chip_path;
    std::filesystem::path backing_device_path;
    std::string chip_name;
    std::string chip_label;
    gpiod::line::offset offset{};
    bool resolved_by_name = false;
    std::string kernel_name;
    std::string resolution_note;
    std::vector<std::filesystem::path> equivalent_chip_paths;
};

bool resolve_gpio_line(
    int bcm,
    ResolvedGPIOLine &resolved_line,
    std::string &error_message);

bool canonicalize_resolved_gpio_candidates(
    int bcm,
    bool resolved_by_name,
    const std::vector<ResolvedGPIOLine> &candidates,
    ResolvedGPIOLine &resolved_line,
    std::string &diagnostic_message);

std::string describe_resolved_gpio_line(const ResolvedGPIOLine &resolved_line);

#endif // GPIO_LINE_RESOLVER_HPP
