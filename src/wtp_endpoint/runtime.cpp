#include "wtp_endpoint/runtime.hpp"

#include "wtp_endpoint/authority.hpp"
#include "wtp_endpoint/dns_sd_publisher.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_endpoint/interface_selection.hpp"
#include "wtp_endpoint/plain_listener.hpp"
#include "wtp_endpoint/revocation_journal.hpp"
#include "wtp_endpoint/system_clock.hpp"
#include "wtp_endpoint/tone_engine.hpp"
#include "wtp_endpoint/execution_context.hpp"
#include "WTP-Server/include/wtp_server/inhibited_rf_engine.hpp"
#include "wtp_pi_control.hpp"

#include "json.hpp"
#include "logging.hpp"
#include "version.hpp"

#include <cerrno>
#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <pwd.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <tuple>

#include "backend_capabilities.hpp"
#if WSPRRYPI_BACKEND_SIMULATED
#include "WSPR-Transmitter/src/simulated_transmit_backend.hpp"
#endif
#if WSPRRYPI_BACKEND_SI5351
#include "wtp_endpoint/si5351_bridge.hpp"
#include "wtp_endpoint/si5351_realization.hpp"
#endif

namespace wsprrypi {
namespace {
std::string private_state_path() {
    const passwd* owner = getpwuid(geteuid());
    if (!owner || !owner->pw_dir || !*owner->pw_dir)
        throw std::runtime_error("WTP endpoint state home unavailable");
    const std::string directory = std::string(owner->pw_dir) + "/.wsprrypi-wtp";
    if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST)
        throw std::runtime_error("Unable to create WTP endpoint state directory");
    struct stat info{};
    if (lstat(directory.c_str(), &info) != 0 || !S_ISDIR(info.st_mode) ||
        info.st_uid != geteuid() || (info.st_mode & 0077) != 0)
        throw std::runtime_error("WTP endpoint state directory is not private");
    return directory + "/revocation-v1";
}

wsprrypico::wtp::ServiceConfig tone_caps() {
    wsprrypico::wtp::ServiceConfig caps;
    caps.capability_engine = "si5351-finite-tone";
    caps.supported_modes = {"tone"};
    caps.minimum_frequency_nhz = 14'000'000'000'000'000ULL;
    caps.maximum_frequency_nhz = 14'350'000'000'000'000ULL;
    caps.max_events = 2; // One tone plus the controller's optional 1 ns RF-off tail.
    caps.max_job_duration_ns = 10'000'000'001ULL;
    caps.minimum_arm_lead_ns = 2'000'000'000ULL;
    caps.maximum_arm_uncertainty_ns = 500'000'000ULL;
    caps.output_disable_timeout_ns = 5'000'000'000ULL;
    return caps;
}

class Runtime {
public:
    explicit Runtime(const ArgParserConfig& settings)
        : device_id_(wtp_pi_device_id()),
          journal_(private_state_path(), device_id_),
          selected_interface_(settings.wtp_server.interface),
          port_(settings.wtp_server_port_override.value_or(settings.wtp_server.port)),
          settings_(settings) {
        auto caps = tone_caps();
#if WSPRRYPI_BACKEND_SI5351
        if (settings.transmit_backend == TransmitBackendKind::SI5351) {
        auto config = wtp_pi_si5351_backend_config(
            settings.si5351_i2c_bus, settings.si5351_i2c_address,
            static_cast<std::uint32_t>(settings.si5351_reference_hz),
            settings.si5351_reference_source == "crystal",
            settings.si5351_crystal_load_capacitance_pf,
            settings.si5351_tx_output, settings.si5351_power_level);
        config.output_enable_admission = [this] { return engine_->admit_output_enable(); };
        config.first_output_enabled = [this] { engine_->observe_output_enable(); };
        config.anchor_finite_tone_to_enable = true;
        config.confirm_output_disable = true;
        backend_ = std::make_unique<WsprSi5351Backend>(bridge_, config);
        BackendExecutionInputs inputs;
        inputs.power_level = settings.si5351_power_level;
        const auto reference_hz = static_cast<std::uint32_t>(settings.si5351_reference_hz);
        const auto output = settings.si5351_tx_output;
        const auto ppm = settings.si5351_ppm;
        engine_ = std::make_unique<WtpPiToneEngine>(
            *backend_, inputs, BackendKind::SI5351, clock_,
            [reference_hz, output, ppm](std::uint64_t requested) {
                return wtp_si5351_tone_frequency_nhz(
                    requested, reference_hz, ppm, output);
            }, ppm, [this] { bridge_.reset_stop(); }, true);
        }
#endif
#if WSPRRYPI_BACKEND_SIMULATED
        if (settings.transmit_backend == TransmitBackendKind::SIMULATED) {
            SimulatedBackendConfig simulation;
            simulation.virtual_time = false;
            backend_ = std::make_unique<SimulatedTransmitBackend>(simulation_context_, simulation);
            engine_ = std::make_unique<WtpPiToneEngine>(*backend_, BackendExecutionInputs{},
                BackendKind::SIMULATED, clock_,
                [](std::uint64_t value) { return std::optional<std::uint64_t>(value); },
                0.0, [this] { simulation_context_.reset(); }, false,
                [this] { simulation_context_.stop(); });
            caps.capability_engine = "simulated-finite-tone";
        }
#endif
        if (engine_ && !engine_->startup_safe())
            throw std::runtime_error("WTP startup output quiescence failed");
        if (!engine_) listener_error_ = "WTP server supports Si5351 and explicit simulation; selected route is unavailable";
        service_ = std::make_unique<wsprrypico::wtp::JobService>(
            clock_, engine_ ? static_cast<wsprrypico::wtp::RfEngine&>(*engine_) :
                             static_cast<wsprrypico::wtp::RfEngine&>(unavailable_engine_),
            boot_identity_, caps);
        authority_ = std::make_shared<WtpPiAuthority>(*service_, false, &journal_);
        if (authority_->snapshot().revocation_unavailable)
            throw std::runtime_error("WTP takeover revocation journal unavailable");
        if (settings.transmit)
            (void)authority_->enable_noninteractive();
        listener_ = std::make_unique<WtpPlainListener>(
            *authority_, device_id_, get_exe_version());

    }

