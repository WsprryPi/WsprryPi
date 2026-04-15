#include "gpio_test_utils.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    struct PairSpec
    {
        int output_bcm = -1;
        int input_bcm = -1;
    };

    void print_usage(const char *argv0)
    {
        std::cout
            << "Usage: " << argv0 << " [--chip gpiochipX] [--settle-ms N] OUT:IN [OUT:IN ...]\n"
            << "BCM numbering is used. Wire each output BCM line to the matching input BCM line\n"
            << "through an approximately 1 kOhm resistor.\n"
            << "Examples:\n"
            << "  " << argv0 << " 17:22\n"
            << "  " << argv0 << " --chip gpiochip4 --settle-ms 50 17:22 27:23\n";
    }

    PairSpec parse_pair(const std::string &token)
    {
        const std::size_t colon = token.find(':');
        if (colon == std::string::npos)
        {
            throw std::invalid_argument(
                "Invalid pair '" + token + "'. Expected OUT:IN.");
        }

        const std::string out_text = token.substr(0, colon);
        const std::string in_text = token.substr(colon + 1U);
        if (out_text.empty() || in_text.empty())
        {
            throw std::invalid_argument(
                "Invalid pair '" + token + "'. Expected OUT:IN.");
        }

        PairSpec pair;
        pair.output_bcm = std::stoi(out_text);
        pair.input_bcm = std::stoi(in_text);

        if (pair.output_bcm < 0 || pair.output_bcm > 27 ||
            pair.input_bcm < 0 || pair.input_bcm > 27)
        {
            throw std::invalid_argument(
                "BCM lines in pair '" + token + "' must be between 0 and 27.");
        }

        return pair;
    }

    void ensure_unique_offsets(
        const std::vector<gpiod::line::offset> &offsets,
        const std::string &label)
    {
        std::vector<unsigned int> raw;
        raw.reserve(offsets.size());
        for (const auto offset : offsets)
        {
            raw.push_back(static_cast<unsigned int>(offset));
        }

        std::sort(raw.begin(), raw.end());
        for (std::size_t i = 1; i < raw.size(); ++i)
        {
            if (raw[i] == raw[i - 1U])
            {
                throw std::runtime_error(
                    "Duplicate " + label + " line offset " +
                    std::to_string(raw[i]) + " is not supported.");
            }
        }
    }

    void print_pair_mapping(
        const PairSpec &pair,
        const gpio_test::LineResolution &out_line,
        const gpio_test::LineResolution &in_line)
    {
        std::cout << "Pair BCM " << pair.output_bcm
                  << " -> BCM " << pair.input_bcm
                  << " maps to output offset "
                  << static_cast<unsigned int>(out_line.offset)
                  << " and input offset "
                  << static_cast<unsigned int>(in_line.offset)
                  << '\n';
    }
} // namespace

int main(int argc, char **argv)
{
    try
    {
        std::optional<std::string> chip_arg;
        int settle_ms = 20;
        std::vector<PairSpec> pairs;

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--help" || arg == "-h")
            {
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            }

            if (arg == "--chip")
            {
                if (i + 1 >= argc)
                {
                    throw std::invalid_argument("--chip requires an argument.");
                }
                chip_arg = argv[++i];
                continue;
            }

            if (arg == "--settle-ms")
            {
                if (i + 1 >= argc)
                {
                    throw std::invalid_argument("--settle-ms requires an argument.");
                }
                settle_ms = std::stoi(argv[++i]);
                if (settle_ms < 0)
                {
                    throw std::invalid_argument("--settle-ms must be non-negative.");
                }
                continue;
            }

            pairs.push_back(parse_pair(arg));
        }

        if (pairs.empty())
        {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }

        std::vector<int> requested_lines;
        requested_lines.reserve(pairs.size() * 2U);
        for (const PairSpec &pair : pairs)
        {
            requested_lines.push_back(pair.output_bcm);
            requested_lines.push_back(pair.input_bcm);
        }

        const gpio_test::SelectedChip selected =
            gpio_test::resolve_chip_and_lines(requested_lines, chip_arg);
        gpio_test::print_selected_chip(selected);

        std::vector<gpio_test::LineResolution> output_lines;
        std::vector<gpio_test::LineResolution> input_lines;
        output_lines.reserve(pairs.size());
        input_lines.reserve(pairs.size());
        for (std::size_t i = 0; i < pairs.size(); ++i)
        {
            output_lines.push_back(selected.lines[i * 2U]);
            input_lines.push_back(selected.lines[i * 2U + 1U]);
            print_pair_mapping(pairs[i], output_lines.back(), input_lines.back());
        }

        std::vector<gpiod::line::offset> output_offsets;
        std::vector<gpiod::line::offset> input_offsets;
        output_offsets.reserve(output_lines.size());
        input_offsets.reserve(input_lines.size());
        for (const auto &line : output_lines)
        {
            output_offsets.push_back(line.offset);
        }
        for (const auto &line : input_lines)
        {
            input_offsets.push_back(line.offset);
        }

        ensure_unique_offsets(output_offsets, "output");
        ensure_unique_offsets(input_offsets, "input");

        gpiod::chip chip(selected.path);

        gpiod::line_settings output_settings;
        output_settings.set_direction(gpiod::line::direction::OUTPUT);
        output_settings.set_output_value(gpiod::line::value::INACTIVE);

        auto output_builder = chip.prepare_request();
        output_builder.set_consumer("gpio_loopback_test_out");
        output_builder.add_line_settings(output_offsets, output_settings);
        gpiod::line_request output_request = output_builder.do_request();

        gpiod::line_settings input_settings;
        input_settings.set_direction(gpiod::line::direction::INPUT);

        auto input_builder = chip.prepare_request();
        input_builder.set_consumer("gpio_loopback_test_in");
        input_builder.add_line_settings(input_offsets, input_settings);
        gpiod::line_request input_request = input_builder.do_request();

        const std::vector<gpiod::line::value> phases = {
            gpiod::line::value::INACTIVE,
            gpiod::line::value::ACTIVE,
            gpiod::line::value::INACTIVE};

        bool mismatch = false;
        for (const gpiod::line::value phase : phases)
        {
            std::vector<gpiod::line::value> output_values(
                output_offsets.size(),
                phase);
            output_request.set_values(output_offsets, output_values);

            std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));

            const std::vector<gpiod::line::value> input_values =
                input_request.get_values(input_offsets);

            for (std::size_t i = 0; i < pairs.size(); ++i)
            {
                const bool pair_match = input_values[i] == phase;
                std::cout
                    << gpio_test::timestamp_now()
                    << " chip=" << selected.path
                    << " requested_output_bcm=" << pairs[i].output_bcm
                    << " requested_input_bcm=" << pairs[i].input_bcm
                    << " output_offset=" << static_cast<unsigned int>(output_offsets[i])
                    << " input_offset=" << static_cast<unsigned int>(input_offsets[i])
                    << " written=" << gpio_test::line_value_text(phase)
                    << " read=" << gpio_test::line_value_text(input_values[i])
                    << " match=" << gpio_test::bool_text(pair_match)
                    << '\n';
                mismatch = mismatch || !pair_match;
            }
        }

        if (mismatch)
        {
            std::cerr << "gpio_loopback_test detected at least one mismatch.\n";
            return EXIT_FAILURE;
        }

        std::cout << "gpio_loopback_test passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
