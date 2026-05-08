#include "gpio_test_utils.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
    std::atomic<bool> g_running{true};

    void signal_handler(int)
    {
        g_running.store(false, std::memory_order_release);
    }

    void print_usage(const char *argv0)
    {
        std::cout
            << "Usage: " << argv0 << " [--chip gpiochipX] [--poll-ms N] BCM [BCM ...]\n"
            << "   or: " << argv0 << " [--chip gpiochipX] [--poll-ms N] BCM,BCM,...\n"
            << "BCM numbering is used. Wire monitored inputs to candidate output GPIOs\n"
            << "through approximately 1 kOhm resistors and run this tool while WsprryPi is active.\n"
            << "Examples:\n"
            << "  " << argv0 << " 22 23 24\n"
            << "  " << argv0 << " --chip gpiochip4 --poll-ms 10 22,23,24\n";
    }
} // namespace

int main(int argc, char **argv)
{
    try
    {
        std::optional<std::string> chip_arg;
        int poll_ms = 20;
        std::vector<std::string> bcm_tokens;

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

            if (arg == "--poll-ms")
            {
                if (i + 1 >= argc)
                {
                    throw std::invalid_argument("--poll-ms requires an argument.");
                }
                poll_ms = std::stoi(argv[++i]);
                if (poll_ms <= 0)
                {
                    throw std::invalid_argument("--poll-ms must be positive.");
                }
                continue;
            }

            bcm_tokens.push_back(arg);
        }

        if (bcm_tokens.empty())
        {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }

        const std::vector<int> bcm_lines = gpio_test::parse_bcm_list(bcm_tokens);
        if (bcm_lines.empty())
        {
            throw std::invalid_argument("No BCM input lines were supplied.");
        }

        const gpio_test::SelectedChip selected =
            gpio_test::resolve_chip_and_lines(bcm_lines, chip_arg);
        gpio_test::print_selected_chip(selected);

        std::vector<GpioLineOffset> input_offsets;
        input_offsets.reserve(selected.lines.size());
        for (const auto &line : selected.lines)
        {
            input_offsets.push_back(line.offset);
        }

        gpiod::chip chip(selected.path);
        gpiod::line_settings input_settings;
        input_settings.set_direction(gpiod::line::direction::INPUT);

        auto input_builder = chip.prepare_request();
        input_builder.set_consumer("band_gpio_live_monitor");
        input_builder.add_line_settings(input_offsets, input_settings);
        gpiod::line_request input_request = input_builder.do_request();

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        std::vector<gpiod::line::value> previous =
            input_request.get_values(input_offsets);

        std::cout << gpio_test::timestamp_now()
                  << " monitor started on " << selected.path
                  << ". Press Ctrl-C to stop.\n";
        for (std::size_t i = 0; i < selected.lines.size(); ++i)
        {
            std::cout << "  BCM " << selected.lines[i].bcm
                      << " offset " << static_cast<unsigned int>(selected.lines[i].offset)
                      << " initial=" << gpio_test::line_value_text(previous[i])
                      << '\n';
        }

        while (g_running.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));

            const std::vector<gpiod::line::value> current =
                input_request.get_values(input_offsets);

            for (std::size_t i = 0; i < current.size(); ++i)
            {
                if (current[i] == previous[i])
                {
                    continue;
                }

                std::cout << gpio_test::timestamp_now()
                          << " chip=" << selected.path
                          << " bcm=" << selected.lines[i].bcm
                          << " offset=" << static_cast<unsigned int>(selected.lines[i].offset)
                          << " value=" << gpio_test::line_value_text(current[i])
                          << '\n';
            }

            previous = current;
        }

        std::cout << gpio_test::timestamp_now() << " monitor stopped.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
