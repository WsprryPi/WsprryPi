#include "monitorfile.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

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

    void write_text_file(const std::filesystem::path &path, const std::string &text)
    {
        std::ofstream out(path, std::ios::trunc);
        require(out.is_open(), "must open " + path.string());
        out << text;
        require(static_cast<bool>(out), "must write " + path.string());
    }

    template <typename Predicate>
    bool wait_until(
        Predicate predicate,
        std::chrono::milliseconds timeout,
        std::chrono::milliseconds interval = std::chrono::milliseconds(25))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
            {
                return true;
            }

            std::this_thread::sleep_for(interval);
        }

        return predicate();
    }
} // namespace

int main()
{
    namespace fs = std::filesystem;

    const fs::path test_dir =
        fs::temp_directory_path() / "wsprrypi_monitor_file_regression";
    const fs::path ini_path = test_dir / "watched.ini";
    const fs::path replace_path = test_dir / "watched.ini.tmp";

    std::error_code ec;
    fs::remove_all(test_dir, ec);
    fs::create_directories(test_dir, ec);
    require(!ec, "must create test directory");

    write_text_file(ini_path, "AAAAAAAA\n");

    std::atomic<int> callback_count{0};
    MonitorFile monitor;
    monitor.set_polling_interval(std::chrono::milliseconds(100));
    const MonitorState start_state =
        monitor.filemon(ini_path.string(), [&callback_count]() {
            callback_count.fetch_add(1, std::memory_order_relaxed);
        });

    require(start_state == MonitorState::MONITORING, "monitor must start on existing file");

    require(
        wait_until(
            [&monitor]() {
                return monitor.get_state() == MonitorState::MONITORING;
            },
            std::chrono::seconds(1)),
        "monitor must settle into MONITORING");

    fs::remove(ini_path, ec);
    require(!ec, "must delete watched file");

    require(
        wait_until(
            [&callback_count]() {
                return callback_count.load(std::memory_order_relaxed) >= 1;
            },
            std::chrono::seconds(3)),
        "deletion must trigger callback");
    require(
        wait_until(
            [&monitor]() {
                return monitor.get_state() == MonitorState::FILE_NOT_FOUND;
            },
            std::chrono::seconds(1)),
        "deletion must leave monitor in FILE_NOT_FOUND");

    write_text_file(ini_path, "AAAAAAAA\n");

    require(
        wait_until(
            [&callback_count]() {
                return callback_count.load(std::memory_order_relaxed) >= 2;
            },
            std::chrono::seconds(3)),
        "recreation must trigger callback");
    require(
        wait_until(
            [&monitor]() {
                return monitor.get_state() == MonitorState::MONITORING;
            },
            std::chrono::seconds(1)),
        "recreation must return monitor to MONITORING");

    const auto preserved_time = fs::last_write_time(ini_path);
    write_text_file(replace_path, "BBBBBBBB\n");
    fs::last_write_time(replace_path, preserved_time, ec);
    require(!ec, "must preserve replacement timestamp");
    fs::rename(replace_path, ini_path, ec);
    require(!ec, "must atomically replace watched file");

    require(
        wait_until(
            [&callback_count]() {
                return callback_count.load(std::memory_order_relaxed) >= 3;
            },
            std::chrono::seconds(3)),
        "atomic replacement must trigger callback");

    monitor.stop();
    fs::remove_all(test_dir, ec);
    require(!ec, "must remove test directory");

    return EXIT_SUCCESS;
}
