#ifndef GPIO_TEST_UTILS_HPP
#define GPIO_TEST_UTILS_HPP

#include "gpio_include.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gpio_test
{
    struct ChipEntry
    {
        std::filesystem::path path;
        std::string name;
        std::string label;
        std::size_t num_lines = 0;
    };

    struct LineResolution
    {
        int bcm = -1;
        GpioLineOffset offset{};
        bool resolved_by_name = false;
        std::string kernel_name;
    };

    struct SelectedChip
    {
        std::filesystem::path path;
        std::string name;
        std::string label;
        std::vector<LineResolution> lines;
        bool used_offset_fallback = false;
        bool auto_selected = false;
    };

    inline std::string bool_text(bool value)
    {
        return value ? "true" : "false";
    }

    inline std::string line_value_text(gpiod::line::value value)
    {
        return value == gpiod::line::value::ACTIVE ? "1" : "0";
    }

    inline std::string timestamp_now()
    {
        using clock = std::chrono::system_clock;
        const auto now = clock::now();
        const auto tt = clock::to_time_t(now);
        const auto micros =
            std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()) %
            std::chrono::seconds(1);

        std::tm tm{};
        localtime_r(&tt, &tm);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%F %T")
            << '.'
            << std::setfill('0')
            << std::setw(6)
            << micros.count();
        return oss.str();
    }

    inline std::string normalize_chip_argument(const std::string &chip_arg)
    {
        if (chip_arg.empty())
        {
            throw std::invalid_argument("GPIO chip argument is empty.");
        }

        if (chip_arg[0] == '/')
        {
            return chip_arg;
        }

        return "/dev/" + chip_arg;
    }

    inline std::vector<ChipEntry> enumerate_gpiochips()
    {
        std::vector<ChipEntry> entries;

        const std::filesystem::path dev_dir{"/dev"};
        for (const auto &entry : std::filesystem::directory_iterator(dev_dir))
        {
            if (!entry.is_character_file() && !entry.is_symlink())
            {
                continue;
            }

            const std::string filename = entry.path().filename().string();
            if (filename.rfind("gpiochip", 0) != 0)
            {
                continue;
            }

            try
            {
                gpiod::chip chip(entry.path());
                const gpiod::chip_info info = chip.get_info();
                entries.push_back(ChipEntry{
                    entry.path(),
                    info.name(),
                    info.label(),
                    info.num_lines()});
            }
            catch (const std::exception &e)
            {
                std::cerr << "WARN: failed to open " << entry.path()
                          << ": " << e.what() << '\n';
            }
        }

        std::sort(
            entries.begin(),
            entries.end(),
            [](const ChipEntry &lhs, const ChipEntry &rhs)
            {
                return lhs.path < rhs.path;
            });

        return entries;
    }

    inline void print_chip_entries(const std::vector<ChipEntry> &entries)
    {
        std::cout << "Available GPIO chips:\n";
        if (entries.empty())
        {
            std::cout << "  none found under /dev/gpiochip*\n";
            return;
        }

        for (const ChipEntry &entry : entries)
        {
            std::cout << "  " << entry.path
                      << " name=" << entry.name
                      << " label=\"" << entry.label << "\""
                      << " lines=" << entry.num_lines
                      << '\n';
        }
    }

    inline std::optional<LineResolution> try_resolve_line(
        gpiod::chip &chip,
        int bcm,
        bool allow_offset_fallback,
        bool &used_offset_fallback)
    {
        const std::string expected_name = "GPIO" + std::to_string(bcm);
        const int named_offset = chip.get_line_offset_from_name(expected_name);
        if (named_offset >= 0)
        {
            return LineResolution{
                bcm,
                gpio_line_offset(static_cast<unsigned int>(named_offset)),
                true,
                expected_name};
        }

        const gpiod::chip_info info = chip.get_info();
        if (!allow_offset_fallback ||
            bcm < 0 ||
            static_cast<std::size_t>(bcm) >= info.num_lines())
        {
            return std::nullopt;
        }

        used_offset_fallback = true;
        std::string kernel_name;
        try
        {
            kernel_name = chip.get_line_info(
                                  gpio_line_offset(
                                      static_cast<unsigned int>(bcm)))
                              .name();
        }
        catch (...)
        {
            kernel_name.clear();
        }

        return LineResolution{
            bcm,
            gpio_line_offset(static_cast<unsigned int>(bcm)),
            false,
            kernel_name};
    }

    inline SelectedChip resolve_chip_and_lines(
        const std::vector<int> &bcm_lines,
        const std::optional<std::string> &chip_arg)
    {
        if (bcm_lines.empty())
        {
            throw std::invalid_argument("No BCM lines were supplied.");
        }

        const std::vector<ChipEntry> entries = enumerate_gpiochips();
        print_chip_entries(entries);
        if (entries.empty())
        {
            throw std::runtime_error("No GPIO chips found.");
        }

        auto resolve_on_entry =
            [&](const ChipEntry &entry, bool auto_selected) -> std::optional<SelectedChip>
        {
            gpiod::chip chip(entry.path);
            SelectedChip selected;
            selected.path = entry.path;
            selected.name = entry.name;
            selected.label = entry.label;
            selected.auto_selected = auto_selected;

            for (int bcm : bcm_lines)
            {
                bool used_offset_fallback = false;
                std::optional<LineResolution> resolution =
                    try_resolve_line(chip, bcm, true, used_offset_fallback);
                if (!resolution.has_value())
                {
                    return std::nullopt;
                }

                selected.used_offset_fallback =
                    selected.used_offset_fallback || used_offset_fallback;
                selected.lines.push_back(*resolution);
            }

            return selected;
        };

        if (chip_arg.has_value())
        {
            const std::filesystem::path chip_path{
                normalize_chip_argument(*chip_arg)};
            auto it =
                std::find_if(
                    entries.begin(),
                    entries.end(),
                    [&](const ChipEntry &entry)
                    {
                        return entry.path == chip_path;
                    });
            if (it == entries.end())
            {
                throw std::runtime_error(
                    "Requested chip " + chip_path.string() +
                    " was not found in /dev.");
            }

            std::optional<SelectedChip> selected = resolve_on_entry(*it, false);
            if (!selected.has_value())
            {
                throw std::runtime_error(
                    "Requested chip " + chip_path.string() +
                    " does not expose all requested BCM lines.");
            }
            return *selected;
        }

        std::vector<SelectedChip> by_name;
        std::vector<SelectedChip> by_fallback;

        for (const ChipEntry &entry : entries)
        {
            try
            {
                std::optional<SelectedChip> selected = resolve_on_entry(entry, true);
                if (!selected.has_value())
                {
                    continue;
                }

                if (selected->used_offset_fallback)
                {
                    by_fallback.push_back(*selected);
                }
                else
                {
                    by_name.push_back(*selected);
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "WARN: skipping " << entry.path
                          << ": " << e.what() << '\n';
            }
        }

        if (!by_name.empty())
        {
            if (by_name.size() > 1U)
            {
                std::cerr
                    << "WARN: multiple chips expose the requested GPIO line names; "
                    << "choosing " << by_name.front().path
                    << ". Use --chip to override.\n";
            }
            return by_name.front();
        }

        if (!by_fallback.empty())
        {
            std::cerr
                << "WARN: no chip exposed GPIO line names for all requested BCM lines; "
                << "falling back to raw offsets on " << by_fallback.front().path
                << ". On Raspberry Pi 5 or other multi-chip layouts this may be wrong. "
                << "Use --chip explicitly if header pins do not match.\n";
            return by_fallback.front();
        }

        throw std::runtime_error(
            "Unable to resolve the requested BCM lines on any detected GPIO chip.");
    }

    inline void print_selected_chip(const SelectedChip &selected)
    {
        std::cout << "Selected chip: " << selected.path
                  << " name=" << selected.name
                  << " label=\"" << selected.label << "\""
                  << " auto_selected=" << bool_text(selected.auto_selected)
                  << " used_offset_fallback=" << bool_text(selected.used_offset_fallback)
                  << '\n';

        for (const LineResolution &line : selected.lines)
        {
            std::cout << "  BCM " << line.bcm
                      << " -> offset " << static_cast<unsigned int>(line.offset)
                      << " resolved_by_name=" << bool_text(line.resolved_by_name);
            if (!line.kernel_name.empty())
            {
                std::cout << " kernel_name=\"" << line.kernel_name << "\"";
            }
            std::cout << '\n';
        }
    }

    inline std::vector<int> parse_bcm_list(const std::vector<std::string> &tokens)
    {
        std::vector<int> lines;

        for (const std::string &token : tokens)
        {
            if (token.empty())
            {
                continue;
            }

            std::size_t start = 0U;
            while (start < token.size())
            {
                const std::size_t comma = token.find(',', start);
                const std::string piece =
                    token.substr(start, comma == std::string::npos
                                            ? std::string::npos
                                            : comma - start);
                if (!piece.empty())
                {
                    int bcm = 0;
                    try
                    {
                        bcm = std::stoi(piece);
                    }
                    catch (const std::exception &)
                    {
                        throw std::invalid_argument(
                            "Invalid BCM line '" + piece + "'.");
                    }

                    if (bcm < 0 || bcm > 27)
                    {
                        throw std::invalid_argument(
                            "BCM line out of range: " + piece + ".");
                    }

                    lines.push_back(bcm);
                }

                if (comma == std::string::npos)
                {
                    break;
                }
                start = comma + 1U;
            }
        }

        return lines;
    }
} // namespace gpio_test

#endif // GPIO_TEST_UTILS_HPP
