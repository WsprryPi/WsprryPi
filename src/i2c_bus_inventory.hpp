#pragma once

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Metadata only: never open an adapter or issue I2C transactions.
namespace i2c_bus_inventory
{
struct Bus
{
    int number;
    std::string name;
};
struct Inventory
{
    std::vector<Bus> buses;
    std::string error;
    bool contains(int number) const
    {
        return error.empty() && std::any_of(buses.begin(), buses.end(),
            [number](const Bus &bus) { return bus.number == number; });
    }
};

inline Inventory discover(
    const std::filesystem::path &sysfs = "/sys/class/i2c-dev",
    const std::filesystem::path &devices = "/dev")
{
    Inventory result;
    std::error_code error;
    std::filesystem::directory_iterator entries(sysfs, error), end;
    if (error == std::errc::no_such_file_or_directory) return result;
    while (!error && entries != end)
    {
        const auto entry = *entries;
        const std::string filename = entry.path().filename().string();
        if (filename.rfind("i2c-", 0) == 0)
        {
            const std::string digits = filename.substr(4);
            int number = -1;
            const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), number);
            if (!digits.empty() && parsed.ec == std::errc{} &&
                parsed.ptr == digits.data() + digits.size() && number >= 0 &&
                digits == std::to_string(number))
            {
                const auto status = std::filesystem::status(devices / filename, error);
                if (error == std::errc::no_such_file_or_directory) error.clear();
                if (!error && std::filesystem::is_character_file(status))
                {
                    std::string name;
                    std::ifstream stream(entry.path() / "name");
                    std::getline(stream, name);
                    result.buses.push_back({number, name});
                }
            }
        }
        if (!error) entries.increment(error);
    }
    if (error)
    {
        result.buses.clear();
        result.error = "Unable to read the I2C bus inventory: " + error.message();
    }
    std::sort(result.buses.begin(), result.buses.end(),
        [](const Bus &a, const Bus &b) { return a.number < b.number; });
    return result;
}

inline std::string selection_error(const Inventory &inventory, int number)
{
    if (!inventory.error.empty()) return inventory.error;
    if (inventory.contains(number)) return {};
    return "I2C bus " + std::to_string(number) +
        " is unavailable. Select an I2C bus present on this system.";
}
} // namespace i2c_bus_inventory
