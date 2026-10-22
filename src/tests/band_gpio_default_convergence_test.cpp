#include "config_handler.hpp"
#include "wspr_band_lookup.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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

    std::string read_text_file(const std::string &path)
    {
        std::ifstream in(path);
        require(in.is_open(), "test helper must open " + path);
        return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
    }

    void require_all_band_gpio_disabled(
        const std::array<BandGPIOConfig, HAM_BAND_COUNT> &band_gpio,
        const std::string &message)
    {
        for (int band_index = 0; band_index < HAM_BAND_COUNT; ++band_index)
        {
            const BandGPIOConfig &band_config = band_gpio[band_index];
            require(
                band_config.gpio == -1 &&
                    !band_config.enabled &&
                    !band_config.active_high,
                message + " for " +
                    std::string(band_to_string(static_cast<HamBand>(band_index))));
        }
    }
} // namespace

int main()
{
    set_patch_all_from_web_runtime_apply_suppressed_for_test(true);

    init_default_config();
    require_all_band_gpio_disabled(
        config.band_gpio,
        "init_default_config must disable all Band GPIO defaults");

    config_to_json();
    for (int band_index = 0; band_index < HAM_BAND_COUNT; ++band_index)
    {
        const std::string band_name =
            band_to_string(static_cast<HamBand>(band_index));
        require(
            jConfig["Band GPIO"][band_name].value("GPIO", 0) == -1 &&
                !jConfig["Band GPIO"][band_name].value("Enabled", true) &&
                !jConfig["Band GPIO"][band_name].value("Active High", true),
            "config_to_json must persist disabled Band GPIO defaults for " + band_name);
    }

    init_config_json();
    json_to_config();
    require_all_band_gpio_disabled(
        config.band_gpio,
        "init_config_json/json_to_config must normalize Band GPIO defaults to disabled");

    jConfig.erase("Band GPIO");
    json_to_config();
    require_all_band_gpio_disabled(
        config.band_gpio,
        "missing Band GPIO section must normalize to disabled defaults");

    config_to_json();
    patch_all_from_web({
        {"Band GPIO",
         {{"20m", {{"GPIO", 17}, {"Enabled", true}, {"Active High", true}}},
          {"40m", {{"GPIO", 27}, {"Enabled", true}, {"Active High", false}}}}}
    });
    require(
        config.band_gpio[ham_band_index(HamBand::BAND_20M)].gpio == 17 &&
            config.band_gpio[ham_band_index(HamBand::BAND_20M)].enabled &&
            config.band_gpio[ham_band_index(HamBand::BAND_20M)].active_high &&
            config.band_gpio[ham_band_index(HamBand::BAND_40M)].gpio == 27 &&
            config.band_gpio[ham_band_index(HamBand::BAND_40M)].enabled &&
            !config.band_gpio[ham_band_index(HamBand::BAND_40M)].active_high,
        "explicit Band GPIO mappings must still be honored");

    WSPRBandLookup lookup;
    require(
        lookup.lookup_ham_band(14095600.0).has_value() &&
            *lookup.lookup_ham_band(14095600.0) == HamBand::BAND_20M,
        "band lookup must still resolve 20m dial frequencies normally");
    require(
        lookup.lookup_ham_band(3570100.0).has_value() &&
            *lookup.lookup_ham_band(3570100.0) == HamBand::BAND_80M,
        "band lookup must still resolve 80m dial frequencies normally");

    const std::string stock_ini =
        read_text_file("/home/pi/WsprryPi/config/wsprrypi.ini");
    require(
        stock_ini.find("2200m = \n2200m Active High = false") != std::string::npos &&
            stock_ini.find("20m = \n20m Active High = false") != std::string::npos &&
            stock_ini.find("2m =\n2m Active High = false") != std::string::npos &&
            stock_ini.find("Active High = true") == std::string::npos,
        "stock INI must declare explicit disabled Band GPIO defaults for every band");

    std::cout << "band_gpio_default_convergence_test passed" << std::endl;
    return EXIT_SUCCESS;
}
