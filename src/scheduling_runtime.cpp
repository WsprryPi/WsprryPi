/**
 * @file scheduling_runtime.cpp
 * @brief Owns transmitter callbacks and process runtime lifecycle.
 */

#include "scheduling.hpp"
#include "wtp_runtime_bridge.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "wtp_integration/fleet_runtime.hpp"
#include "wtp_pi_control.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "scheduling_internal.hpp"

#include "gpio_input.hpp"
#include "gpio_output.hpp"
#include "logging.hpp"
#include "machine_power_control.hpp"
#include "ppm_manager.hpp"
#include "privileged_network_runtime.hpp"
#include "rp1_route_bridge.hpp"
#include "runtime_config_operations.hpp"
#include "signal_handler.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "version.hpp"
#include "web_server.hpp"
#include "web_socket.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

static std::atomic<bool> transmission_runtime_failed{false};

static std::string format_elapsed(double elapsed)
{
    if (elapsed == 0.0)
        return std::string();

    std::ostringstream oss;
    oss << std::fixed
        << std::setprecision(6)
        << elapsed;
    return oss.str();
}

constexpr LogLevel to_log_level(WsprTransmitLogLevel level)
{
    switch (level)
    {
    case WsprTransmitLogLevel::DEBUG:
        return LogLevel::DEBUG;
    case WsprTransmitLogLevel::INFO:
        return LogLevel::INFO;
    case WsprTransmitLogLevel::WARN:
        return LogLevel::WARN;
    case WsprTransmitLogLevel::ERROR:
        return LogLevel::ERROR;
    case WsprTransmitLogLevel::FATAL:
        return LogLevel::FATAL;
    }

    return LogLevel::INFO; // Safe fallback
}

