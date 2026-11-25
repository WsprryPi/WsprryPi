#include "WTP-Server/include/wtp_server/observation.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "wtp_retirement.hpp"
#ifdef _WIN32
#include "native_windows_owners.hpp"
#else
#include "native_controller_owners.hpp"
#endif

#include "wtp_endpoint/authority.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_endpoint/revocation_journal.hpp"
#include "wtp_endpoint/system_clock.hpp"
#include "wtp_endpoint/tone_engine.hpp"
#include "wtp_endpoint/execution_context.hpp"
#include "WTP-Server/include/wtp_server/inhibited_rf_engine.hpp"
#include "wtp_pi_control.hpp"
#include "wtp_managed_output.hpp"
#include "recurrence_authority.hpp"
#include "wtp_integration/pi_management_journal.hpp"

#include "json.hpp"
#include "logging.hpp"
#include "version.hpp"

#include <cerrno>
#include <array>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#endif
#include <stdexcept>
#include <sys/stat.h>
#include <tuple>
#include <vector>
#include <filesystem>

#include "backend_capabilities.hpp"
#include "wtp_endpoint/capabilities.hpp"
#if WSPRRYPI_BACKEND_RPI_GPIO || WSPRRYPI_BACKEND_RP1_GPCLK || WSPRRYPI_BACKEND_SI5351
#include "wtp_endpoint/native_bridge.hpp"
#endif
#include "scheduling.hpp"
#include "rp1_route_bridge.hpp"
#if WSPRRYPI_BACKEND_SIMULATED
#include "WSPR-Transmitter/src/simulated_transmit_backend.hpp"
#endif
#if WSPRRYPI_BACKEND_SI5351
#include "wtp_endpoint/si5351_bridge.hpp"
#endif
#if WSPRRYPI_BACKEND_RPI_GPIO
#include "WSPR-Transmitter/src/wspr_transmit_backend_rpi.hpp"
#endif
#if WSPRRYPI_BACKEND_RP1_GPCLK
#include "WSPR-Transmitter/src/rp1_gpclk_transmit_backend.hpp"
#endif

