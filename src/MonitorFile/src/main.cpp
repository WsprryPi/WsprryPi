/**
 * @file main.cpp
 * @brief Bounded, hardware-free MonitorFile test.
 *
 * Licensed under the repository-root LICENSE.md.
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 */

#include "monitorfile.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;

int main()
{
    const fs::path testDir = fs::temp_directory_path() /
                             ("monitorfile-test-" + std::to_string(
                                                       std::chrono::steady_clock::now()
                                                           .time_since_epoch()
                                                           .count()));
    const fs::path testFile = testDir / "observed.txt";
    fs::create_directories(testDir);

    int result = 1;
    {
        std::ofstream(testFile) << "initial\n";
        std::atomic<bool> callbackCalled(false);
        MonitorFile monitor;
        monitor.set_polling_interval(std::chrono::milliseconds(25));

        if (monitor.filemon(testFile.string(), [&callbackCalled]() {
                callbackCalled.store(true);
            }) == MonitorState::MONITORING)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::ofstream(testFile, std::ios::app) << "changed\n";

            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(3);
            while (!callbackCalled.load() &&
                   std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            result = callbackCalled.load() ? 0 : 1;
        }
        monitor.stop();
    }

    fs::remove_all(testDir);
    if (result != 0)
    {
        std::cerr << "MonitorFile did not report the bounded test change.\n";
    }
    return result;
}
