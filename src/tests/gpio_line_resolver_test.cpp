#include "gpio_line_resolver.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    void require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }

    ResolvedGPIOLine make_candidate(
        int bcm,
        const std::string &chip_path,
        const std::string &backing_device_path,
        unsigned int offset,
        bool resolved_by_name)
    {
        ResolvedGPIOLine candidate;
        candidate.bcm = bcm;
        candidate.chip_path = chip_path;
        candidate.backing_device_path = backing_device_path;
        candidate.offset = gpiod::line::offset(offset);
        candidate.resolved_by_name = resolved_by_name;
        candidate.kernel_name = resolved_by_name ? ("GPIO" + std::to_string(bcm)) : "";
        return candidate;
    }
} // namespace

int main()
{
    {
        ResolvedGPIOLine resolved;
        std::string diagnostic;
        const std::vector<ResolvedGPIOLine> candidates{
            make_candidate(
                18,
                "/dev/gpiochip7",
                "/sys/devices/platform/soc/fe200000.gpio",
                18U,
                true)};

        require(
            canonicalize_resolved_gpio_candidates(18, true, candidates, resolved, diagnostic),
            "single named candidate must resolve");
        require(diagnostic.empty(), "single named candidate must not report ambiguity");
        require(
            resolved.chip_path == "/dev/gpiochip7",
            "single named candidate must preserve chip path");
        require(
            resolved.equivalent_chip_paths.size() == 1U &&
                resolved.equivalent_chip_paths.front() == "/dev/gpiochip7",
            "single named candidate must track its canonical chip view");
    }

    {
        ResolvedGPIOLine resolved;
        std::string diagnostic;
        const std::vector<ResolvedGPIOLine> candidates{
            make_candidate(
                18,
                "/dev/gpiochip4",
                "/sys/devices/platform/soc/fe200000.gpio",
                18U,
                true),
            make_candidate(
                18,
                "/dev/gpiochip0",
                "/sys/devices/platform/soc/fe200000.gpio",
                18U,
                true)};

        require(
            canonicalize_resolved_gpio_candidates(18, true, candidates, resolved, diagnostic),
            "duplicate named candidates on one backing device must collapse");
        require(diagnostic.empty(), "collapsed duplicate named candidates must not be ambiguous");
        require(
            resolved.chip_path == "/dev/gpiochip0",
            "canonical named candidate must prefer the lowest gpiochip number");
        require(
            resolved.equivalent_chip_paths.size() == 2U &&
                resolved.equivalent_chip_paths.front() == "/dev/gpiochip0" &&
                resolved.equivalent_chip_paths.back() == "/dev/gpiochip4",
            "collapsed named candidates must retain equivalent chip metadata");
        require(
            resolved.resolution_note.find("/dev/gpiochip4") != std::string::npos,
            "collapsed named candidates must mention alternate views in the note");
    }

    {
        ResolvedGPIOLine resolved;
        std::string diagnostic;
        const std::vector<ResolvedGPIOLine> candidates{
            make_candidate(
                19,
                "/dev/gpiochip4",
                "/sys/devices/platform/soc/fe200000.gpio",
                19U,
                false),
            make_candidate(
                19,
                "/dev/gpiochip0",
                "/sys/devices/platform/soc/fe200000.gpio",
                19U,
                false)};

        require(
            canonicalize_resolved_gpio_candidates(19, false, candidates, resolved, diagnostic),
            "duplicate raw-offset candidates on one backing device must collapse");
        require(
            resolved.chip_path == "/dev/gpiochip0",
            "canonical raw-offset candidate must prefer the lowest gpiochip number");
        require(
            resolved.equivalent_chip_paths.size() == 2U,
            "collapsed raw-offset candidates must retain equivalent chip metadata");
    }

    {
        ResolvedGPIOLine resolved;
        std::string diagnostic;
        const std::vector<ResolvedGPIOLine> candidates{
            make_candidate(
                18,
                "/dev/gpiochip0",
                "/sys/devices/platform/soc/fe200000.gpio",
                18U,
                true),
            make_candidate(
                18,
                "/dev/gpiochip5",
                "/sys/devices/platform/soc/fe300000.gpio",
                18U,
                true)};

        require(
            !canonicalize_resolved_gpio_candidates(18, true, candidates, resolved, diagnostic),
            "distinct named backing devices must remain ambiguous");
        require(
            diagnostic.find("/dev/gpiochip0") != std::string::npos &&
                diagnostic.find("/dev/gpiochip5") != std::string::npos,
            "named ambiguity must list canonical chip paths");
        require(
            diagnostic.find("multiple backing devices") != std::string::npos,
            "named ambiguity must explain that backing devices differ");
    }

    {
        ResolvedGPIOLine resolved;
        std::string diagnostic;
        const std::vector<ResolvedGPIOLine> candidates{
            make_candidate(
                19,
                "/dev/gpiochip0",
                "/sys/devices/platform/soc/fe200000.gpio",
                19U,
                false),
            make_candidate(
                19,
                "/dev/gpiochip5",
                "/sys/devices/platform/soc/fe300000.gpio",
                19U,
                false)};

        require(
            !canonicalize_resolved_gpio_candidates(19, false, candidates, resolved, diagnostic),
            "distinct raw-offset backing devices must remain ambiguous");
        require(
            diagnostic.find("Refusing ambiguous raw-offset mapping.") != std::string::npos,
            "raw-offset ambiguity must preserve the refusal wording");
    }

    std::cout << "GPIO line resolver regression tests passed." << std::endl;
    return EXIT_SUCCESS;
}