void transmitter_cb(WsprTransmissionCallbackEvent event,
                    WsprTransmitLogLevel level,
                    const std::string &msg,
                    double value)
{
    switch (event)
    {
    case WsprTransmissionCallbackEvent::STARTING:
    {
        const double frequency = value;

        if (config.mode == ModeType::WSPR &&
            (!active_wspr_plan_in_progress || active_wspr_frame_index == 0U))
        {
            consume_tx_iteration_if_needed();
        }

        if (config.transmit_backend != TransmitBackendKind::WTP)
            assert_transmit_gpio_outputs("transmission start");

        // Notify clients of start.
        send_ws_message("transmit", "starting");

        // Log messages.
        if (!msg.empty() && frequency != 0.0)
        {
            llog.logS(to_log_level(level),
                      "Started transmission (",
                      msg,
                      ") ",
                      transmitter_format_frequency_mhz(frequency),
                      " MHz",
                      active_gpio_log_suffix(),
                      ".");
        }
        else if (frequency != 0.0)
        {
            if (config.mode == ModeType::QRSS)
            {
                llog.logS(to_log_level(level),
                          "Started QRSS transmission at frequency: ",
                          transmitter_format_frequency_mhz(config.qrss.frequency_hz),
                          " MHz",
                          active_gpio_log_suffix(),
                          ".");
            }
            else if (config.mode == ModeType::FSKCW)
            {
                llog.logS(to_log_level(level),
                          "Started FSKCW transmission at mark frequency: ",
                          transmitter_format_frequency_mhz(config.fskcw.mark_frequency_hz),
                          " MHz",
                          active_gpio_log_suffix(),
                          ".");
            }
            else if (config.mode == ModeType::DFCW)
            {
                llog.logS(to_log_level(level),
                          "Started DFCW transmission at dot frequency: ",
                          transmitter_format_frequency_mhz(config.dfcw.dot_frequency_hz),
                          " MHz",
                          active_gpio_log_suffix(),
                          ".");
            }
            else
            {
                llog.logS(to_log_level(level),
                          "Started transmission: ",
                          transmitter_format_frequency_mhz(frequency),
                          " MHz",
                          active_gpio_log_suffix(),
                          ".");
            }
        }
        else if (!msg.empty())
        {
            llog.logS(to_log_level(level),
                      "Started transmission (",
                      msg,
                      ").");
        }
        else
        {
            llog.logS(to_log_level(level),
                      "Started transmission.");
        }
        break;
    }

    case WsprTransmissionCallbackEvent::PROGRESS:
    {
        send_ws_message(
            "transmit",
            "progress",
            std::string(),
            static_cast<int>(value));
        break;
    }

    case WsprTransmissionCallbackEvent::COMPLETE:
    {
        if (config.transmit_backend == TransmitBackendKind::WTP && config.mode == ModeType::WSPR &&
            (!active_wspr_plan_in_progress || active_wspr_frame_index == 0U))
            consume_tx_iteration_if_needed();
        const double elapsed = value;
        bool do_config = true;
        const bool deferred_reload_pending =
            ini_reload_pending.load(std::memory_order_acquire);

        const std::string s_elapsed = format_elapsed(elapsed);
        if (!msg.empty() && elapsed != 0.0)
        {
            llog.logS(to_log_level(level),
                      "Completed transmission (",
                      msg,
                      ") ",
                      s_elapsed,
                      " seconds.");
        }
        else if (elapsed != 0.0)
        {
            llog.logS(to_log_level(level),
                      "Completed transmission: ",
                      s_elapsed,
                      " seconds.");
        }
        else if (!msg.empty())
        {
            llog.logS(to_log_level(level),
                      "Completed transmission (",
                      msg,
                      ").");
        }
        else
        {
            llog.logS(to_log_level(level),
                      "Completed transmission.");
        }

        finalize_transmission_stop_cleanup(
            &config,
            runtime_should_hold_selector_gpios_initialized(config),
            "transmission completion");

        // Notify the websocket clients.
        send_ws_message("transmit", "finished");

        const bool shutdown_when_idle =
            shutdown_after_current_transmission.exchange(false, std::memory_order_acq_rel);
        const bool shutdown_when_plan_finishes =
            shutdown_after_wspr_plan.load(std::memory_order_acquire);

        if (deferred_reload_pending)
        {
            reset_active_wspr_plan_state();
        }
        else if (do_config && active_wspr_plan_has_more_frames_after_current())
        {
            ++active_wspr_frame_index;
        }
        else if (active_wspr_plan_in_progress)
        {
            reset_active_wspr_plan_state();
        }

        if (shutdown_when_idle && do_config)
        {
            request_wspr_shutdown("completed configured TX iterations");
            do_config = false;
        }
        else if (shutdown_when_plan_finishes && do_config &&
                 !active_wspr_plan_in_progress)
        {
            shutdown_after_wspr_plan.store(false, std::memory_order_release);
            request_wspr_shutdown("completed configured TX iterations");
            do_config = false;
        }
        else if (deferred_reload_pending && do_config)
        {
            set_config();
            do_config = false;
        }
        else if (do_config &&
                 config.mode != ModeType::WSPR &&
                 config.mode != ModeType::TONE &&
                 !has_non_wspr_cli_startup_request(config.mode))
        {
            schedule_next_non_wspr_launch(config);
            do_config = false;
        }

        // Set config will determine if we have work to do.
        if (do_config)
        {
            set_config();
        }

        break;
    }

    case WsprTransmissionCallbackEvent::FAILED:
    {
        transmission_runtime_failed.store(true, std::memory_order_release);
        const std::string reason = msg.empty()
            ? "Transmission backend execution failed."
            : msg;
        llog.logS(ERROR, "Transmission failed: ", reason);

        finalize_transmission_stop_cleanup(
            &config,
            false,
            "transmission failure",
            true,
            true);
        send_ws_message("transmit", "failed", reason);

        if (config.use_ini)
        {
            set_managed_reload_tx_inhibited(true);
            llog.logS(
                ERROR,
                "Transmit is inhibited until configuration is reloaded or the service is restarted.");
        }
        else
        {
            request_wspr_shutdown("transmission backend failure");
        }
        break;
    }

    case WsprTransmissionCallbackEvent::CANCELLED:
    {
        const double elapsed = value;
        const std::string s_elapsed = format_elapsed(elapsed);
        const bool suppress_ws_event =
            suppress_cancelled_ws_event_for_user_stop.exchange(
                false,
                std::memory_order_acq_rel);

        llog.logS(to_log_level(level),
                  "Transmission canceled after ",
                  s_elapsed,
                  " seconds.");

        finalize_transmission_stop_cleanup(
            &config,
            runtime_should_hold_selector_gpios_initialized(config),
            "transmission cancellation",
            true);
        if (suppress_ws_event)
        {
            llog.logS(
                DEBUG,
                "Suppressing websocket canceled event because an explicit user stop will publish stopped.");
        }
        else
        {
            send_ws_message("transmit", "canceled");
        }

        break;
    }

    case WsprTransmissionCallbackEvent::SKIPPED:
    {
        if (!current_transmission_request.isSkipWindow())
        {
            llog.logS(
                WARN,
                "Ignoring unexpected SKIPPED transmitter callback for non-skip request.");
            break;
        }

        // Return transmit GPIO outputs to the same idle path used by every
        // terminal transmitter event.
        deassert_transmit_gpio_outputs(
            &config,
            runtime_should_hold_selector_gpios_initialized(config),
            "transmission skip");

        if (!msg.empty())
            llog.logS(to_log_level(level), msg, ".");
        else
            llog.logS(to_log_level(level), "Skipping transmission.");

        // Notify websocket clients.
        send_ws_message("transmit", "skipped");

        shutdown_after_current_transmission.store(false, std::memory_order_release);
        shutdown_after_wspr_plan.store(false, std::memory_order_release);
        reset_active_wspr_plan_state();

        // Advance to the next configured slot.
        set_config();

        break;
    }

    case WsprTransmissionCallbackEvent::LOGGING:
    default:
    {
        if (!msg.empty())
            llog.logS(to_log_level(level), msg);

        break;
    }
    }
}