    ~Runtime() {
        publisher_.stop();
        if (listener_) listener_->stop();
    }

    void activate() {
        std::lock_guard lock(state_mutex_);
        if (activated_ || !listener_ || !engine_ || !settings_.wtp_server.enabled) return;
        const auto now = std::chrono::steady_clock::now();
        if (now < next_activation_) return;
        next_activation_ = now + std::chrono::seconds(2);
        activated_ = true;
        const auto selected = wtp_select_station_address(
            wtp_station_addresses(), selected_interface_);
        if (!selected) {
            activated_ = false;
            listener_error_ = "WTP station LAN interface selection is absent or ambiguous";
            return;
        }
        address_ = *selected;
        if (!listener_->start(address_->address, port_)) {
            listener_error_ = listener_->error();
            activated_ = false;
            return;
        }
        listener_error_.clear();
        std::array<char, 64> hostname{};
        (void)gethostname(hostname.data(), hostname.size() - 1);
        const auto name = "WsprryPi " + std::string(hostname.data()).substr(0, 40) +
                          " " + device_id_.substr(0, 8);
        // A multihomed Pi's ordinary hostname may resolve to an interface on
        // which this listener is deliberately not bound. Publish the selected
        // address under an endpoint-specific SRV target in the same group.
        (void)publisher_.start(address_->interface_name, name, listener_->bound_port(),
                               "wtp-" + device_id_ + ".local", address_->address);
    }

    void poll() {
        if (authority_) authority_->poll();
        std::lock_guard lock(state_mutex_);
        if (listener_ && listener_->running() && address_) {
            const auto current = wtp_select_station_address(
                wtp_station_addresses(), selected_interface_);
            if (!current || current->address != address_->address ||
                current->interface_name != address_->interface_name) {
                publisher_.stop();
                listener_->stop();
                activated_ = false;
                listener_error_ = "WTP station LAN address changed; listener withdrawn";
            }
        }
        if (listener_ && !listener_->running()) publisher_.stop();
    }

