#include "../i2c_bus_inventory.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>

int main()
{
    namespace fs = std::filesystem;
    using namespace i2c_bus_inventory;
    char scratch[] = "/tmp/wsprrypi-i2c-inventory-XXXXXX";
    const char *created = mkdtemp(scratch);
    assert(created);
    const fs::path root(created), sysfs = root / "sys", dev = root / "device-fixture";
    fs::create_directory(sysfs);
    fs::create_directory(dev);
    assert(discover(sysfs, dev).buses.empty());
    assert(discover(root / "absent", dev).error.empty());
    for (const auto &name : {"i2c-10", "i2c-2", "i2c-0", "i2c-7", "i2c-8",
                             "i2c-", "i2c-01", "i2c--1", "i2c-+1", "i2c-2extra",
                             "i2c-99999999999999999999", "other"})
    {
        fs::create_directory(sysfs / name);
        std::ofstream(sysfs / name / "name") << "Test adapter " << name << '\n';
        if (std::string(name) != "i2c-7") fs::create_symlink("/dev/null", dev / name);
    }
    fs::remove(dev / "i2c-8");
    std::ofstream(dev / "i2c-8") << "not a device";
    auto inventory = discover(sysfs, dev);
    assert(inventory.error.empty());
    assert(inventory.buses.size() == 3);
    assert(inventory.buses[0].number == 0);
    assert(inventory.buses[1].number == 2);
    assert(inventory.buses[2].number == 10);
    assert(inventory.buses[1].name == "Test adapter i2c-2");
    assert(selection_error(inventory, 2).empty());
    assert(!selection_error(inventory, 7).empty());
    fs::remove(dev / "i2c-2");
    assert(!discover(sysfs, dev).contains(2));
    fs::remove(sysfs / "i2c-10" / "name");
    assert(discover(sysfs, dev).buses.back().name.empty());
    inventory = discover(dev / "i2c-8", dev);
    assert(!inventory.error.empty());
    assert(inventory.buses.empty());
    assert(!selection_error(inventory, 0).empty());
    fs::remove_all(root);
    std::cout << "I2C metadata discovery tests passed\n";
}
