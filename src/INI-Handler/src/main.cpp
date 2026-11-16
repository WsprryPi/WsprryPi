/*
 * main.cpp
 *
 * Standalone test harness for the IniFile class. This file intentionally uses
 * regular implementation comments rather than API Doxygen because the public
 * interface is documented in ini_file.hpp.
 */

#include "ini_file.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace
{
    const fs::path kFixtureIni = "../test/test.ini";
    const std::string kSemanticVersion = "9.9.9-test";
    fs::path test_dir;
    fs::path baseline_ini;
    fs::path baseline_stock_ini;
    fs::path test_ini;
    fs::path test_stock_ini;

    void copy_file_or_throw(const fs::path &source, const fs::path &destination)
    {
        std::error_code ec;
        fs::copy_file(source,
                      destination,
                      fs::copy_options::overwrite_existing,
                      ec);
        if (ec)
        {
            throw std::runtime_error("Cannot copy file from " +
                                     source.string() + " to " +
                                     destination.string() + ".");
        }
    }

    void require_file_contains(const fs::path &path, const std::string &text)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            throw std::runtime_error("Cannot open file " + path.string() +
                                     " for verification.");
        }

        std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

        if (contents.find(text) == std::string::npos)
        {
            throw std::runtime_error("Expected text not found in " +
                                     path.string() + ".");
        }
    }

    void require_file_not_contains(const fs::path &path, const std::string &text)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            throw std::runtime_error("Cannot open file " + path.string() +
                                     " for verification.");
        }

        const std::string contents((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        if (contents.find(text) != std::string::npos)
        {
            throw std::runtime_error("Unexpected text found in " +
                                     path.string() + ".");
        }
    }

    fs::path get_source_ini()
    {
        if (fs::exists(kFixtureIni))
        {
            return kFixtureIni;
        }

        throw std::runtime_error("No test fixture found at " +
                                 kFixtureIni.string() + ".");
    }

    fs::path get_source_stock_ini(const fs::path &source_ini)
    {
        const fs::path fixture_stock_ini(kFixtureIni.string() + ".stock");

        if (fs::exists(fixture_stock_ini))
        {
            return fixture_stock_ini;
        }

        return source_ini;
    }

    void prepare_test_environment()
    {
        const auto unique_suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        test_dir = fs::temp_directory_path() /
                   ("ini-handler-test-" + std::to_string(unique_suffix));
        baseline_ini = test_dir / "wsprrypi.baseline.ini";
        baseline_stock_ini = test_dir / "wsprrypi.baseline.ini.stock";
        test_ini = test_dir / "wsprrypi.test.ini";
        test_stock_ini = test_dir / "wsprrypi.test.ini.stock";

        std::error_code ec;
        fs::create_directories(test_dir, ec);
        if (ec)
        {
            throw std::runtime_error("Cannot create test directory " +
                                     test_dir.string() + ".");
        }

        const fs::path source_ini = get_source_ini();
        const fs::path source_stock_ini = get_source_stock_ini(source_ini);

        copy_file_or_throw(source_ini, baseline_ini);
        copy_file_or_throw(source_stock_ini, baseline_stock_ini);
        copy_file_or_throw(baseline_ini, test_ini);
        copy_file_or_throw(baseline_stock_ini, test_stock_ini);
    }

    void restore_known_good_state(IniFile &config)
    {
        copy_file_or_throw(baseline_ini, test_ini);
        copy_file_or_throw(baseline_stock_ini, test_stock_ini);
        config.set_filename(test_ini.string());
    }

    void cleanup_test_environment()
    {
        if (test_dir.empty())
        {
            return;
        }

        std::error_code ec;
        fs::remove_all(test_dir, ec);
        if (ec)
        {
            throw std::runtime_error("Cannot remove test directory " +
                                     test_dir.string() + ".");
        }
    }

    class TestFileGuard
    {
    public:
        TestFileGuard() = default;

        ~TestFileGuard()
        {
            try
            {
                cleanup_test_environment();
            }
            catch (const std::exception &e)
            {
                std::cerr << "Cleanup warning: " << e.what() << std::endl;
            }
        }

        TestFileGuard(const TestFileGuard &) = delete;
        TestFileGuard &operator=(const TestFileGuard &) = delete;
    };

    void print_header(const std::string &title)
    {
        std::cout << std::endl
                  << title << std::endl;
    }
}