    std::string status_json() const {
        std::lock_guard lock(state_mutex_);
        nlohmann::json status{{"schema", "wsprrypi-wtp-endpoint/1"},
                              {"device_id", device_id_},
                              {"enabled", settings_.wtp_server.enabled},
                              {"listener_running", listener_ && listener_->running()},
                              {"dns_sd_published", publisher_.published()},
                              {"listener_error", listener_error_}};
        if (listener_ && listener_->running()) {
            status["address"] = address_->address;
            status["port"] = listener_->bound_port();
        }
        if (authority_) {
            const auto state = authority_->snapshot();
            status["local_requested"] = state.requested_local_enable;
            status["local_effective"] = state.effective_local_enable;
            status["local_work_active"] = state.local_work_active;
            status["remote_admission_closed"] = state.remote_admission_closed;
            status["boot_id"] = state.remote.boot_id;
            status["remote_owner"] = state.remote.owner_id.value_or("");
            status["remote_job_id"] = state.remote.job_id.value_or("");
            status["remote_state"] = wsprrypico::wtp::state_name(state.remote.state);
            status["remote_output_active"] = state.remote.output_active;
            status["output_unknown"] = state.output_unknown;
            status["finishing_remote_job"] = state.finishing_remote_job;
            status["revocation_unavailable"] = state.revocation_unavailable;
            status["revocation_generation"] = std::to_string(state.revocation_generation);
        }
        if (engine_) {
            const auto launch = engine_->launch_observation();
            status["launch_target_monotonic_ns"] = std::to_string(launch.target_monotonic_ns);
            status["launch_observed_monotonic_ns"] = launch.observed_monotonic_ns
                ? nlohmann::json(std::to_string(*launch.observed_monotonic_ns))
                : nlohmann::json(nullptr);
        }
        return status.dump();
    }

    std::shared_ptr<WtpPiAuthority> authority() const { return authority_; }
    void withdraw() {
        std::lock_guard lock(state_mutex_);
        publisher_.stop();
        listener_->stop();
        activated_ = false;
    }
    const ArgParserConfig& settings() const { return settings_; }


private:
    WtpPiSystemClock clock_;
    WtpPiBootIdentity boot_identity_;
    std::string device_id_;
    WtpRevocationJournal journal_;
#if WSPRRYPI_BACKEND_SI5351
    WtpPiSi5351Bridge bridge_{[](WsprTransmissionCallbackEvent, WsprTransmitLogLevel level,
                              const std::string& message, double) {
        const auto severity = (level == WsprTransmitLogLevel::ERROR ||
                               level == WsprTransmitLogLevel::FATAL) ? ERROR :
                              level == WsprTransmitLogLevel::WARN ? WARN :
                              level == WsprTransmitLogLevel::INFO ? INFO : DEBUG;
        llog.logS(severity, "WTP Si5351: ", message);
    }};
#endif
    WtpPiSimulationContext simulation_context_;
    std::unique_ptr<ITransmissionBackend> backend_;
    wsprrypico::wtp::InhibitedRfEngine unavailable_engine_;
    std::unique_ptr<WtpPiToneEngine> engine_;
    std::unique_ptr<wsprrypico::wtp::JobService> service_;
    std::shared_ptr<WtpPiAuthority> authority_;
    std::unique_ptr<WtpPlainListener> listener_;
    WtpPiDnsSdPublisher publisher_;
    std::optional<WtpStationAddress> address_;
    std::string listener_error_;
    std::string selected_interface_;
    std::uint16_t port_;
    ArgParserConfig settings_;
    bool activated_ = false;
    std::chrono::steady_clock::time_point next_activation_{};
    mutable std::mutex state_mutex_;
};

std::mutex runtime_mutex;
std::recursive_mutex lifecycle_mutex;
std::shared_ptr<Runtime> runtime;
std::string runtime_start_error;
std::optional<ArgParserConfig> pending_settings;

auto output_settings(const ArgParserConfig& s) {
    return std::tuple{s.transmit_backend, s.si5351_i2c_bus, s.si5351_i2c_address,
        s.si5351_reference_hz, s.si5351_reference_source, s.si5351_crystal_load_capacitance_pf,
        s.si5351_tx_output, s.si5351_power_level, s.si5351_ppm, s.wtp_server,
        s.wtp_server_port_override};
}
} // namespace

bool start_wtp_pi_endpoint(const ArgParserConfig& settings, std::string* error) {
    std::lock_guard lifecycle(lifecycle_mutex);
    if (!settings.use_ini) return true;
    try {
        auto candidate = std::make_shared<Runtime>(settings);
        set_wtp_pi_control_hooks({
            [candidate] { return candidate->authority()->snapshot().effective_local_enable; },
            [candidate] { return candidate->authority()->begin_scheduled_work(); },
            [candidate] { return candidate->authority()->begin_test_tone(); },
            [candidate](bool off) { candidate->authority()->end_local_work(off); },
            [candidate](bool enabled) {
                if (enabled) (void)candidate->authority()->enable_noninteractive();
                else candidate->authority()->disable_local();
            },
            [candidate](bool off) { candidate->authority()->end_scheduled_work(off); },
            [candidate] { return candidate->authority()->begin_configuration(); },
            [candidate] { candidate->authority()->end_configuration(); }});
        std::lock_guard lock(runtime_mutex);
        runtime = std::move(candidate);
        runtime_start_error.clear();
        return true;
    } catch (const std::exception& failure) {
        std::lock_guard lock(runtime_mutex);
        runtime_start_error = failure.what();
        if (error) *error = runtime_start_error;
        return false;
    }
}