/**
 * @brief  Callback invoked when PPMManager has a new PPM reading.
 *
 * @details
 * Sets the `ppm_reload_pending` flag so that downstream consumers
 * will pick up the new value and marks the provider estimate as available
 * after the adapter has delivered a measurement.
 *
 * @param new_ppm  The latest PPM correction value (ignored here; reload
 *                 logic will pull it from PPMManager when needed).
 */
void ppm_callback(double /*new_ppm*/)
{
    refresh_frequency_estimate_for_config();
    // Notify other subsystems to reload/recalibrate with the fresh PPM.
    ppm_reload_pending.store(true, std::memory_order_relaxed);

    // Record that the provider adapter has produced a value.
    if (!config.frequency_estimate_good)
    {
        llog.logS(DEBUG, "The system-clock frequency estimate provider has updated its value.");
        config.frequency_estimate_good = true;
    }
}

/**
 * @brief   Initialize the PPM subsystem.
 *
 * Registers the PPM callback, initializes the PPMManager, and handles
 * any returned status. Qualification remains a soft gate; unavailable or
 * converging provider state is handled by deterministic correction fallback.
 *
 * @return  true if initialization is considered successful;
 *          false only on a fatal PPM error (e.g. excessive drift).
 */
bool ppm_init()
{
    bool retval = true;

    // Register the PPM update callback
    ppmManager.setPPMCallback(ppm_callback);

    // Perform the normal initialization
    PPMStatus status = ppmManager.initialize();

    switch (status)
    {
    case PPMStatus::SUCCESS:
        llog.logS(INFO, "PPM Manager initialized successfully.");
        break;

    case PPMStatus::WARNING_HIGH_PPM:
        llog.logE(ERROR, "Measured PPM exceeds safe threshold.");
        return false;

    case PPMStatus::ERROR_CHRONY_NOT_FOUND:
        llog.logE(WARN,
                  "chrony estimate unavailable; GPIO correction fallback policy will apply.");
        break;

    case PPMStatus::ERROR_UNSYNCHRONIZED_TIME:
        llog.logE(WARN, "System-clock frequency estimate provider is not synchronized; fallback policy will apply.");
        break;

    default:
        llog.logE(WARN, "Unknown PPMStatus returned from initialize().");
        break;
    }

    return retval;
}