void test_malformed_entries(IniFile &config)
{
    print_header("Testing malformed INI entries.");

    try
    {
        config.set_string_value("Common", "TX Power", "abc");
        const int tx_power = config.get_int_value("Common", "TX Power");
        std::cout << "TX Power after setting invalid value: " << tx_power
                  << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Caught exception for malformed TX Power: "
                  << e.what() << std::endl;
    }

    try
    {
        config.set_string_value("Extended", "PPM", "xyz");
        const double ppm = config.get_double_value("Extended", "PPM");
        std::cout << "PPM after setting invalid value: " << ppm << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Caught exception for malformed PPM: "
                  << e.what() << std::endl;
    }
}

void test_reading(IniFile &config, const std::string &filename)
{
    print_header("Testing read operations on: " + filename);

    std::cout << std::boolalpha;
    std::cout << "Control  | Transmit: "
              << config.get_bool_value("Control", "Transmit") << std::endl;

    std::cout << "Common   | Call Sign: "
              << config.get_string_value("Common", "Call Sign") << std::endl;
    std::cout << "Common   | Grid Square: "
              << config.get_string_value("Common", "Grid Square")
              << std::endl;
    std::cout << "Common   | TX Power: "
              << config.get_int_value("Common", "TX Power") << std::endl;
    std::cout << "Common   | Frequency: "
              << config.get_string_value("Common", "Frequency") << std::endl;
    std::cout << "Common   | Transmit Pin: "
              << config.get_int_value("Common", "Transmit Pin") << std::endl;

    std::cout << "Extended | PPM: "
              << config.get_double_value("Extended", "PPM") << std::endl;
    std::cout << "Extended | Use NTP: "
              << config.get_bool_value("Extended", "Use NTP") << std::endl;
    std::cout << "Extended | Offset: "
              << config.get_bool_value("Extended", "Offset") << std::endl;
    std::cout << "Extended | Use LED: "
              << config.get_bool_value("Extended", "Use LED") << std::endl;
    std::cout << "Extended | LED Pin: "
              << config.get_int_value("Extended", "LED Pin") << std::endl;
    std::cout << "Extended | Power Level: "
              << config.get_int_value("Extended", "Power Level") << std::endl;

    std::cout << "Server   | Web Port: "
              << config.get_int_value("Server", "Web Port") << std::endl;
    std::cout << "Server   | Socket Port: "
              << config.get_int_value("Server", "Socket Port") << std::endl;
    std::cout << "Server   | Use Shutdown: "
              << config.get_bool_value("Server", "Use Shutdown")
              << std::endl;
    std::cout << "Server   | Shutdown Button: "
              << config.get_int_value("Server", "Shutdown Button")
              << std::endl;

    try
    {
        std::cout << "Reading missing section." << std::endl;
        config.get_string_value("NonExistent", "Key");
    }
    catch (const std::exception &e)
    {
        std::cerr << "Caught expected exception: " << e.what() << std::endl;
    }

    try
    {
        std::cout << "Reading missing key in existing section." << std::endl;
        config.get_string_value("Control", "FakeKey");
    }
    catch (const std::exception &e)
    {
        std::cerr << "Caught expected exception: " << e.what() << std::endl;
    }
}

