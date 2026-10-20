#include "support_bundle_intake_retrieval.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace {

std::string value_after(const std::vector<std::string> &arguments, const std::string &option) {
    const auto found = std::find(arguments.begin(), arguments.end(), option);
    if (found == arguments.end() || std::next(found) == arguments.end()) return {};
    return *std::next(found);
}

bool valid_contract(const std::vector<std::string> &arguments) {
    const std::vector<std::string> expected_environment = {
        "LANG=C", "LC_ALL=C", "HOME=/nonexistent", "PATH=/usr/bin:/bin"};
    std::vector<std::string> environment;
    for (char **entry = environ; *entry; ++entry) environment.emplace_back(*entry);
    if (environment != expected_environment || arguments.size() != 27) return false;
    const auto url = value_after(arguments, "--url");
    const auto maximum = url == kWsprryPiIntakeManifestUrl ? "16384" : "2048";
    if (url != kWsprryPiIntakeManifestUrl && url != kWsprryPiIntakeSignatureUrl) return false;
    const std::vector<std::string> expected = {
        arguments[0], "--disable", "--silent", "--show-error", "--fail",
        "--ipv4",
        "--proto", "=https", "--proto-redir", "=https", "--max-redirs", "0",
        "--connect-timeout", "0.100", "--max-time", "0.800", "--max-filesize", maximum,
        "--http1.1", "--request", "GET", "--output", "-", "--write-out",
        "\n%{http_code}", "--url", url};
    return arguments == expected;
}

} // namespace

int main(int argc, char **argv) {
    std::vector<std::string> arguments(argv, argv + argc);
    if (!valid_contract(arguments)) return 64;
    const auto scenario = std::filesystem::path(argv[0]).filename().string();
    const bool manifest = value_after(arguments, "--url") == kWsprryPiIntakeManifestUrl;
    if ((scenario == "manifest-empty" && manifest) ||
        (scenario == "signature-empty" && !manifest)) {
        std::cout << "\n200";
        return 0;
    }
    if ((scenario == "manifest-timeout" && manifest) ||
        (scenario == "signature-timeout" && !manifest)) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        return 0;
    }
    if (scenario == "signature-signal" && !manifest) {
        raise(SIGKILL);
    }
    if (scenario == "signature-descendant" && !manifest) {
        const auto descendant = fork();
        if (descendant < 0) return 70;
        if (descendant == 0) {
            signal(SIGTERM, SIG_IGN);
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            std::ofstream(argv[0] + std::string(".survived"))
                << "descendant survived cleanup\n";
            _exit(0);
        }
    }
    if ((scenario == "manifest-fail" && manifest) ||
        (scenario == "signature-fail" && !manifest)) {
        std::cout << "PARTIAL MUST BE DISCARDED";
        return 22;
    }
    if (scenario == "signature-redirect" && !manifest) {
        std::cout << "redirect body\n302";
        return 0;
    }
    if (scenario == "manifest-oversized" && manifest) {
        std::cout << std::string(16385, 'm') << "\n200";
        return 0;
    }
    if (scenario == "signature-oversized" && !manifest) {
        std::cout << std::string(2049, 's') << "\n200";
        return 0;
    }
    if (manifest) std::cout.write("manifest\0bytes\n", 15);
    else std::cout.write("signature\0bytes\n", 16);
    std::cout << "\n200";
    return 0;
}
