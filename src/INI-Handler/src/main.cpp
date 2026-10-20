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
        ini_file.set_filename(test_ini.string());

        run_test(ini_file,
                 "test_reading()",
                 [&]()
                 {
                     test_reading(ini_file, test_ini.string());
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
