// SPDX-License-Identifier: MIT
#include "solar_configuration.hpp"
#include "version.hpp"
#include "backend_capabilities.hpp"
#include "wtp_runtime_bridge.hpp"
#include "WSPR-Transmitter/src/gpio_band_policy.hpp"
#include "WSPR-Transmitter/src/legacy_gpio_clock_model.hpp"
#include "WSPR-Transmitter/src/rp1_gpclk_planner.hpp"
#include "WSPR-Transmitter/src/rp1_gpclk_backend.hpp"
#include "Chipset-Offsets/src/chipset_offsets.hpp"
#include "WSPR-Transmitter/src/si5351_planner.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>
namespace wsprrypi {
bool solar_backend_frequencies(const ArgParserConfig &cfg,ModeType mode,std::span<const std::uint64_t> tones){
    try{
        if((mode!=ModeType::WSPR&&mode!=ModeType::QRSS&&mode!=ModeType::FSKCW&&mode!=ModeType::DFCW)||tones.empty()||!transmit_backend_is_compiled(cfg.transmit_backend))return false;
        const auto expected=mode==ModeType::WSPR?4U:mode==ModeType::QRSS?1U:2U;
        if(tones.size()!=expected)return false;
        const auto wiremode=mode==ModeType::WSPR?"wspr":mode==ModeType::QRSS?"qrss":mode==ModeType::FSKCW?"fskcw":"dfcw";
        const auto native=mode==ModeType::WSPR?TransmissionMode::WSPR:mode==ModeType::QRSS?TransmissionMode::QRSS:mode==ModeType::FSKCW?TransmissionMode::FSKCW:TransmissionMode::DFCW;
        const auto backend=cfg.transmit_backend==TransmitBackendKind::SIMULATED?BackendKind::SIMULATED:cfg.transmit_backend==TransmitBackendKind::SI5351?BackendKind::SI5351:cfg.transmit_backend==TransmitBackendKind::WTP?BackendKind::WTP:cfg.transmit_backend==TransmitBackendKind::RP1_GPCLK?BackendKind::RP1_GPCLK:BackendKind::RPI_CLOCK_GPIO;
        HardwareProfile profile=HardwareProfile::UNSPECIFIED;std::optional<LegacyGpioProcessorProfile> processor;
        if(backend==BackendKind::RPI_CLOCK_GPIO){const auto generation=get_raspberry_pi_generation();if(generation==1)processor=LegacyGpioProcessorProfile::Bcm2835;else if(generation==2||generation==3)processor=LegacyGpioProcessorProfile::Bcm2836Bcm2837;else if(generation==4)processor=LegacyGpioProcessorProfile::Bcm2711;else return false;profile=legacyHardwareProfile(*processor);}
        if(backend==BackendKind::RP1_GPCLK)profile=HardwareProfile::RP1_GPCLK;
        if(backend==BackendKind::SI5351)profile=HardwareProfile::SI5351;
        nlohmann::json remote;
        // Called by the configuration owner under config_update_mutex. A new
        // endpoint cannot borrow the old endpoint's cached capabilities.
        if(backend==BackendKind::WTP){
            if(cfg.wtp!=config.wtp)return false;
            remote=nlohmann::json::parse(wtp_runtime_json());
            if(!remote.value("ready",false)||!remote.contains("identity")||remote["identity"].is_null()||
               (!cfg.wtp.device_id.empty()&&remote["identity"].at("device_id")!=cfg.wtp.device_id)||
               !remote.contains("capabilities")||remote["capabilities"].is_null())return false;
            const auto &modes=remote["capabilities"]["modes"];
            if(std::find(modes.begin(),modes.end(),wiremode)==modes.end())return false;
        }
        std::vector<Si5351Planner::ToneEntry> synth;double low=INFINITY,high=0;
        for(const auto ticks:tones){
            const double hz=static_cast<double>(static_cast<long double>(ticks)/1e9L);if(!ticks||!std::isfinite(hz))return false;
            if(!evaluate_frequency_policy(backend,native,hz,cfg.allow_unqualified_frequency,cfg.allow_non_amateur_frequency,profile).allowed)return false;
            if(backend==BackendKind::SI5351&&(hz<Si5351Planner::minimum_output_hz||hz>Si5351Planner::maximum_output_hz))return false;
            if(backend==BackendKind::RP1_GPCLK&&(hz<kRp1GpclkNominalParentFrequencyHz/65535.99998474121||hz>kRp1GpclkMaximumDirectOutputHz))return false;
            if(backend==BackendKind::WTP){bool found=false;for(const auto &range:remote["capabilities"]["frequency_ranges"]){const auto decimal=[](const nlohmann::json &v){const auto text=v.get<std::string>();std::uint64_t value{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!value)throw std::invalid_argument("invalid_frequency_range");return value;};const auto minimum=decimal(range.at("minimum_nhz")),maximum=decimal(range.at("maximum_nhz"));if(minimum>maximum)return false;if(ticks>=minimum&&ticks<=maximum)found=true;}if(!found)return false;}
            low=std::min(low,hz);high=std::max(high,hz);synth.push_back({hz});
        }
        // The current Pi compatibility path constructs four numerical tones
        // even for CW. Validate that table without qualifying its unemitted
        // tones as RF output and without preparing or writing any hardware.
        const double spacing=mode==ModeType::FSKCW||mode==ModeType::DFCW?high-low:1.46484375;
        if(!std::isfinite(spacing)||spacing<=0)return false;
        const double table_high=mode==ModeType::WSPR?high:low+3*spacing;
        if(processor){(void)selectLegacyGpioClockForAdditionalCorrection(*processor,low,table_high,cfg.ppm);}
#if WSPRRYPI_BACKEND_RP1_GPCLK
        if(backend==BackendKind::RP1_GPCLK){
            Rp1GpclkPlannerInput settings;
            settings.center_frequency_hz=low+1.5*spacing;
            settings.tone_spacing_hz=spacing;
            settings.parent_frequency_hz=kRp1GpclkNominalParentFrequencyHz;
            settings.source_rate_ppm=cfg.ppm;
            settings.intrinsic_source_rate_ppm=chipsetIntrinsicOffsetPpm(ClockChipset::Rp1);
            settings.dither_sequence_length=Rp1GpclkBackend::kWritesPerSymbol;
            if(!planRp1GpclkWspr(settings).ok)return false;
        }
#endif

#if WSPRRYPI_BACKEND_SI5351
        if(backend==BackendKind::SI5351){Si5351Planner::Config settings;settings.reference_hz=cfg.si5351_reference_hz;settings.calibration_ppm=cfg.si5351_ppm;settings.tx_output=static_cast<Si5351Device::Output>(cfg.si5351_tx_output);Si5351Planner planner(settings);const auto selected=mode==ModeType::WSPR?Si5351Planner::Mode::WSPR:mode==ModeType::QRSS?Si5351Planner::Mode::QRSS:mode==ModeType::FSKCW?Si5351Planner::Mode::FSKCW:Si5351Planner::Mode::DFCW;const auto plan=planner.buildPlan(selected,synth);if(plan.tone_sets.size()!=tones.size())return false;}

#endif
        return true;
    }catch(...){return false;}
}
}