void test_writing(IniFile &config, const std::string &filename)
{
    print_header("Testing write operations.");

    config.set_bool_value("Control", "Transmit", true);
    config.set_int_value("Common", "TX Power", 30);
    config.set_double_value("Extended", "PPM", 1.23);
    config.set_string_value("Common", "Call Sign", "TEST123");
    config.set_string_value("NewSection", "NewKey", "NewValue");

    config.commit_changes();

    std::cout << "Write test complete." << std::endl;

    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Cannot open file for verification: " +
                                 filename + ".");
    }

    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    if (contents.find("[NewSection]") == std::string::npos ||
        contents.find("NewKey = NewValue") == std::string::npos)
    {
        throw std::runtime_error("NewSection/NewKey was not persisted.");
    }

    std::cout << "Verified: NewSection/NewKey was written."
              << std::endl;
}

void test_serialization(IniFile &config)
{
    print_header("Testing pure proposed-data serialization.");
    const auto filename = test_dir / "serialization.ini";
    const std::string original = std::string("; preserved byte ") + char(0xff) +
        "\r\n[Common]\r\nKey = first\r\nKey = last\r\nKeep = retained\r\nRemove = old\r\n"
        "[Security]\r\n  Privileged Network Safety = enforced  \r\n[Tail]\r\nLast = unchanged\r\n";
    { std::ofstream output(filename, std::ios::binary); output << original; }
    config.set_filename(filename.string());
    config.set_raw_passthrough_keys({{"Security", "Privileged Network Safety"}});
    config.erase_value("Common", "Remove");
    const auto current = config.getData();
    const bool pending = config.hasPendingChanges();
    auto proposed = current;
    proposed["Common"]["Key"] = "proposed";
    proposed["Common"]["Added"] = "new";
    proposed["New section"]["New key"] = "new value";
    proposed["Security"]["Privileged Network Safety"] = "disabled";
    proposed["Tail"].erase("Last");
    const auto predicted = config.serialize_with_data(proposed);
    const auto read = [&]() {
        std::ifstream input(filename, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    };
    if (read() != original || config.getData() != current ||
        config.hasPendingChanges() != pending || fs::exists(filename.string() + ".tmp"))
        throw std::runtime_error("Serialization changed file, data, pending edits, or temporary files.");
    const auto first = predicted.find("Key = proposed\n");
    if (first == std::string::npos || predicted.find("Key = proposed\n", first + 1) == std::string::npos ||
        predicted.find("Remove =") != std::string::npos || predicted.find("Last = unchanged\r\n") == std::string::npos ||
        predicted.find("  Privileged Network Safety = enforced  \r\n") == std::string::npos ||
        predicted.find(char(0xff)) == std::string::npos || predicted.find("[New section]\nNew key = new value\n") == std::string::npos)
        throw std::runtime_error("Serialization lost layout, removals, additions, duplicates, bytes, or raw owner data.");
    auto reintroduced = proposed;
    reintroduced["Common"]["Remove"] = "reintroduced";
    const auto restored_key = config.serialize_with_data(reintroduced);
    if (restored_key.find("Remove = reintroduced\n") == std::string::npos ||
        config.getData() != current || config.hasPendingChanges() != pending)
        throw std::runtime_error("Proposed reintroduction changed pending erasure state.");
    fs::remove(filename);
    if (config.serialize_with_data(proposed) != predicted || fs::exists(filename))
        throw std::runtime_error("Serialization performed I/O on a missing live file.");
    { std::ofstream output(filename, std::ios::binary); output << original; }
    config.setData(proposed);
    config.save();
    if (read() != predicted || config.hasPendingChanges())
        throw std::runtime_error("Predicted bytes differ from the actual save.");
    config.setData(reintroduced);
    config.save();
    if (read() != restored_key)
        throw std::runtime_error("Reintroduced-key prediction differs from setData/save.");
    config.set_raw_passthrough_keys({});
}

void test_key_erasure(IniFile &config, const std::string &filename)
{
    print_header("Testing explicit key erasure.");

    require_file_contains(filename, "Use NTP =");
    const std::string retained_call_sign =
        config.get_string_value("Common", "Call Sign");

    if (!config.erase_value("Extended", "Use NTP"))
    {
        throw std::runtime_error("Existing key was not marked for erasure.");
    }
    config.commit_changes();

    require_file_not_contains(filename, "Use NTP =");
    require_file_contains(filename, "[Extended]");
    if (config.get_string_value("Common", "Call Sign") != retained_call_sign)
    {
        throw std::runtime_error("Unrelated value changed during key erasure.");
    }
    if (config.erase_value("Extended", "Use NTP"))
    {
        throw std::runtime_error("Repeated erasure should report no existing key.");
    }

    config.set_bool_value("Extended", "Use NTP", true);
    config.commit_changes();
    require_file_contains(filename, "Use NTP = true");
}

void test_exceptions(IniFile &config)
{
    print_header("Testing INI exception processing.");

    try
    {
        std::cout << "Reading get_string_value() with bad section."
                  << std::endl;
        config.get_string_value("Bad Section", "Bad Key");
    }
    catch (const std::exception &e)
    {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    }

    try
    {
        std::cout << "Reading get_bool_value() with bad section."
                  << std::endl;
        config.get_bool_value("Bad Section", "Bad Key");
    }
    catch (const std::exception &e)
    {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    }

    try
    {
        std::cout << "Reading get_string_value() with bad key."
                  << std::endl;
        config.get_string_value("Common", "Bad Key");
    }
    catch (const std::exception &e)
    {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    }

    try
    {
        std::cout << "Reading get_int_value() with bad key." << std::endl;
        config.get_int_value("Common", "Bad Key");
    }
    catch (const std::exception &e)
    {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    }

    try
    {
        std::cout << "Reading get_double_value() with bad key."
                  << std::endl;
        config.get_double_value("Common", "Bad Key");
    }
    catch (const std::exception &e)
    {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    }

    try
    {
        std::cout << "Reading get_double_value() for PPM." << std::endl;
        std::cout << "PPM: "
                  << config.get_double_value("Extended", "PPM")
                  << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    }
}

void test_reset_to_stock(IniFile &config, const std::string &filename)
{
    print_header("Testing reset_to_stock().");

    config.set_string_value("Common", "Call Sign", "RESETME");
    config.set_int_value("Common", "TX Power", 42);
    config.commit_changes();

    std::ofstream retired_keys(filename, std::ios::app);
    if (!retired_keys.is_open())
    {
        throw std::runtime_error("Cannot append retired keys to test ini file " +
                                 filename + ".");
    }
    retired_keys << "22m = 17\n22m Active High = True\n";
    retired_keys.close();
    config.set_filename(filename);

    require_file_contains(filename, "22m = 17");
    require_file_contains(filename, "22m Active High = True");

    std::cout << "Modified values before reset." << std::endl;
    std::cout << "Common   | Call Sign: "
              << config.get_string_value("Common", "Call Sign") << std::endl;
    std::cout << "Common   | TX Power: "
              << config.get_int_value("Common", "TX Power") << std::endl;

    config.reset_to_stock(kSemanticVersion);

    std::cout << "Values after reset." << std::endl;
    std::cout << "Common   | Call Sign: "
              << config.get_string_value("Common", "Call Sign") << std::endl;
    std::cout << "Common   | TX Power: "
              << config.get_int_value("Common", "TX Power") << std::endl;

    require_file_contains(filename, kSemanticVersion);
    std::cout << "Semantic version token replacement verified."
              << std::endl;

    const std::string reset_call_sign =
        config.get_string_value("Common", "Call Sign");
    if (reset_call_sign == "RESETME")
    {
        throw std::runtime_error("reset_to_stock() did not restore Call Sign.");
    }

    const int reset_tx_power = config.get_int_value("Common", "TX Power");
    if (reset_tx_power == 42)
    {
        throw std::runtime_error("reset_to_stock() did not restore TX Power.");
    }

    require_file_not_contains(filename, "22m =");
    require_file_not_contains(filename, "22m Active High =");

    std::cout << "reset_to_stock() verification passed." << std::endl;
}

void test_stock_serialization(IniFile &config)
{
    print_header("Testing pure stock reset/repair serialization.");
    const auto filename = test_dir / "stock-preview.ini";
    const auto stockfile = fs::path(filename.string() + ".stock");
    const auto write = [](const fs::path &path, const std::string &bytes) {
        std::ofstream output(path, std::ios::binary); output << bytes;
        if (!output) throw std::runtime_error("Stock preview fixture write failed.");
    };
    const auto read = [](const fs::path &path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    const std::string live = "[Section]\nKey = custom\nOther = off\nRetired = discarded\n[Retired]\nX = discarded\n";
    const std::string stock = "; %SEMANTIC_VERSION% / %SEMANTIC_VERSION%\n[Section]\nKey = factory ; retained on reset\nOther = on\n";
    const std::string reset = "; 1.2.3 / 1.2.3\n[Section]\nKey = factory ; retained on reset\nOther = on\n";
    const std::string repaired = "; 1.2.3 / 1.2.3\n[Section]\nKey = custom\nOther = off\n";
    write(filename, live);write(stockfile, stock);config.set_filename(filename.string());
    config.set_string_value("Section", "Key", "unsaved");
    config.erase_value("Section", "Other");
    const auto memory = config.getData();
    const auto pending = config.hasPendingChanges();
    const auto ordinary = config.serialize_with_data(memory);
    if (config.serialize_from_stock("1.2.3", false) != reset ||
        config.serialize_from_stock("1.2.3", true) != repaired ||
        config.getData() != memory || config.hasPendingChanges() != pending ||
        config.serialize_with_data(memory) != ordinary || read(filename) != live ||
        read(stockfile) != stock || fs::exists(filename.string() + ".tmp"))
        throw std::runtime_error("Stock preview changed state or differs from exact legacy reset/repair bytes.");
    config.repair_from_stock("1.2.3");
    if (read(filename) != repaired || config.hasPendingChanges())
        throw std::runtime_error("Repair differs from its exact preview.");
    config.reset_to_stock("1.2.3");
    if (read(filename) != reset || config.hasPendingChanges())
        throw std::runtime_error("Reset differs from its exact preview.");
    // Unchanged compatible values retain the original stock inline comments;
    // raw owner fields retain stock bytes even when another field is merged.
    if (config.serialize_from_stock("1.2.3", true) != reset)
        throw std::runtime_error("No-op repair normalized stock layout.");
    write(filename,live);config.set_filename(filename.string());
    config.set_raw_passthrough_keys({{"Section","Key"}});
    const std::string raw = "; 1.2.3 / 1.2.3\n[Section]\nKey = factory ; retained on reset\nOther = off\n";
    if (config.serialize_from_stock("1.2.3",true) != raw)
        throw std::runtime_error("Repair preview changed raw passthrough semantics.");
    config.repair_from_stock("1.2.3");
    if (read(filename) != raw) throw std::runtime_error("Raw repair differs from preview.");
    config.set_raw_passthrough_keys({});
    config.set_string_value("Section","Key","pending");
    const auto before_error=config.getData();const auto pending_error=config.hasPendingChanges();
    fs::remove(stockfile);
    for (const bool repair : {false,true}) {
        bool refused=false;
        try { (void)config.serialize_from_stock("1.2.3",repair); }
        catch (const std::runtime_error &) { refused=true; }
        if (!refused || config.getData()!=before_error || config.hasPendingChanges()!=pending_error ||
            read(filename)!=raw || fs::exists(filename.string()+".tmp"))
            throw std::runtime_error("Failed stock preview changed live or pending state.");
    }
    write(stockfile,stock);fs::remove(filename);
    if (config.serialize_from_stock("1.2.3",false)!=reset || fs::exists(filename))
        throw std::runtime_error("Reset preview requires or creates a live file.");
    bool refused=false;
    try { (void)config.serialize_from_stock("1.2.3",true); }
    catch (const std::runtime_error &) { refused=true; }
    if (!refused || config.getData()!=before_error || config.hasPendingChanges()!=pending_error)
        throw std::runtime_error("Repair preview of absent live file changed state.");
}

void test_repair_from_stock(IniFile &config, const std::string &filename)
{
    print_header("Testing repair_from_stock().");

    config.set_string_value("Common", "Call Sign", "REPAIRME");
    config.set_int_value("Common", "TX Power", 33);
    config.set_double_value("Extended", "PPM", 2.5);
    config.set_bool_value("Control", "Transmit", true);
    config.commit_changes();

    std::ofstream damaged_file(filename, std::ios::trunc);
    if (!damaged_file.is_open())
    {
        throw std::runtime_error("Cannot open damaged test ini file " +
                                 filename + ".");
    }

    damaged_file
        << "; Created for WsprryPi version broken\n"
        << "[Control]\n"
        << "Transmit = True\n"
        << "\n"
        << "[Common]\n"
        << "Call Sign = REPAIRME\n"
        << "TX Power = 33\n"
        << "\n"
        << "[Extended]\n"
        << "PPM = 2.5\n"
        << "\n"
        << "[Band GPIO]\n"
        << "20m = 23\n"
        << "20m Active High = true\n"
        << "22m = 17\n"
        << "22m Active High = true\n";
    damaged_file.close();

    config.set_filename(filename);

    config.repair_from_stock(kSemanticVersion);

    std::cout << "Values after repair." << std::endl;
    std::cout << "Control  | Transmit: "
              << config.get_bool_value("Control", "Transmit") << std::endl;
    std::cout << "Common   | Call Sign: "
              << config.get_string_value("Common", "Call Sign") << std::endl;
    std::cout << "Common   | TX Power: "
              << config.get_int_value("Common", "TX Power") << std::endl;
    std::cout << "Common   | Grid Square: "
              << config.get_string_value("Common", "Grid Square")
              << std::endl;
    std::cout << "Extended | PPM: "
              << config.get_double_value("Extended", "PPM") << std::endl;
    std::cout << "Server   | Web Port: "
              << config.get_int_value("Server", "Web Port") << std::endl;
    std::cout << "Server   | Socket Port: "
              << config.get_int_value("Server", "Socket Port") << std::endl;

    require_file_contains(filename, kSemanticVersion);
    std::cout << "Semantic version token replacement verified."
              << std::endl;

    if (config.get_string_value("Common", "Call Sign") != "REPAIRME")
    {
        throw std::runtime_error("repair_from_stock() did not preserve "
                                 "Call Sign.");
    }

    if (config.get_int_value("Common", "TX Power") != 33)
    {
        throw std::runtime_error("repair_from_stock() did not preserve "
                                 "TX Power.");
    }

    if (config.get_double_value("Extended", "PPM") != 2.5)
    {
        throw std::runtime_error("repair_from_stock() did not preserve PPM.");
    }

    if (!config.get_bool_value("Control", "Transmit"))
    {
        throw std::runtime_error("repair_from_stock() did not preserve "
                                 "Transmit.");
    }

    if (config.get_string_value("Common", "Grid Square") != "ZZ99")
    {
        throw std::runtime_error("repair_from_stock() did not restore "
                                 "Grid Square from stock.");
    }

    if (config.get_int_value("Server", "Web Port") != 31415)
    {
        throw std::runtime_error("repair_from_stock() did not restore "
                                 "Web Port.");
    }

    if (config.get_int_value("Server", "Socket Port") != 31416)
    {
        throw std::runtime_error("repair_from_stock() did not restore "
                                 "Socket Port.");
    }

    if (config.get_int_value("Band GPIO", "20m") != 23 ||
        !config.get_bool_value("Band GPIO", "20m Active High"))
    {
        throw std::runtime_error("repair_from_stock() did not preserve "
                                 "current-schema Band GPIO values.");
    }

    require_file_not_contains(filename, "22m =");
    require_file_not_contains(filename, "22m Active High =");

    std::cout << "repair_from_stock() verification passed." << std::endl;
}

void test_file_reader(IniFile &config)
{
    const auto path=(test_dir/"virtual-reader.ini").string();
    const std::string live="; retained\n[Section]\nKey = custom\nOther = off\n";
    const std::string stock="; %SEMANTIC_VERSION%\n[Section]\nKey = factory\nOther = on\n";
    bool fail=false;std::vector<std::string> reads;
    struct Restore{IniFile &config;~Restore(){config.set_file_reader({});}} restore{config};
    config.set_file_reader([&](const std::string &name){
        reads.push_back(name);
        if(fail)throw std::runtime_error("Reader refuses input");
        if(name==path)return live;
        if(name==path+".stock")return stock;
        throw std::runtime_error("Unexpected reader path");
    });
    config.set_filename(path);
    if(fs::exists(path)||config.get_value("Section","Key")!="custom")throw std::runtime_error("Reader must supply live bytes without reopening a file");
    config.set_string_value("Section","Key","unsaved");config.erase_value("Section","Other");
    const auto data=config.getData(),before_data=data;
    const auto preview=config.serialize_with_data(data);
    if(config.serialize_from_stock("reader-version",false)!="; reader-version\n[Section]\nKey = factory\nOther = on\n"||
        config.serialize_from_stock("reader-version",true)!="; reader-version\n[Section]\nKey = custom\nOther = off\n")
        throw std::runtime_error("Stock preview must use the same reader and existing merge/layout semantics");
    if(config.getData()!=before_data||!config.hasPendingChanges()||config.serialize_with_data(config.getData())!=preview)
        throw std::runtime_error("Reader previews must preserve pending data and removals");
    fail=true;
    for(const auto action:{0,1,2}){
        bool refused=false;
        try{if(action==0)config.load();else (void)config.serialize_from_stock("v",action==2);}catch(const std::runtime_error &){refused=true;}
        if(!refused||config.getData()!=before_data||!config.hasPendingChanges()||config.serialize_with_data(config.getData())!=preview)
            throw std::runtime_error("Reader failure must leave component state unchanged");
    }
    if(reads.size()<5)throw std::runtime_error("Expected live/stock reader coverage");
    config.set_file_reader({});
    bool ordinary_refused=false;try{config.load();}catch(const std::runtime_error &){ordinary_refused=true;}
    if(!ordinary_refused)throw std::runtime_error("Clearing a reader must restore ordinary missing-file refusal");
}

void test_file_writer(IniFile &config)
{
    const auto path=(test_dir/"virtual-writer.ini").string();
    const std::string stock="; %SEMANTIC_VERSION%\n[Section]\nKey = factory\n";
    std::string live="; retained\n[Section]\nKey = saved\n";
    bool fail=false;unsigned writes=0;
    struct Restore{IniFile &config;~Restore(){config.set_file_writer({});config.set_file_reader({});}} restore{config};
    config.set_file_reader([&](const std::string &name){
        if(name==path)return live;
        if(name==path+".stock")return stock;
        throw std::runtime_error("Unexpected reader path");
    });
    config.set_file_writer([&](const std::string &name,const std::string &value){
        if(name!=path)throw std::runtime_error("Unexpected writer path");
        if(fail)throw std::runtime_error("Writer refuses publication");
        ++writes;live=value;
    });
    config.set_filename(path);config.set_string_value("Section","Key","pending");
    const auto pending=config.getData();const auto preview=config.serialize_with_data(pending);
    fail=true;
    for(const auto action:{0,1,2}){
        bool refused=false;
        try{if(action==0)config.save();else if(action==1)config.reset_to_stock("v");else config.repair_from_stock("v");}
        catch(const std::runtime_error &){refused=true;}
        if(!refused||writes||config.getData()!=pending||!config.hasPendingChanges()||config.serialize_with_data(config.getData())!=preview)
            throw std::runtime_error("Writer refusal must preserve pending data and layout without default publication");
    }
    fail=false;config.commit_changes();
    if(writes!=1||live!=preview||config.hasPendingChanges()||fs::exists(path)||fs::exists(path+".tmp"))
        throw std::runtime_error("Writer must receive exact live bytes and bypass ordinary filesystem publication");
    config.reset_to_stock("reset");
    if(writes!=2||live!="; reset\n[Section]\nKey = factory\n"||config.get_value("Section","Key")!="factory")
        throw std::runtime_error("Reset must publish through the writer then reload through the reader");
    config.set_string_value("Section","Key","uncommitted");
    config.repair_from_stock("repair");
    if(writes!=3||live!="; repair\n[Section]\nKey = factory\n"||config.hasPendingChanges())
        throw std::runtime_error("Repair must use saved values and the same publication policy");
    config.set_file_writer({});config.set_string_value("Section","Key","ordinary");config.save();
    if(writes!=3||!fs::exists(path)||config.hasPendingChanges())
        throw std::runtime_error("Clearing a writer must restore ordinary publication");
}

template <typename TestFunc>
void run_test(IniFile &config,
              const std::string &test_name,
              TestFunc test_func)
{
    std::cout << std::endl
              << "Preparing known-good state for " << test_name << "."
              << std::endl;
    restore_known_good_state(config);
    test_func();
}

int main()
{
    try
    {
        const TestFileGuard guard;
        prepare_test_environment();

        auto &ini_file = IniFile::instance();
        const std::map<std::string, std::unordered_map<std::string, std::string>> initial_data =
            {{"Unloaded", {{"Key", "Value"}}}};
        if (ini_file.serialize_with_data(initial_data) != "\n[Unloaded]\nKey = Value\n" ||
            !ini_file.getData().empty() || ini_file.hasPendingChanges())
            throw std::runtime_error("Pure serialization requires no configured filename or state change.");
        bool stock_refused=false;
        try { (void)ini_file.serialize_from_stock(kSemanticVersion,false); }
        catch (const std::runtime_error &) { stock_refused=true; }
        if (!stock_refused || !ini_file.getData().empty() || ini_file.hasPendingChanges())
            throw std::runtime_error("Unconfigured stock preview must refuse without initializing state.");
        ini_file.set_filename(test_ini.string());

        run_test(ini_file,
                 "test_reading()",
                 [&]()
                 {
                     test_reading(ini_file, test_ini.string());
                 });

        run_test(ini_file,
                 "test_serialization()",
                 [&]()
                 {
                     test_serialization(ini_file);
                 });

        run_test(ini_file,
                 "test_key_erasure()",
                 [&]()
                 {
                     test_key_erasure(ini_file, test_ini.string());
                 });

        run_test(ini_file,
                 "test_writing()",
                 [&]()
                 {
                     test_writing(ini_file, test_ini.string());
                 });

        run_test(ini_file,
                 "test_malformed_entries()",
                 [&]()
                 {
                     test_malformed_entries(ini_file);
                 });

        run_test(ini_file,
                 "test_exceptions()",
                 [&]()
                 {
                     test_exceptions(ini_file);
                 });

        run_test(ini_file,
                 "test_reset_to_stock()",
                 [&]()
                 {
                     test_reset_to_stock(ini_file, test_ini.string());
                 });

        run_test(ini_file, "test_stock_serialization()", [&]() {
            test_stock_serialization(ini_file);
        });
        run_test(ini_file,"test_file_reader()",[&]{test_file_reader(ini_file);});
        run_test(ini_file,"test_file_writer()",[&]{test_file_writer(ini_file);});

        run_test(ini_file,
                 "test_repair_from_stock()",
                 [&]()
                 {
                     test_repair_from_stock(ini_file, test_ini.string());
                 });

        std::cout << std::endl
                  << "All tests passed." << std::endl;

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Test harness failed: " << e.what() << std::endl;
        return 1;
    }
}
