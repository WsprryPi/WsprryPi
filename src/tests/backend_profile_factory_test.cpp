#include "backend_capabilities.hpp"
#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "gpio_output.hpp"
#include "wspr_transmit.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace
{
bool compiled(wsprrypi::BackendKind backend)
{
    switch (backend)
    {
    case wsprrypi::BackendKind::RPI_CLOCK_GPIO:
        return WSPRRYPI_BACKEND_RPI_GPIO;
    case wsprrypi::BackendKind::RP1_GPCLK:
        return WSPRRYPI_BACKEND_RP1_GPCLK;
    case wsprrypi::BackendKind::SI5351:
        return WSPRRYPI_BACKEND_SI5351;
    case wsprrypi::BackendKind::WTP:
        return false;
    case wsprrypi::BackendKind::SIMULATED:
        return WSPRRYPI_BACKEND_SIMULATED;
    }
    return false;
}
}

int main()
{
    WsprTransmitter transmitter;
    if (transmitter.hasSelectedBackend())
        throw std::runtime_error(
            "transmitter construction selected a backend before platform validation");
    transmitter.clearExecutionStateAfterStop();
    TransmissionRequest unselected_request;
    unselected_request.skip_window = true;
    bool rejected_unselected_execution = false;
    try
    {
        transmitter.configureExecution(unselected_request);
    }
    catch (const std::logic_error &error)
    {
        rejected_unselected_execution =
            std::string(error.what()).find("explicitly selected backend") !=
            std::string::npos;
    }
    if (!rejected_unselected_execution)
        throw std::runtime_error(
            "execution without an explicitly selected backend did not fail closed");
    constexpr std::array backends{
        wsprrypi::BackendKind::RPI_CLOCK_GPIO,
        wsprrypi::BackendKind::RP1_GPCLK,
        wsprrypi::BackendKind::SI5351,
        wsprrypi::BackendKind::SIMULATED,
        wsprrypi::BackendKind::WTP,
    };

    for (const auto backend : backends)
    {
        try
        {
            transmitter.selectBackend(backend);
            if (!compiled(backend))
                throw std::runtime_error("omitted backend selection unexpectedly succeeded");
            if (!transmitter.hasSelectedBackend())
                throw std::runtime_error("explicit backend selection did not create a backend");
        }
        catch (const std::invalid_argument &error)
        {
            if (compiled(backend))
                throw;
            const std::string message = error.what();
            if (message.find("unavailable in this build") == std::string::npos ||
                message.find(WSPRRYPI_COMPILED_BACKENDS) == std::string::npos)
            {
                throw std::runtime_error("omitted backend diagnostic lacks compiled capabilities");
            }
        }
    }

    constexpr std::array config_backends{
        TransmitBackendKind::GPIO,
        TransmitBackendKind::RP1_GPCLK,
        TransmitBackendKind::SI5351,
        TransmitBackendKind::SIMULATED,
    };
    for (const auto backend : config_backends)
    {
        if (transmit_backend_is_compiled(backend))
            continue;

        ArgParserConfig candidate;
        candidate.transmit_backend = backend;
        std::string error;
        if (validate_config_candidate(candidate, &error))
            throw std::runtime_error("omitted configuration backend unexpectedly validated");
        if (error != transmit_backend_unavailable_message(backend))
            throw std::runtime_error("omitted configuration backend diagnostic mismatch");
    }

    if (!transmit_backend_requires_root(TransmitBackendKind::GPIO))
        throw std::runtime_error("GPIO backend unexpectedly permits non-root execution");
    if (!transmit_backend_requires_root(TransmitBackendKind::RP1_GPCLK))
        throw std::runtime_error("RP1 GPCLK backend unexpectedly permits non-root execution");
    if (transmit_backend_requires_root(TransmitBackendKind::SIMULATED))
        throw std::runtime_error("simulated backend unexpectedly requires root");

    const bool gpio_capable = WSPRRYPI_BACKEND_RPI_GPIO ||
        WSPRRYPI_BACKEND_RP1_GPCLK || WSPRRYPI_ANCILLARY_GPIO;
    if (build_has_physical_gpio_capability() != gpio_capable)
        throw std::runtime_error("build GPIO privilege capability mismatch");
    if (transmit_backend_requires_root(TransmitBackendKind::SI5351) != gpio_capable)
        throw std::runtime_error("Si5351 privilege policy does not match GPIO capabilities");

    if (!WSPRRYPI_ANCILLARY_GPIO)
    {
        if (!ledControl.toggleGPIO(false))
            throw std::runtime_error("unavailable GPIO cleanup was not a successful no-op");
        if (ledControl.toggleGPIO(true))
            throw std::runtime_error("unavailable GPIO assertion unexpectedly succeeded");

        // Remaining INI fixtures specifically select Si5351. Its omission was
        // already checked above; do not require it in the portable profile.
        if (!WSPRRYPI_BACKEND_SI5351)
            return 0;

        ArgParserConfig compatible_disabled_config;
        compatible_disabled_config.transmit_backend = TransmitBackendKind::SI5351;
        compatible_disabled_config.use_ini = true;
        compatible_disabled_config.transmit = false;
        std::string compatible_error;
        if (!validate_config_candidate(
                compatible_disabled_config,
                &compatible_error))
        {
            throw std::runtime_error(
                "disabled ancillary GPIO defaults were rejected: " +
                compatible_error);
        }

        const auto expect_ancillary_rejection = [](ArgParserConfig candidate)
        {
            std::string error;
            if (validate_config_candidate(candidate, &error))
                throw std::runtime_error("ancillary GPIO configuration unexpectedly validated");
            if (error.find("Ancillary GPIO is unavailable in this build") == std::string::npos)
                throw std::runtime_error("ancillary GPIO diagnostic mismatch");
        };

        ArgParserConfig candidate;
        candidate.transmit_backend = TransmitBackendKind::SI5351;
        candidate.use_led = true;
        expect_ancillary_rejection(candidate);
        candidate = ArgParserConfig{};
        candidate.transmit_backend = TransmitBackendKind::SI5351;
        candidate.use_amp = true;
        expect_ancillary_rejection(candidate);
        candidate = ArgParserConfig{};
        candidate.transmit_backend = TransmitBackendKind::SI5351;
        candidate.use_shutdown = true;
        expect_ancillary_rejection(candidate);
        candidate = ArgParserConfig{};
        candidate.transmit_backend = TransmitBackendKind::SI5351;
        candidate.band_gpio.front().enabled = true;
        expect_ancillary_rejection(candidate);
    }
}
