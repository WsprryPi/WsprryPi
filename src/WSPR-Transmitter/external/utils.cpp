#include "utils.hpp"

#include <array>
#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <sys/wait.h>

static constexpr auto log_tag_chars = make_log_tag_chars("Utils");
static constexpr std::string_view log_tag = as_string_view(log_tag_chars);

/**
 * @brief Get the PPM (frequency) value from `chronyc tracking`
 *
 * Executes the `chronyc tracking` command and parses the "Frequency" line,
 * which reflects the estimated drift rate in parts per million (PPM).
 *
 * @return The frequency in PPM as a double
 * @throws std::runtime_error if the command fails or parsing fails
 */
double get_ppm_from_chronyc()
{
    const char *cmd = "chronyc tracking 2>&1";
    std::array<char, 256> buffer{};
    std::string result;

    // Open pipe to chronyc
    FILE *pipe = popen(cmd, "r");
    if (!pipe)
    {
        throw std::runtime_error(
            std::string("Failed to run chronyc tracking: ") +
            std::strerror(errno));
    }

    // Accumulate all output
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
    {
        result += buffer.data();
    }

    // Close pipe and inspect exit status
    int status = pclose(pipe);
    if (status == -1)
    {
        if (errno != ECHILD)
        {
            std::ostringstream err;
            err << "pclose() failed: errno=" << errno
                << " (" << std::strerror(errno) << ")\nOutput:\n"
                << result;
            throw std::runtime_error(err.str());
        }
        // Ignore ECHILD per POSIX
    }
    else if (WIFEXITED(status))
    {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 127)
        {
            throw std::runtime_error(
                "chronyc not found; please install chrony");
        }
        if (exit_code != 0)
        {
            std::ostringstream err;
            err << "chronyc tracking exited with status "
                << exit_code << "\nOutput:\n"
                << result;
            throw std::runtime_error(err.str());
        }
    }
    else if (WIFSIGNALED(status))
    {
        int sig = WTERMSIG(status);
        std::ostringstream err;
        err << "chronyc tracking killed by signal "
            << sig << "\nOutput:\n"
            << result;
        throw std::runtime_error(err.str());
    }

    // Scan for the Frequency line
    std::istringstream iss(result);
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.find("Frequency") != std::string::npos)
        {
            std::istringstream ls(line);
            std::string label, colon, units;
            double ppm;
            ls >> label >> colon >> ppm >> units;
            std::cout << log_tag
                      << "PPM set to: "
                      << std::fixed
                      << std::setprecision(3)
                      << ppm
                      << std::endl;
            return ppm;
        }
    }

    throw std::runtime_error(
        "Frequency line not found in chronyc tracking output");
}