void poll_wtp_pi_endpoint() noexcept {
    try {
        std::lock_guard lifecycle(lifecycle_mutex);
        std::shared_ptr<Runtime> current;
        { std::lock_guard lock(runtime_mutex); current = runtime; }
        if (current) {
            current->poll();
            if (pending_settings) {
                const auto state = current->authority()->snapshot();
                if (!state.local_work_active && !state.remote.owner_id &&
                    !state.remote.output_active && !state.output_unknown &&
                    !state.finishing_remote_job && current->authority()->reserve_reconfiguration()) {
                    std::string error;
                    if (start_wtp_pi_endpoint(*pending_settings, &error)) {
                        pending_settings.reset();
                        activate_wtp_pi_endpoint();
                    }
                }
            } else current->activate();
        }
    } catch (...) {}
}

void activate_wtp_pi_endpoint() {
    std::shared_ptr<Runtime> current;
    { std::lock_guard lock(runtime_mutex); current = runtime; }
    if (current) current->activate();
}

void stop_wtp_pi_endpoint() noexcept {
    std::lock_guard lifecycle(lifecycle_mutex);
    clear_wtp_pi_control_hooks();
    std::shared_ptr<Runtime> current;
    { std::lock_guard lock(runtime_mutex); current.swap(runtime); }
    current.reset();
    pending_settings.reset();
}

std::string wtp_pi_endpoint_status_json() {
    std::shared_ptr<Runtime> current;
    std::string start_error;
    {
        std::lock_guard lock(runtime_mutex);
        current = runtime;
        start_error = runtime_start_error;
    }
    if (current) return current->status_json();
    std::string id;
    try { id = wtp_pi_device_id(); } catch (...) {}
    return nlohmann::json{{"schema", "wsprrypi-wtp-endpoint/1"},
                      {"device_id", id},
                      {"enabled", false}, {"error", start_error}}.dump();
}

void wtp_pi_enable_interactive(bool finish_current, bool confirmed,
                               const std::function<void()>& persist,
                               const std::string& observed_owner, const std::string& observed_job) {
    std::lock_guard lifecycle(lifecycle_mutex);
    std::shared_ptr<Runtime> current;
    { std::lock_guard lock(runtime_mutex); current = runtime; }
    if (!current) { persist(); return; }
    const auto result = current->authority()->enable_interactive(
        finish_current ? WtpPiAuthority::EnableChoice::FinishCurrent :
                         WtpPiAuthority::EnableChoice::EndNow,
        confirmed, persist, std::pair{observed_owner, observed_job});
    if (result == WtpPiAuthority::EnableResult::ConfirmationRequired)
        throw std::runtime_error("local_takeover_confirmation_required");
    // A saved request can remain inhibited until cleanup/recovery succeeds;
    // report its effective state through the endpoint status resource.
}

void wtp_pi_configuration_committed(const ArgParserConfig& settings) {
    std::lock_guard lifecycle(lifecycle_mutex);
    std::shared_ptr<Runtime> current;
    { std::lock_guard lock(runtime_mutex); current = runtime; }
    if (!current) return;
    if (pending_settings || output_settings(settings) != output_settings(current->settings())) {
        pending_settings = settings;
        current->authority()->suspend_remote_admission();
        current->withdraw();
    }
}

bool wtp_pi_stop_remote_work() {
    std::lock_guard lifecycle(lifecycle_mutex);
    std::shared_ptr<Runtime> current;
    { std::lock_guard lock(runtime_mutex); current = runtime; }
    return !current || current->authority()->local_stop();
}

bool wtp_pi_recover_remote_output() {
    std::lock_guard lifecycle(lifecycle_mutex);
    std::shared_ptr<Runtime> current;
    { std::lock_guard lock(runtime_mutex); current = runtime; }
    if (!current) return false;
    current->withdraw();
    const bool recovered = current->authority()->recover_remote_output();
    if (recovered && !pending_settings) current->activate();
    return recovered;
}
} // namespace wsprrypi
