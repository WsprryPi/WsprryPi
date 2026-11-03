#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <utility>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <sys/stat.h>

// Linux sysfs metadata only. Discovery never opens an I2C adapter or probes a slave.
// Kept in the reusable transmitter component so application and transport share
// the same identity rules without a dependency on application internals.
namespace linux_i2c_identity {
namespace fs = std::filesystem;
struct Adapter {
    int bus = -1;
    std::string name, identity, usb_path, serial, vendor, product;
    fs::path sysfs_path, node;
    // Object identity detects detach/rebind, including reappearance at the same path.
    std::uintmax_t sys_dev = 0, sys_inode = 0, usb_dev = 0, usb_inode = 0;
    bool operator==(const Adapter& other) const {
        return bus == other.bus && identity == other.identity &&
            sysfs_path == other.sysfs_path && usb_path == other.usb_path &&
            sys_dev == other.sys_dev && sys_inode == other.sys_inode &&
            usb_dev == other.usb_dev && usb_inode == other.usb_inode;
    }
};
struct Inventory { std::vector<Adapter> adapters; std::string error; };
inline std::string read(const fs::path& path) {
    std::ifstream input(path);
    std::string result;
    std::getline(input, result);
    if (!result.empty() && result.back() == '\r') result.pop_back();
    return result;
}
inline std::string read_usb(const fs::path& path, std::string& error) {
    std::error_code code;
    const bool exists = fs::exists(path, code);
    if (code) { error = "Unable to inspect USB identity metadata: " + code.message(); return {}; }
    if (!exists) return {};
    std::ifstream input(path);
    std::string result;
    if (!input || !std::getline(input, result)) {
        // An empty serial file means no serial; an unreadable file is not topology fallback.
        if (!input.is_open() || input.bad() || !input.eof())
            error = "Unable to read USB identity metadata: " + path.string();
    }
    if (!result.empty() && result.back() == '\r') result.pop_back();
    return result;
}
inline bool hex(const std::string& value, std::size_t size) {
    return value.size() == size && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
inline std::string encode(const std::string& value) {
    const char* digits = "0123456789abcdef";
    std::string result;
    for (unsigned char c : value) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}
inline bool valid(const std::string& identity) {
    if (identity.empty()) return true; // Explicit legacy numeric-bus selection.
    if (identity.size() > 2048 || identity.rfind("usb:", 0) != 0 ||
        identity.size() < 21 || identity[8] != ':' || identity[13] != ':' ||
        !hex(identity.substr(4, 4), 4) || !hex(identity.substr(9, 4), 4)) return false;
    const auto kind_end = identity.find(':', 14);
    if (kind_end == std::string::npos) return false;
    const auto kind = identity.substr(14, kind_end - 14);
    const auto token = identity.substr(kind_end + 1);
    return (kind == "serial" || kind == "path") && !token.empty() &&
        token.size() % 2 == 0 && hex(token, token.size());
}
inline Inventory discover(const fs::path& sysfs = "/sys/class/i2c-dev",
                          const fs::path& devices = "/dev") {
    Inventory result;
    std::error_code error;
    fs::directory_iterator entries(sysfs, error), end;
    if (error == std::errc::no_such_file_or_directory) return result;
    while (!error && entries != end) {
        const auto path = entries->path();
        const auto filename = path.filename().string();
        if (filename.rfind("i2c-", 0) == 0) {
            const auto digits = filename.substr(4);
            int bus = -1;
            const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), bus);
            if (!digits.empty() && parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size() &&
                bus >= 0 && digits == std::to_string(bus)) {
                struct stat node_status{}, adapter_status{};
                const auto node = devices / filename;
                if (::stat(node.c_str(), &node_status) == 0 && S_ISCHR(node_status.st_mode)) {
                    Adapter item;
                    item.bus = bus; item.node = node; item.name = read(path / "name");
                    item.sysfs_path = fs::canonical(path, error);
                    if (error || ::stat(path.c_str(), &adapter_status) != 0) {
                        result.error = "I2C adapter metadata changed during discovery.";
                        result.adapters.clear(); return result;
                    }
                    item.sys_dev = adapter_status.st_dev; item.sys_inode = adapter_status.st_ino;
                    for (auto parent = item.sysfs_path; parent != parent.root_path(); parent = parent.parent_path()) {
                        const auto vendor = read_usb(parent / "idVendor", result.error);
                        const auto product = read_usb(parent / "idProduct", result.error);
                        if (!result.error.empty()) { result.adapters.clear(); return result; }
                        if (vendor.empty() && product.empty()) continue;
                        if (!hex(vendor, 4) || !hex(product, 4)) {
                            result.error = "Invalid USB adapter identity metadata.";
                            result.adapters.clear(); return result;
                        }
                        struct stat usb_status{};
                        if (::stat(parent.c_str(), &usb_status) != 0) {
                            result.error = "USB adapter removed during discovery.";
                            result.adapters.clear(); return result;
                        }
                        item.vendor = vendor; item.product = product;
                        item.usb_path = parent.string(); item.serial = read_usb(parent / "serial", result.error);
                        if (!result.error.empty()) { result.adapters.clear(); return result; }
                        item.usb_dev = usb_status.st_dev; item.usb_inode = usb_status.st_ino;
                        item.identity = "usb:" + vendor + ":" + product +
                            (item.serial.empty() ? ":path:" + encode(item.usb_path) : ":serial:" + encode(item.serial));
                        break;
                    }
                    result.adapters.push_back(std::move(item));
                }
            }
        }
        entries.increment(error);
    }
    if (error) { result.adapters.clear(); result.error = "Unable to read I2C identity metadata: " + error.message(); }
    std::sort(result.adapters.begin(), result.adapters.end(), [](const Adapter& a, const Adapter& b) { return a.bus < b.bus; });
    return result;
}
struct Resolution { Adapter adapter; std::string error; };
inline Resolution resolve(const Inventory& inventory, const std::string& identity, int bus) {
    if (!valid(identity)) return {{}, "Invalid stable I2C adapter identity."};
    if (!inventory.error.empty()) return {{}, inventory.error};
    Resolution result;
    unsigned matches = 0;
    for (const auto& item : inventory.adapters) {
        if (identity.empty() ? item.bus == bus : item.identity == identity) { result.adapter = item; ++matches; }
    }
    if (matches != 1) return {{}, matches == 0 ? "Configured I2C adapter is unavailable; no matching adapter." :
        "Configured I2C adapter identity is ambiguous; multiple matching adapters."};
    if (identity.empty() && result.adapter.vendor == "04d8" && result.adapter.product == "00dd")
        return {{}, "MCP2221A requires an explicit stable adapter identity; a bus number is not sufficient."};
    return result;
}
} // namespace linux_i2c_identity
