#include "linux_i2c_identity.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>

int main() {
    namespace fs = std::filesystem;
    using namespace linux_i2c_identity;
    char temp[] = "/tmp/wsprrypi-identity-XXXXXX";
    assert(mkdtemp(temp));
    const fs::path root(temp), sys = root / "class", dev = root / "dev";
    fs::create_directories(sys); fs::create_directory(dev);
    auto add = [&](int bus, const std::string& port, const std::string& serial) {
        const auto usb = root / "devices" / port;
        const auto adapter = usb / ("i2c-" + std::to_string(bus));
        fs::create_directories(adapter);
        std::ofstream(usb / "idVendor") << "04d8\n";
        std::ofstream(usb / "idProduct") << "00dd\n";
        if (!serial.empty()) std::ofstream(usb / "serial") << serial << '\n';
        std::ofstream(adapter / "name") << "MCP2221\n";
        fs::create_directory_symlink(adapter, sys / adapter.filename());
        fs::create_symlink("/dev/null", dev / adapter.filename());
    };
    add(5, "usb1/1-2", "unique serial");
    auto inventory = discover(sys, dev);
    assert(inventory.error.empty() && inventory.adapters.size() == 1);
    const auto identity = inventory.adapters[0].identity;
    assert(identity == "usb:04d8:00dd:serial:" + encode("unique serial"));
    assert(valid(identity));
    assert(resolve(inventory, identity, 1).adapter.bus == 5); // Never trusts saved numeric bus.
    assert(!resolve(inventory, {}, 5).error.empty()); // MCP numeric selection refused.
    const auto binding = inventory.adapters[0];
    fs::remove(sys / "i2c-5"); fs::remove(dev / "i2c-5");
    add(17, "usb1/1-2", "unique serial");
    inventory = discover(sys, dev);
    assert(resolve(inventory, identity, 5).adapter.bus == 17);
    assert(!(resolve(inventory, identity, 5).adapter == binding)); // Reconnect invalidates active binding.
    add(18, "usb1/1-3", "unique serial");
    assert(resolve(discover(sys, dev), identity, 5).error.find("ambiguous") != std::string::npos);
    add(19, "usb1/1-4", "");
    inventory = discover(sys, dev);
    const auto topology = inventory.adapters.back().identity;
    assert(valid(topology) && topology.find(":path:") != std::string::npos);
    assert(resolve(inventory, topology, 1).adapter.bus == 19);
    std::ofstream(root / "devices/usb1/1-4/serial") << "now has serial\n";
    assert(!resolve(discover(sys, dev), topology, 19).error.empty()); // Stronger identity never ignored.
    for (const auto& invalid : {"usb:04D8:00dd:serial:aa", "usb:04d8:00dd:serial:",
                               "usb:04d8:00dd:serial:a", "usb:04d8:00dd:path:../dev", "other"})
        assert(!valid(invalid));
    assert(!resolve(discover(root / "absent", dev), identity, 1).error.empty());
    assert(!resolve(Inventory{{}, "metadata denied"}, identity, 1).error.empty());
    // Unreadable identity metadata is an error, never a weaker topology identity.
    const auto serial_file = root / "devices/usb1/1-4/serial";
    fs::permissions(serial_file, fs::perms::none);
    if (geteuid() == 0) {
        // GitHub container jobs may start as root. Exercise actual denied access
        // in an unprivileged child rather than assuming mode bits restrict root.
        fs::permissions(root, fs::perms::owner_all | fs::perms::group_read |
            fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec);
        const pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            if (setgid(65534) != 0 || setuid(65534) != 0) _exit(2);
            _exit(discover(sys, dev).error.empty() ? 1 : 0);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    } else {
        assert(!discover(sys, dev).error.empty());
    }
    fs::permissions(serial_file, fs::perms::owner_read | fs::perms::owner_write);
    // Recreate an adapter at the same bus/path while retaining its former inode.
    const auto before_rebind = resolve(discover(sys, dev), "usb:04d8:00dd:serial:" + encode("now has serial"), 19).adapter;
    fs::rename(root / "devices/usb1/1-4/i2c-19", root / "retired-i2c-19");
    fs::create_directory(root / "devices/usb1/1-4/i2c-19");
    const auto after_rebind = resolve(discover(sys, dev), before_rebind.identity, 19);
    assert(after_rebind.error.empty() && !(after_rebind.adapter == before_rebind));
    // A generic non-USB adapter retains explicit numeric selection.
    fs::create_directory(sys / "i2c-20"); fs::create_symlink("/dev/null", dev / "i2c-20");
    assert(resolve(discover(sys, dev), {}, 20).error.empty());
    fs::remove_all(root);
    std::cout << "Stable I2C identity: serial, duplicate, no serial/topology, renumbering, removal, mismatch, and numeric compatibility passed\n";
}