namespace wsprrypi {
#if !WSPRRYPI_CONTROLLER_ONLY
static WtpExternalReconcileResult reconcile_native_external_output(const WtpManagedOutput &);
#endif
namespace {
std::string fixture_private_state_path;
std::string fixture_controller_owner_home;
#ifdef _WIN32
std::string windows_path_text(const std::filesystem::path &path){const auto value=path.u8string();return {value.begin(),value.end()};}
void verify_windows_fixture_home(const std::filesystem::path &home) {
    const auto owner=windows_file::current_sid();
    windows_file::ParentChain parents(home/L".fixture-owner-check",owner);
    windows_file::Handle directory(::CreateFileW(windows_file::api_path(home).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
    windows_file::require(directory.get()!=INVALID_HANDLE_VALUE);(void)windows_file::inspect(directory.get(),true);
    windows_file::verify_exact_owner(directory.get(),owner);windows_file::verify_security(directory.get(),owner,true,true);
}
#endif
#if !WSPRRYPI_CONTROLLER_ONLY
std::string private_state_path() {
    if(!fixture_private_state_path.empty())return fixture_private_state_path;
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

class Runtime {
public:
    explicit Runtime(const ArgParserConfig& settings)
        : device_id_(wtp_pi_device_id()),
          journal_(private_state_path(), device_id_),
          settings_(settings) {
        wsprrypico::wtp::ServiceConfig caps;
#if WSPRRYPI_BACKEND_RPI_GPIO || WSPRRYPI_BACKEND_RP1_GPCLK || WSPRRYPI_BACKEND_SI5351
        BackendExecutionInputs inputs;
        HardwareProfile profile = HardwareProfile::UNSPECIFIED;
        double ppm = settings.gpio_manual_ppm;
#if WSPRRYPI_BACKEND_RPI_GPIO
        if (settings.transmit_backend == TransmitBackendKind::GPIO) {
            inputs.tx_gpio = inputs.configured_tx_gpio = settings.gpio_tx_pin;
            inputs.power_level = settings.gpio_power_level;
            switch (get_raspberry_pi_generation()) {
                case 1: profile = HardwareProfile::BCM2835; break;
                case 2: case 3: profile = HardwareProfile::BCM2836_BCM2837; break;
                case 4: profile = HardwareProfile::BCM2711; break;
                default: throw std::runtime_error("GPIO WTP processor profile unavailable");
            }
            const auto processor = profile == HardwareProfile::BCM2711
                ? LegacyGpioProcessorProfile::Bcm2711 : profile == HardwareProfile::BCM2835
                ? LegacyGpioProcessorProfile::Bcm2835 : LegacyGpioProcessorProfile::Bcm2836Bcm2837;
            backend_ = std::make_unique<WsprRpiBackend>(bridge_, nullptr, settings.gpio_tx_pin, processor);
        }
#endif
#if WSPRRYPI_BACKEND_RP1_GPCLK
        if (settings.transmit_backend == TransmitBackendKind::RP1_GPCLK) {
            backend_ = std::make_unique<WsprRp1GpclkBackend>(bridge_);
            inputs.tx_gpio = inputs.configured_tx_gpio = settings.gpio_tx_pin;
            inputs.power_level = settings.rp1_gpio_drive_ma;
            profile = HardwareProfile::RP1_GPCLK;
        }
#endif
#if WSPRRYPI_BACKEND_SI5351
        if (settings.transmit_backend == TransmitBackendKind::SI5351) {
            auto config = wtp_pi_si5351_backend_config(
                settings.si5351_i2c_bus, settings.si5351_i2c_address,
                static_cast<std::uint32_t>(settings.si5351_reference_hz),
                settings.si5351_reference_source == "crystal",
                settings.si5351_crystal_load_capacitance_pf,
                settings.si5351_tx_output, settings.si5351_power_level);
            config.device.adapter_identity = settings.si5351_adapter_identity;
            backend_ = std::make_unique<WsprSi5351Backend>(bridge_, config);
            inputs.power_level = settings.si5351_power_level;
            ppm = settings.si5351_ppm;
            profile = HardwareProfile::SI5351;
        }
#endif
        if (backend_) {
            ScheduledExecutionHooks hooks{
                [this] { return engine_->admit_output_enable(); },
                [this] { engine_->observe_output_enable(); },
                [this] {
                    if (!engine_->admit_output_enable()) return false;
                    engine_->observe_output_enable();
                    return true;
                }, true};
            if (!backend_->setScheduledExecutionHooks(std::move(hooks)))
                throw std::runtime_error("Native backend has no scheduled WTP launch contract");
            engine_ = std::make_unique<WtpPiToneEngine>(
                *backend_, inputs, backend_->info().kind, clock_,
                [this](const wsprrypico::wtp::Job&)
                    -> std::optional<std::vector<std::uint64_t>> {
                    std::vector<std::uint64_t> realized;
                    for (double hz : backend_->realizedEventFrequencies()) {
                        if (hz == 0) { realized.push_back(0); continue; }
                        const auto value = wtp_frequency_nhz(hz);
                        if (!value) return std::nullopt;
                        realized.push_back(*value);
                    }
                    return realized;
                }, ppm, [this] { bridge_.reset_stop(); }, true,
                [this] { bridge_.request_stop(); },
                settings.allow_unqualified_frequency, settings.allow_non_amateur_frequency,
                profile, [this](const wsprrypico::wtp::Job& job, ExecutionPlan& plan,
                                BackendExecutionInputs& job_inputs) {
                    if (settings_.transmit_backend == TransmitBackendKind::GPIO ||
                        settings_.transmit_backend == TransmitBackendKind::RP1_GPCLK) {
                        const auto correction = gpio_frequency_correction_for_request(settings_);
                        if (!correction.valid) return false;
                        plan.calibration.ppm = correction.additional_ppm;
                    }
                    if (settings_.transmit_backend == TransmitBackendKind::RP1_GPCLK) {
                        ::TransmissionRequest request;
                        std::string error;
                        if (!apply_direct_rp1_development_confirmation_bridge(settings_, request, &error))
                            return false;
                        if (request.rp1_development.operation_id != job.job_id) return false;
                        job_inputs.rp1_development = request.rp1_development;
                    }
                    return true;
                });
        }
#endif
#if WSPRRYPI_BACKEND_SIMULATED
        if (settings.transmit_backend == TransmitBackendKind::SIMULATED) {
            SimulatedBackendConfig simulation;
            simulation.virtual_time = false;
            backend_ = std::make_unique<SimulatedTransmitBackend>(simulation_context_, simulation);
            engine_ = std::make_unique<WtpPiToneEngine>(*backend_, BackendExecutionInputs{},
                BackendKind::SIMULATED, clock_,
                [](const wsprrypico::wtp::Job& job)
                    -> std::optional<std::vector<std::uint64_t>> {
                    std::vector<std::uint64_t> realized;
                    for (const auto& event : job.events)
                        realized.push_back(event.frequency_nhz.value_or(0));
                    return realized;
                },
                0.0, [this] { simulation_context_.reset(); }, false,
                [this] { simulation_context_.stop(); });
        }
#endif
        if (backend_) caps = wtp_backend_caps(*backend_);
        if (engine_ && !engine_->startup_safe())
            throw std::runtime_error("WTP startup output quiescence failed");
        if (!engine_) listener_error_ = "WTP server cannot use the selected transmit backend";
        service_ = std::make_unique<wsprrypico::wtp::JobService>(
            clock_, engine_ ? static_cast<wsprrypico::wtp::RfEngine&>(*engine_) :
                             static_cast<wsprrypico::wtp::RfEngine&>(unavailable_engine_),
            boot_identity_, caps);
        authority_ = std::make_shared<WtpPiAuthority>(*service_, false, &journal_,wtp_pi_management_ready);
        if (authority_->snapshot().revocation_unavailable)
            throw std::runtime_error("WTP takeover revocation journal unavailable");
        if (settings.transmit)
            (void)authority_->enable_noninteractive();
    }

    // The authority, native engine, clock, and protected journals remain owned
    // here. Retirement removes network publication and admission entirely.
    void activate() {}
    void poll() {
        if (authority_) authority_->poll();
    }

    std::string status_json() const {
        std::lock_guard lock(state_mutex_);
        nlohmann::json status{{"schema", "wsprrypi-wtp-endpoint/1"},
                              {"device_id", device_id_},
                              {"enabled", false},
                              {"listener_running", false},
                              {"dns_sd_published", false},
                              {"listener_error", listener_error_}};
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
    const std::string &device_id() const { return device_id_; }
    void withdraw() {}
    const ArgParserConfig& settings() const { return settings_; }


private:
    WtpPiSystemClock clock_;
    WtpPiBootIdentity boot_identity_;
    std::string device_id_;
    WtpRevocationJournal journal_;
#if WSPRRYPI_BACKEND_RPI_GPIO || WSPRRYPI_BACKEND_RP1_GPCLK || WSPRRYPI_BACKEND_SI5351
    WtpPiNativeBridge bridge_{[](WsprTransmissionCallbackEvent, WsprTransmitLogLevel level,
                              const std::string& message, double) {
        const auto severity = (level == WsprTransmitLogLevel::ERROR ||
                               level == WsprTransmitLogLevel::FATAL) ? ERROR :
                              level == WsprTransmitLogLevel::WARN ? WARN :
                              level == WsprTransmitLogLevel::INFO ? INFO : DEBUG;
        llog.logS(severity, "WTP native: ", message);
    }};
#endif
    WtpPiSimulationContext simulation_context_;
    std::unique_ptr<ITransmissionBackend> backend_;
    wsprrypico::wtp::InhibitedRfEngine unavailable_engine_;
    std::unique_ptr<WtpPiToneEngine> engine_;
    std::unique_ptr<wsprrypico::wtp::JobService> service_;
    std::shared_ptr<WtpPiAuthority> authority_;
    std::string listener_error_;
    ArgParserConfig settings_;
    mutable std::mutex state_mutex_;
};

#endif // Local execution runtime
std::recursive_mutex lifecycle_mutex;
#if !WSPRRYPI_CONTROLLER_ONLY
std::mutex runtime_mutex;
std::shared_ptr<Runtime> runtime;
std::string runtime_start_error;
std::optional<ArgParserConfig> pending_settings;

auto output_settings(const ArgParserConfig& s) {
    return std::tuple{s.transmit_backend, s.gpio_tx_pin, s.gpio_power_level, s.rp1_gpio_drive_ma,
        s.gpio_use_system_clock_frequency_estimate, s.gpio_use_direct_pps_calibration, s.gpio_frequency_residual_ppm,
        s.gpio_manual_ppm, s.rp1_development_confirmation_json, s.si5351_i2c_bus, s.si5351_adapter_identity, s.si5351_i2c_address,
        s.si5351_reference_hz, s.si5351_reference_source, s.si5351_crystal_load_capacitance_pf,
        s.si5351_tx_output, s.si5351_power_level, s.si5351_ppm, s.wtp_server,
        s.wtp_server_port_override, s.allow_unqualified_frequency,
        s.allow_non_amateur_frequency};
}
#endif
} // namespace

bool wtp_pi_has_prior_output_history() noexcept {
    try{
        std::lock_guard lifecycle(lifecycle_mutex);
        std::string path=fixture_private_state_path;
#ifdef _WIN32
        auto owner_sid=windows_file::current_sid();
        if(path.empty()) {
            const auto owner=native_windows_controller_owners().backend;owner_sid=owner.sid;
            path=windows_path_text(windows_file::path_from_utf8(owner.home)/L".wsprrypi-wtp");
        }
        const auto selected=windows_file::path_from_utf8(path);
        windows_file::ParentChain parents(selected,owner_sid);
        if(::GetFileAttributesW(windows_file::api_path(selected).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
        const auto error=::GetLastError();return error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND;
#else
        if(path.empty()){
            const auto owner=getpwuid(geteuid());if(!owner||!owner->pw_dir||!*owner->pw_dir)return true;
            path=std::string(owner->pw_dir)+"/.wsprrypi-wtp";
        }
        struct stat info{};if(!::lstat(path.c_str(),&info))return true;
        return errno!=ENOENT;
#endif
    }catch(...){return true;}
}
void testing::set_wtp_pi_private_state_path(const std::string &path){
    std::lock_guard lifecycle(lifecycle_mutex);
#if !WSPRRYPI_CONTROLLER_ONLY
    std::lock_guard lock(runtime_mutex);
    if(runtime||pending_settings)throw std::logic_error("Runtime already selected");
#endif
#ifdef _WIN32
    const auto selected=windows_file::path_from_utf8(path);
    verify_windows_fixture_home(selected.parent_path());
    fixture_private_state_path=windows_path_text(selected);
#else
    if(path.empty()||path.front()!='/'||path.back()=='/'||path.find('\0')!=std::string::npos)
        throw std::invalid_argument("Invalid isolated endpoint ledger path or runtime lifecycle");
    const auto parent=path.substr(0,path.find_last_of('/'));
    struct stat info{};
    if(lstat(parent.c_str(),&info)!=0||!S_ISDIR(info.st_mode)||info.st_uid!=geteuid()||(info.st_mode&0077)!=0)
        throw std::invalid_argument("Isolated endpoint ledger requires a private fixture parent");
    fixture_private_state_path=path;
#endif
}

void testing::set_controller_installation_owner_home(const std::string &home){
    std::lock_guard lock(lifecycle_mutex);
#ifdef _WIN32
    if(fixture_private_state_path.empty()||!fixture_controller_owner_home.empty())
        throw std::invalid_argument("Invalid isolated controller owner home");
    const auto selected=windows_file::path_from_utf8(home);verify_windows_fixture_home(selected);
    fixture_controller_owner_home=windows_path_text(selected);
#else
    struct stat info{};
    if(fixture_private_state_path.empty()||!fixture_controller_owner_home.empty()||home.empty()||
        home.find('\0')!=std::string::npos||std::filesystem::canonical(home).string()!=home||
        ::lstat(home.c_str(),&info)||!S_ISDIR(info.st_mode)||info.st_uid!=::geteuid()||(info.st_mode&0777)!=0700)
        throw std::invalid_argument("Invalid isolated controller owner home");
    fixture_controller_owner_home=home;
#endif
}
ControllerInstallationOwner controller_installation_owner(){
    std::lock_guard lifecycle(lifecycle_mutex);
#ifdef _WIN32
    if(!fixture_controller_owner_home.empty())return {fixture_controller_owner_home,windows_file::current_sid()};
    if(!fixture_private_state_path.empty())return {
        windows_path_text(windows_file::path_from_utf8(fixture_private_state_path).parent_path()),windows_file::current_sid()};
    const auto owner=native_windows_controller_owners().backend;
    return {owner.home,owner.sid};
#else
    if(!fixture_controller_owner_home.empty())return {fixture_controller_owner_home,::geteuid()};
    if(!fixture_private_state_path.empty())return {
        std::filesystem::canonical(std::filesystem::path(fixture_private_state_path).parent_path()).string(),::geteuid()};
#if WSPRRYPI_CONTROLLER_ONLY
    if(const auto owners=native_controller_owners())return {owners->backend.home,owners->backend.uid};
#endif
    const auto owner=native_controller_account(nullptr);
    if(!owner)throw std::runtime_error("private_device_owner_unavailable");
    return {owner->home,owner->uid};
#endif
}
std::string private_device_state_directory(PrivateDeviceDomain domain,const std::string &device){
    if(device.size()!=32||device.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::runtime_error("private_device_identity_unavailable");
    std::lock_guard lifecycle(lifecycle_mutex);
    std::string label;
    switch(domain){
        case PrivateDeviceDomain::Recurrence:label="recurrence";break;
        case PrivateDeviceDomain::ControllerRetirements:label="controller-retirements";break;
        case PrivateDeviceDomain::Pairing:label="pairing";break;
        default:throw std::runtime_error("private_device_domain_unavailable");
    }
    if(!fixture_private_state_path.empty())return fixture_private_state_path+"."+label+"-"+device;
#ifdef _WIN32
    const auto owner=native_windows_controller_owners().backend;
    return windows_path_text(windows_file::path_from_utf8(owner.home)/std::filesystem::path(L".wsprrypi-"+
        std::wstring(label.begin(),label.end())+L"-"+std::wstring(device.begin(),device.end())));
#else
    const auto owner=getpwuid(geteuid());
    if(!owner||!owner->pw_dir||!*owner->pw_dir)throw std::runtime_error("private_device_owner_unavailable");
    return std::string(owner->pw_dir)+"/.wsprrypi-"+label+"-"+device;
#endif
}
PiRecurrenceStorage prepare_pi_recurrence_storage(const std::string &device,const std::string &ini,
    const std::function<void(OwnedStatePublicationPoint)> &checkpoint){
    const bool prior=wtp_pi_has_prior_output_history();
    const auto parent=std::filesystem::canonical(std::filesystem::path(ini).has_parent_path()?std::filesystem::path(ini).parent_path():".");
    const auto binding=PiManagementJournal::digest((parent/std::filesystem::path(ini).filename()).string());
    const auto storage=prepare_owned_state_directory(private_device_state_directory(PrivateDeviceDomain::Recurrence,device),
        [&]{return recurrence_bootstrap_record(device,binding,WtpPiBootIdentity{}.new_boot_id()).dump();},
        [&](const auto &bytes){if(parse_recurrence_storage_record(bytes,device,binding)["schema"]!="pi-recurrence-bootstrap/1")
            throw std::runtime_error("recurrence_storage_hold");},checkpoint);
    return {storage.file,storage.existed,prior};
}

#if !WSPRRYPI_CONTROLLER_ONLY
bool start_wtp_pi_endpoint(const ArgParserConfig& settings, std::string* error) {
    std::lock_guard lifecycle(lifecycle_mutex);
    if(const auto refusal=wtp_retirement_configuration_refusal(settings);!refusal.empty()){if(error)*error=refusal;return false;}
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
            [candidate] { candidate->authority()->end_configuration(); },
            [candidate] {
                const auto state=candidate->authority()->snapshot();
                return !state.output_unknown&&!state.revocation_unavailable;
            },
            [](const WtpManagedOutput &expected){return reconcile_native_external_output(expected);}});
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
    std::lock_guard lifecycle(lifecycle_mutex);
    std::shared_ptr<Runtime> current;
    { std::lock_guard lock(runtime_mutex); current = runtime; }
    if (current) current->activate();
}

void stop_wtp_pi_endpoint() noexcept {
    std::lock_guard lifecycle(lifecycle_mutex);
    wtp_pi_management_stopping();
    clear_wtp_pi_control_hooks();
    std::shared_ptr<Runtime> current;
    { std::lock_guard lock(runtime_mutex); current.swap(runtime); }
    // Read/management reservations can retain this Runtime. Withdraw its
    // actual listener now rather than waiting for the final read reference.
    if(current)current->withdraw();
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

void wtp_pi_with_configuration(const std::function<void()>& action) {
    if (!action) throw std::runtime_error("configuration_action_required");
    std::shared_ptr<Runtime> current;
    {
        // Configuration callers already own their configuration lock. Never
        // wait here behind a poll/reconfiguration path with the opposite lock
        // order, and never release a reservation through a replacement runtime.
        std::unique_lock lifecycle(lifecycle_mutex, std::try_to_lock);
        if (!lifecycle.owns_lock()) throw std::runtime_error("management_busy");
        { std::lock_guard lock(runtime_mutex); current = runtime; }
        if (!current || pending_settings || !current->authority()->begin_configuration())
            throw std::runtime_error("native_configuration_unavailable");
    }
    struct Release {
        std::shared_ptr<Runtime> current;
        ~Release() { current->authority()->end_configuration(); }
    } release{current};
    action();
}

#else
bool start_wtp_pi_endpoint(const ArgParserConfig &, std::string *error) {
    if(error)*error="Local WTP execution is unavailable in the controller build";
    return false;
}
void activate_wtp_pi_endpoint() { throw std::logic_error("Local WTP execution is unavailable"); }
void poll_wtp_pi_endpoint() noexcept {}
void stop_wtp_pi_endpoint() noexcept {}
std::string wtp_pi_endpoint_status_json() { throw std::logic_error("Local WTP execution is unavailable"); }
void wtp_pi_enable_interactive(bool,bool,const std::function<void()> &,const std::string &,const std::string &) {
    throw std::logic_error("Local WTP execution is unavailable");
}
void wtp_pi_configuration_committed(const ArgParserConfig &) { throw std::logic_error("Local WTP execution is unavailable"); }
void wtp_pi_with_configuration(const std::function<void()> &) { throw std::logic_error("Local WTP execution is unavailable"); }
#endif

#if !WSPRRYPI_CONTROLLER_ONLY
std::optional<PiManagementObservation> wtp_pi_management_observation(){
    std::unique_lock lifecycle(lifecycle_mutex,std::try_to_lock);
    if(!lifecycle.owns_lock())return {};
    std::shared_ptr<Runtime> current;
    {std::lock_guard lock(runtime_mutex);current=runtime;}
    if(!current||pending_settings)return {};
    const auto observed=current->authority()->management_snapshot(current->device_id());
    auto result=PiManagementObservation{current->device_id(),observed.first,observed.second,
        [current]{
            std::unique_lock lifecycle(lifecycle_mutex,std::try_to_lock);
            if(!lifecycle.owns_lock())return false;
            {std::lock_guard lock(runtime_mutex);if(runtime!=current||pending_settings)return false;}
            return current->authority()->begin_configuration();
        },[current]{current->authority()->end_configuration();},
        [current,boot=observed.first.remote.boot_id](const WtpPiAuthority::ObservedOutput &expected){
            std::unique_lock lifecycle(lifecycle_mutex,std::try_to_lock);
            if(!lifecycle.owns_lock())return false;
            {std::lock_guard lock(runtime_mutex);if(runtime!=current||pending_settings)return false;}
            return current->authority()->reserve_observed_stop(current->device_id(),boot,expected);
        },[current]{current->authority()->release_observed_stop();},
        [current]{
            std::lock_guard lifecycle(lifecycle_mutex);
            {std::lock_guard lock(runtime_mutex);if(runtime!=current||pending_settings)
                throw std::runtime_error("native_stop_owner_changed");}
            // Durable acceptance already saved Transmit=false. The existing
            // cleanup path does not save again or change scheduling.enabled.
            (void)stop_transmission_by_user_request(false);
        }};
    if(current->settings().transmit_backend!=TransmitBackendKind::WTP){
        result.destination_id=current->device_id();return result;
    }
    const auto outgoing=wtp_runtime_managed_output();if(!outgoing)return {};
    result.destination_id=outgoing->destination_id;result.output=outgoing->output;
    result.authority.output_unknown=result.authority.output_unknown||outgoing->unknown;
    result.authority.stop_reserved=result.authority.stop_reserved||outgoing->stop_reserved;
    result.authority.effective_local_enable=result.authority.requested_local_enable&&outgoing->ready&&!outgoing->unknown;
    const auto native_reserve=result.reserve_configuration;
    const auto native_release=result.release_configuration;
    if(outgoing->reserve_configuration&&outgoing->release_configuration){
        result.reserve_configuration=[native_reserve,native_release,outgoing]{
            if(!native_reserve())return false;
            if(outgoing->reserve_configuration())return true;
            native_release();return false;
        };
        result.release_configuration=[native_release,outgoing]{native_release();outgoing->release_configuration();};
    }else {result.reserve_configuration={};result.release_configuration={};}
    const auto native_stop=result.reserve_stop;
    const auto release_stop=result.release_stop;
    if(outgoing->reserve_stop&&outgoing->release_stop){
        result.reserve_stop=[native_stop,release_stop,outgoing,native_output=observed.second](const auto &expected){
            if(!native_stop(native_output))return false;
            if(outgoing->reserve_stop(expected))return true;
            release_stop();return false;
        };
        result.release_stop=[release_stop,outgoing]{release_stop();outgoing->release_stop();};
    }else {result.reserve_stop={};result.release_stop={};result.cancel_reserved_stop={};}
    return result;
}

bool wtp_pi_stop_remote_work() {
    std::lock_guard lifecycle(lifecycle_mutex);
    std::shared_ptr<Runtime> current;
    { std::lock_guard lock(runtime_mutex); current = runtime; }
    return !current || current->authority()->local_stop();
}

static WtpExternalReconcileResult reconcile_native_external_output(const WtpManagedOutput &expected){
    std::shared_ptr<Runtime> current;
    {std::lock_guard lock(runtime_mutex);current=runtime;}
    if(!current)return WtpExternalReconcileResult::NotApplicable;
    std::unique_lock lifecycle(lifecycle_mutex,std::try_to_lock);
    if(!lifecycle.owns_lock())return WtpExternalReconcileResult::Held;
    {std::lock_guard lock(runtime_mutex);if(current!=runtime)return WtpExternalReconcileResult::Held;}
    if(current->settings().transmit_backend!=TransmitBackendKind::WTP)return WtpExternalReconcileResult::NotApplicable;
    if(pending_settings)return WtpExternalReconcileResult::Held;
    const auto observed=wtp_runtime_managed_output();
    if(!observed||observed->output!=expected.output||observed->destination_id!=expected.destination_id||
        !observed->destination_id||!observed->ready||observed->unknown||observed->output.owner_id||observed->output.job_id)return WtpExternalReconcileResult::Held;
    return current->authority()->recover_output(true)?WtpExternalReconcileResult::Confirmed:WtpExternalReconcileResult::Held;
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
#else
std::optional<PiManagementObservation> wtp_pi_management_observation(){ return {}; }
bool wtp_pi_stop_remote_work(){ return true; }
bool wtp_pi_recover_remote_output(){ return false; }
#endif
} // namespace wsprrypi