/**
 * @brief Callback function triggered to perform a system shutdown sequence.
 *
 * @details
 * This function is intended to be called when a shutdown GPIO event is triggered.
 * It logs the event and calls shutdown_system().
 */
void callback_shutdown_system()
{
    llog.logS(INFO, "Shutdown called by GPIO", config.shutdown_pin);
    shutdown_system();
}

/**
 * @brief Perform a system shutdown sequence.
 *
 * @details
 * This function is intended to be called when a shutdown event is triggered.
 * It performs a visual blink pattern on the LED pin if configured, sets the
 * shutdown flags, and notifies all threads waiting on the shutdown condition
 * variable.
 *
 * Specifically:
 * - Toggles the LED 3 times with 200ms intervals.
 * - Sets `exitwspr_cv` to break out of the main transmission loop.
 * - Sets `shutdown_flag` to mark that a full system shutdown is in progress.
 *
 * @note
 * The LED toggling uses `ledControl.toggleGPIO()` and assumes the hardware
 * supports it.
 */
void shutdown_system()
{
    shutdown_flag.store(true, std::memory_order_relaxed);
    request_wspr_shutdown("system power-off requested");

    if (config.use_led)
    {
        // Flash LED three times if configured
        for (int i = 0; i < 3; ++i)
        {
            set_tx_led_state(true, "shutdown blink on");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            set_tx_led_state(false, "shutdown blink off");
            if (i < 2)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
    }
}

/**
 * @brief Perform a system reboot sequence.
 *
 * @details
 * This function is intended to be called when a reboot event is triggered.
 * It performs a visual blink pattern on the LED pin if configured, sets the
 * reboot flags, and notifies all threads waiting on the reboot condition
 * variable.
 *
 * Specifically:
 * - Toggles the LED 2 times with 100ms intervals.
 * - Sets `exitwspr_cv` to break out of the main transmission loop.
 * - Sets `reboot_flag` to mark that a full system reboot is in progress.
 *
 * @note
 * The LED toggling uses `ledControl.toggleGPIO()` and assumes the hardware supports it.
 */
void reboot_system()
{
    reboot_flag.store(true, std::memory_order_relaxed);
    request_wspr_shutdown("System reboot requested");

    if (config.use_led)
    {
        // Flash LED two times if configured
        for (int i = 0; i < 2; ++i)
        {
            set_tx_led_state(true, "reboot blink on");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            set_tx_led_state(false, "reboot blink off");
            if (i < 2)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}

void stop_runtime_components_for_process_exit() noexcept
{
    ini_reload_pending.store(false, std::memory_order_relaxed);

    webServer.stop();
    socketServer.stop();
    stop_runtime_config_monitor();
    wsprrypi::stop_wtp_fleet();
    wsprrypi::stop_wtp_pi_endpoint();
    shutdownMonitor.stop();
    ppmManager.stop();
    transmitter_shutdown_for_process_exit();
    deassert_transmit_gpio_outputs(
        &config,
        false,
        "process-exit shutdown");
    shutdown_all_configured_selector_gpios(config);
    ampControl.stop();
    ledControl.stop();
}

void stop_runtime_components_for_test() noexcept
{
    webServer.stop();
    socketServer.stop();
    stop_runtime_config_monitor();
    wsprrypi::stop_wtp_fleet();
    wsprrypi::stop_wtp_pi_endpoint();
    shutdownMonitor.stop();
    ppmManager.stop();
    transmitter_stop_and_join();
    deassert_transmit_gpio_outputs(
        &config,
        false,
        "test runtime stop");
    ampControl.stop();
    ledControl.stop();
    release_idle_selector_gpio_reservations();
}

/**
 * @brief Main orchestration loop for startup, scheduling, and shutdown.
 *
 * @details
 * This loop validates configuration, starts long-lived services, prepares
 * the initial committed execution request, and then runs until shutdown.
 * WSPR startup goes through the same reload-safe scheduling path used for
 * later reconfiguration so request construction remains centralized here.
 *
 * @note This function blocks until `exitwspr_cv` is set by another thread.
 */
bool wspr_loop()
{
    transmission_runtime_failed.store(false, std::memory_order_release);
    const bool any_selector_gpio_configured =
        has_configured_selector_gpios(config);

    selector_gpio_control_enabled = any_selector_gpio_configured;
    selector_gpio_drive_enabled = any_selector_gpio_configured;
    bandGPIOSelector.setEnabled(selector_gpio_control_enabled);
    bandGPIOSelector.setDriveGPIO(selector_gpio_drive_enabled);

    // Display the final configuration after parsing arguments and INI file.
    show_config_values(false);

    const bool startup_config_handoff = consume_startup_config_handoff();
    apply_managed_startup_policy_if_requested(startup_config_handoff);
    set_startup_diagnostic_deferral(true);

    if (config.mode != ModeType::WSPR)
    {
        if (startup_config_handoff)
        {
            apply_runtime_config_side_effects();
        }
        else
        {
            validate_config_data();
        }
    }
    else
    {
        // Validate the startup WSPR configuration before any long-lived
        // services are started so malformed CLI frequency lists fail cleanly.
        if (startup_config_handoff)
        {
            apply_runtime_config_side_effects();
        }
        else
        {
            validate_config_data();
        }
    }

    // Backend selection occurs while loading the validated runtime
    // configuration.  Quiesce it before any service, scheduler, selector, or
    // automatic-transmit path can run.  This latch is process-lifetime by
    // design: ordinary reloads and toggles cannot clear a hardware-safety
    // failure; Pico retains its separate explicit reconciliation boundary.
    if (!run_startup_quiesce_gate(config))
    {
        if (config.transmit_backend == TransmitBackendKind::WTP)
            llog.logS(ERROR, "Pico startup is not ready; inspect endpoint status and explicitly reconcile before scheduling.");
        else
            llog.logS(ERROR, "Startup transmission inhibition latched: ", startup_quiesce_error_state());
    }

    if (config.transmit_backend == TransmitBackendKind::RP1_GPCLK)
    {
        const auto reconciliation = reconcile_rp1_idle_startup(config.gpio_tx_pin);
        if (!reconciliation.ok)
        {
            llog.logS(ERROR,
                "RP1 GPCLK startup reconciliation failed; transmission remains inhibited: ",
                reconciliation.message);
        }
        else if (reconciliation.policy_domain == "startup-idle")
        {
            llog.logS(INFO,
                "RP1 GPCLK route reconciled for safe idle startup; exact provider "
                "identity and operation-scoped authorization remain required.");
        }
    }

    if (config.use_ini && !startup_quiesce_inhibited_state())
    {
        std::string endpoint_error;
        if (!wsprrypi::start_wtp_pi_endpoint(config, &endpoint_error))
            llog.logS(ERROR, "WTP Pi endpoint unavailable: ", endpoint_error);
    }

    if (config.use_ini) wsprrypi::start_wtp_fleet(config.ini_filename);
    const bool start_web = web_server_start_enabled(config);
    const bool start_websocket = websocket_server_start_enabled(config);
    const int startup_web_port = config.web_port;
    const uint16_t startup_socket_port = config.socket_port;
    const bool startup_socket_loopback_only = config.socket_loopback_only;
    const auto startup_socket_loopback_family = config.socket_loopback_family;
    if (!config.enable_web)
    {
        llog.logS(INFO, "Web UI disabled via CLI (--no-web)");
    }
    else if (start_web)
    {
        try
        {
            webServer.start(startup_web_port);
            webServer.setThreadPriority(SCHED_RR, 10);
        }
        catch (const std::exception &error)
        {
            llog.logE(ERROR, "Unable to start HTTP listener: ", error.what());
            webServer.stop();
            stop_runtime_components_for_process_exit();
            return false;
        }
        const auto listener_deadline = std::chrono::steady_clock::now() +
                                       std::chrono::seconds(5);
        while (!webServer.isListening() &&
               !exiting_wspr.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < listener_deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!webServer.isListening())
        {
            llog.logE(ERROR, "HTTP listener failed to bind or listen.");
            webServer.stop();
            stop_runtime_components_for_process_exit();
            return false;
        }
        llog.logS(INFO, "HTTP listener started on port ", startup_web_port, ".");
    }
    else
    {
        llog.logS(DEBUG, "Skipping web server.");
    }

    if (start_websocket)
    {
        if (!socketServer.start(
                startup_socket_port,
                SOCKET_KEEPALIVE,
                startup_socket_loopback_only,
                startup_socket_loopback_family))
        {
            llog.logE(ERROR, "WebSocket listener failed to start.");
            stop_runtime_components_for_process_exit();
            return false;
        }
        socketServer.setThreadPriority(SCHED_RR, 10);
    }
    else if (!start_websocket)
    {
        llog.logS(DEBUG, "Skipping socket server.");
    }

    if ((start_web || start_websocket) &&
        active_privileged_network_mode() ==
        PrivilegedNetworkMode::insecure_disabled)
    {
        llog.logS(WARN, "NETWORK SAFETY OFF");
    }
    else if (start_web || start_websocket)
    {
        const auto network_snapshot =
            SupportRequestGuard::discover_local_networks();
        if (!network_snapshot.discovery_succeeded)
        {
            llog.logS(WARN,
                "Privileged network safety is enforced; current interface "
                "discovery failed. Listeners remain available and protected "
                "non-loopback clients are rejected.");
        }
        else if (network_snapshot.networks.empty())
        {
            llog.logS(INFO,
                "Privileged network safety is enforced with no eligible LAN. "
                "Listeners remain available for loopback; protected "
                "non-loopback clients are rejected.");
        }
    }

    // Set transmission server and set priority
    transmitter_set_thread_scheduling(SCHED_FIFO, 50);

    // Set transmission event callbacks
    transmitter_set_callbacks(
        [](WsprTransmissionCallbackEvent event,
           WsprTransmitLogLevel level,
           const std::string &msg,
           double value)
        {
            transmitter_cb(event, level, msg, value);
        });

    // Monitor INI file for changes
    if (config.use_ini)
    {
        // Start INI monitor
        start_runtime_config_monitor(config.ini_filename);
    }

    llog.logS(INFO, "WSPR loop running.");
    set_startup_diagnostic_deferral(false);
    emit_deferred_startup_diagnostics();

    // Startup WSPR configuration should be applied exactly once using the
    // same reload-safe path that handles validation, setup, and scheduling.
    if (config.mode == ModeType::WSPR)
    {
        ini_reload_pending.store(!startup_config_handoff, std::memory_order_relaxed);
        if (!set_config(startup_config_handoff ? false : true))
        {
            stop_runtime_components_for_process_exit();
            return false;
        }
    }
    else if (config.mode == ModeType::TONE)
    {
        log_scheduler_path_selection(config.mode);
        WsprFrequencyEntry entry;
        double actual_rf_frequency_hz = 0.0;
        if (!try_get_direct_tone_startup_request(entry, actual_rf_frequency_hz))
        {
            llog.logE(ERROR, "Direct RF test tone requested without a startup tone request.");
            stop_runtime_components_for_process_exit();
            return false;
        }

        std::string startup_error;
        if (!start_direct_tone_execution(
                config,
                entry,
                actual_rf_frequency_hz,
                &startup_error))
        {
            llog.logS(ERROR, startup_error);
            stop_runtime_components_for_process_exit();
            return false;
        }
        llog.logS(INFO, "transmitting tone, hit Ctrl-C to terminate tone.");
    }
    else if (config.mode == ModeType::QRSS)
    {
        log_scheduler_path_selection(config.mode);
        if (has_non_wspr_cli_startup_request(config.mode))
        {
            shutdown_after_current_transmission.store(true, std::memory_order_release);
            shutdown_after_wspr_plan.store(false, std::memory_order_release);
            if (!start_non_wspr_transmission_now(config))
            {
                stop_runtime_components_for_process_exit();
                return false;
            }
        }
        else
        {
            shutdown_after_current_transmission.store(false, std::memory_order_release);
            shutdown_after_wspr_plan.store(false, std::memory_order_release);
            schedule_next_non_wspr_launch(config);
        }
    }
    else if (config.mode == ModeType::FSKCW)
    {
        log_scheduler_path_selection(config.mode);
        if (has_non_wspr_cli_startup_request(config.mode))
        {
            shutdown_after_current_transmission.store(true, std::memory_order_release);
            shutdown_after_wspr_plan.store(false, std::memory_order_release);
            if (!start_non_wspr_transmission_now(config))
            {
                stop_runtime_components_for_process_exit();
                return false;
            }
        }
        else
        {
            shutdown_after_current_transmission.store(false, std::memory_order_release);
            shutdown_after_wspr_plan.store(false, std::memory_order_release);
            schedule_next_non_wspr_launch(config);
        }
    }
    else if (config.mode == ModeType::DFCW)
    {
        log_scheduler_path_selection(config.mode);
        if (has_non_wspr_cli_startup_request(config.mode))
        {
            shutdown_after_current_transmission.store(true, std::memory_order_release);
            shutdown_after_wspr_plan.store(false, std::memory_order_release);
            if (!start_non_wspr_transmission_now(config))
            {
                stop_runtime_components_for_process_exit();
                return false;
            }
        }
        else
        {
            shutdown_after_current_transmission.store(false, std::memory_order_release);
            shutdown_after_wspr_plan.store(false, std::memory_order_release);
            schedule_next_non_wspr_launch(config);
        }
    }

    // Reboot bootstrap has no current route transaction to acknowledge. The
    // separate worker restores the route and restarts us with its fresh token.
    if (const char *restoration = std::getenv("WSPRRYPI_ROUTE_RESTORE_IDLE");
        restoration && std::getenv("WSPRRYPI_RP1_REBOOT_IDLE") == nullptr)
    {
        const auto listener_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (start_web && !webServer.isListening() &&
               std::chrono::steady_clock::now() < listener_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (startup_quiesce_inhibited_state() ||
            (start_web && !webServer.isListening()) ||
            config.transmit_backend != TransmitBackendKind::RP1_GPCLK ||
            !acknowledge_rp1_restoration(restoration, config.transmit))
        {
            llog.logS(ERROR, "Route restoration could not acknowledge idle application readiness.");
            stop_runtime_components_for_process_exit();
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // Loop (block wspr_loop only) until shutdown is triggered
    // -------------------------------------------------------------------------
    wsprrypi::activate_wtp_pi_endpoint();
    {
        std::unique_lock<std::mutex> lk(exitwspr_mtx);
        while (!exitwspr_ready) {
            exitwspr_cv.wait_for(lk, std::chrono::milliseconds(100));
            lk.unlock();
            transmitter_poll_events();
            wsprrypi::poll_wtp_pi_endpoint();
            if (ini_reload_pending.load(std::memory_order_acquire) &&
                !web_test_tone.load() && !transmitter_reload_should_defer() &&
                wtp_pi_begin_configuration())
            {
                wtp_pi_end_configuration();
                (void)set_config(false);
            }
            lk.lock();
        }
    }

    llog.logS(DEBUG, "WSPR loop termination started.");

    // -------------------------------------------------------------------------
    // Shutdown and cleanup
    // -------------------------------------------------------------------------
    llog.logS(DEBUG, "Stopping runtime components.");

    llog.logS(DEBUG, "Stopping web server.");
    webServer.stop();
    llog.logS(DEBUG, "Web server stopped.");

    llog.logS(DEBUG, "Stopping socket server.");
    socketServer.stop();
    llog.logS(DEBUG, "Socket server stopped.");

    llog.logS(DEBUG, "Stopping configuration monitor.");
    ini_reload_pending.store(false, std::memory_order_relaxed);
    stop_runtime_config_monitor(); // Stop config file monitor before transmitter teardown.
    wsprrypi::stop_wtp_fleet();
    wsprrypi::stop_wtp_pi_endpoint();
    llog.logS(DEBUG, "Configuration monitor stopped.");

    llog.logS(DEBUG, "Stopping shutdown monitor.");
    shutdownMonitor.stop(); // Stop the GPIO monitor
    llog.logS(DEBUG, "Shutdown monitor stopped.");

    llog.logS(DEBUG, "Stopping PPM manager.");
    ppmManager.stop(); // Stop PPM manager (if active)
    llog.logS(DEBUG, "PPM manager stopped.");

    llog.logS(DEBUG, "Stopping transmitter.");
    transmitter_shutdown_for_process_exit();
    deassert_transmit_gpio_outputs(
        &config,
        false,
        "scheduler process shutdown");
    llog.logS(DEBUG, "Transmitter stopped.");

    llog.logS(DEBUG, "Stopping band GPIO selector.");
    shutdown_all_configured_selector_gpios(config);
    llog.logS(DEBUG, "Band GPIO selector stopped.");

    llog.logS(DEBUG, "Stopping Amp Control driver.");
    ampControl.stop();
    llog.logS(DEBUG, "Amp Control driver stopped.");

    llog.logS(DEBUG, "Stopping LED driver.");
    ledControl.stop(); // Stop LED driver
    llog.logS(DEBUG, "LED driver stopped.");

    llog.logS(DEBUG, "Runtime components stopped.");

    llog.logS(INFO, get_project_name(), "exiting.");
    // Flush all file system buffers to disk
    sync();

    return !transmission_runtime_failed.load(std::memory_order_acquire);
}

/**
 * @brief Synchronize disk and reboot the machine.
 *
 * This function calls sync() to flush filesystem buffers, then
 * invokes the reboot(2) syscall directly. The process must have
 * the CAP_SYS_BOOT capability (typically run as root).
 */
void reboot_machine()
{
    const MachinePowerResult result = request_machine_power(MachinePowerOperation::Reboot);
    if (result.status == MachinePowerStatus::Unsupported)
    {
        llog.logE(ERROR,
                  "Machine reboot is unavailable on this platform; "
                  "application shutdown completed without rebooting the machine.");
    }
    else if (result.status == MachinePowerStatus::Failed)
    {
        llog.logE(ERROR, "Reboot failed: ", std::strerror(result.error_number));
    }
}

/**
 * @brief Flush filesystems and power off the machine.
 *
 * Calls sync() to ensure all disk buffers are written, then invokes
 * the reboot(2) syscall with the POWER_OFF command. Requires root or
 * the CAP_SYS_BOOT capability.
 */
void shutdown_machine()
{
    const MachinePowerResult result = request_machine_power(MachinePowerOperation::PowerOff);
    if (result.status == MachinePowerStatus::Unsupported)
    {
        llog.logE(ERROR,
                  "Machine power-off is unavailable on this platform; "
                  "application shutdown completed without powering off the machine.");
    }
    else if (result.status == MachinePowerStatus::Failed)
    {
        llog.logE(ERROR, "Shutdown failed: ", std::strerror(result.error_number));
    }
}
