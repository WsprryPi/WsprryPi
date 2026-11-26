// SPDX-License-Identifier: MIT
#include "gpio_qualification_owner.hpp"
#include "WSPR-Transmitter/src/bcm2835_qualification_lease.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <poll.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

namespace gpio_qualification
{
namespace
{
namespace fs = std::filesystem;
constexpr int ms_cleanup_margin = 1500;
constexpr int ms_reserve = 8000;
constexpr int ms_service_poll = 200;
// The same systemctl query measured at most 480 ms on the ARMv6 bench.
// Round up to 500 ms and reserve one additional 500 ms interval.
constexpr int ms_runtime_service = 1000;
// wspr1's measured service queries took 337–414 ms. Round to 500 ms,
// then allow that much again for variability in each read-only observation.
constexpr int ms_review_service = 1000;
constexpr int ms_review_owner = 2 * ms_review_service + 1000;
constexpr int ms_group_prepare = 12'000;
constexpr unsigned max_station_file = 64 * 1024 * 1024;
const Json safe_service{{"service_active", "inactive"}, {"service_enabled", "disabled"}, {"service_pid", 0}};
const Json capabilities = {"read-only-native-admission", "exclusive-run-and-bank-lease",
    "isolated-drive-instances", "cutoff-before-output-enable", "independent-identified-stop",
    "durable-local-recovery", "identified-reservation"};

class Fd
{
public:
    explicit Fd(int fd = -1) : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) close(fd_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return fd_; }
private:
    int fd_;
};

bool present(const fs::path& path)
{
    struct stat state{};
    if (lstat(path.c_str(), &state) == 0) return true;
    require(errno == ENOENT, "Could not check private state.");
    return false;
}

std::string file(const fs::path& path, unsigned limit = max_message, bool trusted = false)
{
    Fd fd(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    struct stat before{}, after{};
    require(fd.get() >= 0 && fstat(fd.get(), &before) == 0 && S_ISREG(before.st_mode) &&
        (!trusted || (before.st_uid == geteuid() && before.st_nlink == 1 && !(before.st_mode & 022))),
        "Could not open trusted bounded input.");
    std::string result;
    std::array<char, 8192> buffer;
    ssize_t count;
    while ((count = read(fd.get(), buffer.data(), buffer.size())) > 0)
    {
        require(result.size() + count <= limit, "Trusted input exceeds its bound.");
        result.append(buffer.data(), count);
    }
    require(count == 0 && fstat(fd.get(), &after) == 0 &&
        before.st_size == after.st_size && before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
        before.st_mtim.tv_nsec == after.st_mtim.tv_nsec,
        "Input changed or could not be read.");
    return result;
}

void syncDir(const fs::path& path)
{
    Fd dir(open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    require(dir.get() >= 0 && fsync(dir.get()) == 0, "Could not synchronize owner directory.");
}

void publish(const fs::path& path, const Json& data)
{
    const auto temporary = path.parent_path() / ("." + nonce());
    Fd fd(open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    const auto bytes = data.dump();
    require(bytes.size() <= max_message, "Owner journal exceeds its bound.");
    try
    {
        require(fd.get() >= 0 && write(fd.get(), bytes.data(), bytes.size()) == ssize_t(bytes.size()) &&
            fsync(fd.get()) == 0 && rename(temporary.c_str(), path.c_str()) == 0,
            "Could not publish durable owner state.");
        syncDir(path.parent_path());
    }
    catch (...)
    {
        unlink(temporary.c_str());
        throw;
    }
}

class ServiceProbe
{
public:
    explicit ServiceProbe(int budget_ms) : deadline_(nowMs() + budget_ms)
    {
        int pipefd[2];
        require(pipe2(pipefd, O_NONBLOCK | O_CLOEXEC) == 0, "Service observation unavailable.");
        Fd input(pipefd[0]), output(pipefd[1]);
        posix_spawn_file_actions_t actions;
        require(posix_spawn_file_actions_init(&actions) == 0, "Service observation unavailable.");
        const int stdout_status = posix_spawn_file_actions_adddup2(&actions, output.get(), STDOUT_FILENO);
        const int stderr_status = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        const char* arguments[] = {"/usr/bin/systemctl", "show", "--no-pager", "-p", "ActiveState",
            "-p", "UnitFileState", "-p", "MainPID", "wsprrypi.service", nullptr};
        char path[] = "PATH=/usr/bin:/bin";
        char locale[] = "LC_ALL=C";
        char* environment[] = {path, locale, nullptr};
        int status = EINVAL;
        if (!stdout_status && !stderr_status)
            status = posix_spawn(&child_, arguments[0], &actions, nullptr,
                const_cast<char* const*>(arguments), environment);
        posix_spawn_file_actions_destroy(&actions);
        require(status == 0, "Could not observe normal WsprryPi unit.");
        const int copied = fcntl(input.get(), F_DUPFD_CLOEXEC, 3);
        if (copied < 0)
        {
            cancel();
            throw std::runtime_error("Service observation unavailable.");
        }
        input_ = copied;
    }

    ~ServiceProbe()
    {
        cancel();
        if (input_ >= 0) close(input_);
    }
    ServiceProbe(const ServiceProbe&) = delete;
    ServiceProbe& operator=(const ServiceProbe&) = delete;

    std::optional<Json> poll()
    {
        int exit_status{};
        const auto done = waitpid(child_, &exit_status, WNOHANG);
        if (done == child_ || (done < 0 && errno == ECHILD)) child_ = -1;
        require(done >= 0 && nowMs() < deadline_, "Normal unit observation was not completed.");
        std::array<char, 512> buffer;
        for (;;)
        {
            const auto count = read(input_, buffer.data(), buffer.size());
            if (count <= 0) break;
            reply_.append(buffer.data(), count);
            require(reply_.size() <= 2048, "Normal unit observation was not completed.");
        }
        if (done == 0) return std::nullopt;
        require(WIFEXITED(exit_status) && WEXITSTATUS(exit_status) == 0, "Normal unit was not found.");
        const std::set<std::string> expected{"ActiveState=inactive", "UnitFileState=disabled", "MainPID=0"};
        std::istringstream stream(reply_);
        std::set<std::string> observed;
        std::string line;
        while (std::getline(stream, line)) observed.insert(line);
        require(observed == expected, "Normal WsprryPi unit must be inactive, disabled, and PID zero.");
        return safe_service;
    }

private:
    void cancel()
    {
        if (child_ <= 0) return;
        kill(child_, SIGKILL);
        while (waitpid(child_, nullptr, 0) < 0 && errno == EINTR) {}
        child_ = -1;
    }

    std::int64_t deadline_{};
    pid_t child_{-1};
    int input_{-1};
    std::string reply_;
};

#ifdef GPIO_QUALIFICATION_TEST
struct SimulatedRegisters
{
    unsigned bank{27};
    int pin{};
};
class SimulatedHardware final : public QualificationHardware
{
public:
    explicit SimulatedHardware(fs::path fault) : fault_(std::move(fault))
    {
        auto path = fault_.parent_path() / "registers";
        Fd registers(open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600));
        require(registers.get() >= 0 && ftruncate(registers.get(), sizeof(SimulatedRegisters)) == 0,
            "Could not initialize simulated registers.");
        auto* memory = mmap(nullptr, sizeof(SimulatedRegisters), PROT_READ | PROT_WRITE,
            MAP_SHARED, registers.get(), 0);
        require(memory != MAP_FAILED, "Could not share simulated registers.");
        state_ = new (memory) SimulatedRegisters{};
    }
    QualificationObservation observe() override
    {
        if (fault() == "external-pad" && state_->pin) state_->bank = 4;
        return {{state_->bank}, state_->pin == 4 ? 4u : 0u, state_->pin == 20 ? 2u : 0u,
            state_->pin != 0, state_->pin != 0, state_->pin == 0, state_->pin == 0};
    }
    ~SimulatedHardware() override { munmap(state_, sizeof(SimulatedRegisters)); }
    unsigned prepare(QualificationBank original) override
    {
        if (fault() == "pending") usleep(80'000);
        require(fault() != "prepare" && observe().idle() && observe().bank == original,
            "Simulated preparation failure.");
        return 0x40001000;
    }
    void enable(const Bcm2835QualificationPlan& plan, QualificationBank original,
        const std::function<bool()>& armed) override
    {
        if (fault() == "pre-enable") usleep(300'000);
        require(armed() && observe().idle() && observe().bank == original,
            "Simulated pre-enable refusal.");
        state_->bank = original.withDrive(plan.tone.power_level).bits;
        state_->pin = plan.tone.tx_gpio;
        if (fault() == "process-loss") _exit(91);
    }
    bool active(const Bcm2835QualificationPlan& plan, QualificationBank original) override
    {
        if (fault() == "blocked-status") usleep(400'000);
        return state_->pin == plan.tone.tx_gpio && observe().bank == original.withDrive(plan.tone.power_level);
    }
    bool stop(const Bcm2835QualificationPlan& plan, QualificationBank original, unsigned) override
    {
        (void)observe();
        if (state_->pin != 0 && state_->pin != plan.tone.tx_gpio) return false;
        state_->pin = 0;
        if (state_->bank != original.withDrive(plan.tone.power_level).bits && state_->bank != original.bits)
            return false;
        if (fault() == "restore") return false;
        state_->bank = original.bits;
        return true;
    }
    bool finish() override { return fault() != "finish"; }

private:
    std::string fault() const { return present(fault_) ? file(fault_, 64) : ""; }
    fs::path fault_;
    SimulatedRegisters* state_;
};
#endif

struct Environment
{
    fs::path root;
    bool simulated{};
    std::string executable;

    bool realService() const
    {
#ifdef GPIO_QUALIFICATION_TEST
        return !simulated || present(root / "observe-systemd");
#else
        return true;
#endif
    }

    Json service(int budget_ms = ms_runtime_service) const
    {
        if (!realService())
        {
            return fault() == "service" ? Json{{"service_active", "active"},
                {"service_enabled", "enabled"}, {"service_pid", 2}} : safe_service;
        }
        ServiceProbe probe(budget_ms);
        for (;;)
        {
            if (auto service = probe.poll()) return *service;
            usleep(2000);
        }
    }

    std::string fault() const
    {
#ifdef GPIO_QUALIFICATION_TEST
        if (simulated && present(root / "fault")) return file(root / "fault", 64);
#endif
        return {};
    }

    std::string station() const
    {
        Json files = Json::object();
        if (simulated)
            return sha256(file(root / "station", 4096));
        const fs::path state("/var/lib/wsprrypi/application-state");
        const fs::path ini("/usr/local/etc/wsprrypi.ini");
        if (present(ini)) files[ini.string()] = sha256(file(ini, max_station_file, true));
        if (present(state))
        {
            unsigned count = 0;
            for (const auto& entry : fs::recursive_directory_iterator(state))
            {
                require(++count <= 1024 && !entry.is_symlink(), "Unexpected station state tree.");
                const auto name = entry.path().filename().string();
                if (entry.is_regular_file() &&
                    (name.ends_with(".json") || name.ends_with(".sqlite3") ||
                        name.ends_with(".sqlite3-wal") || name.ends_with(".sqlite3-journal")))
                    files[entry.path().string()] = sha256(file(entry, max_station_file, true));
            }
        }
        require(!files.empty(), "Could not identify stored station state.");
        return sha256(files.dump());
    }

    Json source(const Json& route, int service_budget_ms = ms_runtime_service) const
    {
        closed(route, {"profile", "gpio", "mux"});
        const int pin = integer(route.at("gpio"), 4, 20);
        require(route.at("profile") == profile && ((pin == 4 && route.at("mux") == "ALT0") ||
            (pin == 20 && route.at("mux") == "ALT5")), "Unsupported profile or explicit route.");
        require(simulated ? fault() != "model" : wsprrypi::bcm2835QualificationBoard(),
            "Expected BCM2835 Raspberry Pi Model A+.");
        const auto selection_bytes = file(root / "clock.json", 4096, true);
        const auto selection = parse(selection_bytes);
        closed(selection, {"policy", "ppm_millionths", "provenance"});
        const auto ppm = integer(selection.at("ppm_millionths"), -200'000'000, 200'000'000);
        const auto policy = selection.at("policy");
        require((policy == "selected" || (policy == "uncalibrated" && ppm == 0)) &&
            selection.at("provenance").is_string(), "Select an explicit reviewed correction.");
        const auto description = selection.at("provenance").get<std::string>();
        require(!description.empty() && description.size() <= 200, "Invalid correction provenance.");
        auto identity = file(simulated ? root / "device" : "/etc/machine-id", 64);
        auto boot = file(simulated ? root / "boot" : "/proc/sys/kernel/random/boot_id", 64);
        std::erase(identity, '\n'); std::erase(boot, '\n'); std::erase(boot, '-');
        require(service(service_budget_ms) == safe_service, "Normal WsprryPi unit changed.");
        return merge(merge(safe_service, route), {{"host", "wspr1.local"},
            {"model", "Raspberry Pi Model A+"}, {"backend", profile}, {"clock", "GPCLK0"},
            {"capabilities", capabilities}, {"device", token(identity)}, {"boot", token(boot)},
            {"revision", GPIO_QUALIFICATION_REVISION}, {"executable", executable},
            {"station", station()}, {"clock_policy", policy}, {"ppm_millionths", ppm},
            {"correction", sha256(selection_bytes)}, {"correction_provenance", description}});
    }

    std::unique_ptr<QualificationHardware> openHardware() const
    {
#ifdef GPIO_QUALIFICATION_TEST
        if (simulated) return std::make_unique<SimulatedHardware>(root / "fault");
#endif
        return wsprrypi::openBcm2835QualificationHardware();
    }
};

Json routeOf(const Json& source)
{
    return {{"profile", source.at("profile")}, {"gpio", source.at("gpio")}, {"mux", source.at("mux")}};
}

std::string requestKey(const Json& request, const std::string& op)
{
    std::string result = op;
    for (const auto* name : {"instance", "request"})
        if (request.contains(name)) { result += ":"; result += token(request.at(name)); }
    return result;
}

struct Tone
{
    std::string request;
    int generation{};
    std::int64_t reserved_until{};
    std::int64_t armed_at{};
    std::int64_t ceiling{};
    std::int64_t inactive_at{};
    Json requested, admitted;
    std::string phase{"reserved"};
    int revision{};
};

class Supervisor
{
public:
    Supervisor(const Environment& env, const Json& request, int listener)
        : env_(env), run_(token(request.at("run"))), source_(request.at("source")),
          pin_(source_.at("gpio").get<int>()), owner_(nonce()), listener_(listener),
          expires_(nowMs() + run_ms), hardware_(env.openHardware())
    {
        require(env.source(routeOf(source_)) == source_, "Source identity changed before claim.");
        const auto state = hardware_->observe();
        require(state.idle(), "BCM2835 clock, pins, pad bank, DMA, or PWM not positively idle.");
        original_ = state.bank;
        journal("claimed");
        receipts_["claim"] = merge(offReceipt(), {{"exclusive", true}, {"durable", true}});
    }

    Json dispatch(const Json& request)
    {
        const auto op = request.at("operation").get<std::string>();
        require(request.at("run") == run_ && request.at("source").dump() == source_.dump(),
            "Wrong run, route, source, or boot.");
        if (op == "claim")
        {
            require(request.at("lifetime_ms") == run_ms, "Changed run budget.");
            return receipts_.at("claim");
        }
        if (op == "lookup")
        {
            const auto kind = request.at("kind").get<std::string>();
            require(!hold_ || kind == "claim", "Owner state requires reconciliation.");
            if (kind == "reserve")
            {
                require(request.at("instance") == instance_ && request.at("worker") == worker_id_ &&
                    request.at("request") == tone_.request,
                    "No identified reservation.");
                return toneReceipt();
            }
            const auto id = requestKey(request, kind);
            require(receipts_.contains(id), "No identified receipt.");
            return receipts_.at(id);
        }
        if (op == "status" || op == "stop")
        {
            identifyTone(request);
            if (op == "stop")
                stop();
            else if (tone_.phase == "active")
            {
                try
                {
                    require(worker_ && worker_->call({{"operation", "status"}}).at("active") == true,
                        "Native active state changed.");
                }
                catch (...)
                {
                    uncertainStop();
                    throw;
                }
            }
            return toneReceipt();
        }
        if (op == "release")
        {
            require(!worker_ && !hold_, "Worker cleanup requires reconciliation.");
            release();
            return receipts_.at("release");
        }
        if (op == "close_group")
        {
            const auto id = requestKey(request, op);
            if (receipts_.contains(id)) return receipts_.at(id);
            require(worker_ && request.at("instance") == instance_, "Wrong drive group.");
            closeGroup();
            return receipts_.at(id);
        }
        require(!hold_ && !interrupted_ && nowMs() < expires_,
            service_failure_.empty() ? "Run is inhibited or normal service changed." : service_failure_.c_str());
        if (op == "admit_matrix")
        {
            require(!worker_ && !matrix_ && freshSource() &&
                request.at("duration_ms") == cutoff_ms, "Cannot admit changed matrix.");
            const auto& rows = request.at("points");
            require(rows.is_array() && rows.size() == 80, "Expected the entire 80-point matrix.");
            Json plans = Json::array();
            bool admitted = true;
            for (unsigned i = 0; i < 80; ++i)
            {
                require(rows[i].dump() == point(i).dump(), "Matrix point or order changed.");
                auto row = admission(source_, rows[i]);
                admitted = admitted && row.at("refusal").is_null() &&
                    row.at("lower_nhz").get<std::int64_t>() >= rows[i].at("carrier_hz").get<std::int64_t>() * 1'000'000'000 - 1'000'000'000 &&
                    row.at("upper_nhz").get<std::int64_t>() <= rows[i].at("carrier_hz").get<std::int64_t>() * 1'000'000'000 + 1'000'000'000;
                plans.push_back(std::move(row));
            }
            matrix_ = admitted;
            return merge(binding(), {{"admissions", plans}});
        }
        if (op == "open_group")
        {
            const auto id = requestKey(request, op);
            if (receipts_.contains(id)) return receipts_.at(id);
            require(matrix_ && !worker_ && index_ < 80 && index_ % 10 == 0 &&
                request.at("drive_code") == index_ / 10 && request.at("lifetime_ms") == group_ms &&
                freshSource(), "Unexpected or stale drive group.");
            require(expires_ - nowMs() >= cutoff_ms, "No remaining run budget.");
            instance_ = token(request.at("instance"));
            worker_id_ = nonce();
            group_deadline_ = std::min(expires_, nowMs() + group_ms);
            ready_until_ = nowMs() + ms_group_prepare;
            journal("preparing");
            try
            {
                worker_ = std::make_unique<Worker>(*hardware_, original_, pin_, index_ / 10,
                    group_deadline_, listener_);
                journal("starting-worker");
                receipts_[id] = merge(binding(), {{"instance", instance_},
                    {"worker", worker_id_}, {"phase", "pending"}});
                pollGroup();
                return receipts_.at(id);
            }
            catch (...)
            {
                hold_ = true;
                journal("unresolved");
                throw;
            }
        }
        require(worker_ && ready_until_ == 0 && request.at("instance") == instance_ &&
            request.at("worker") == worker_id_,
            "Wrong or expired drive worker.");
        if (op == "reserve")
        {
            require(tone_.phase == "terminal" || tone_.request.empty(),
                "Earlier reservation remains outstanding.");
            const auto id = token(request.at("request"));
            require(!requests_.contains(id) && index_ < 80 && index_ / 10 == group_index_ &&
                request.at("point").dump() == point(index_).dump() && request.at("duration_ms") == cutoff_ms &&
                freshSource(), "Changed or stale reservation.");
            const auto planned = admission(source_, request.at("point"));
            require(planned.dump() == request.at("admission").dump() && planned.at("refusal").is_null(),
                "Reservation does not match the native plan.");
            require(std::min(group_deadline_, expires_) > nowMs() + cutoff_ms + ms_reserve,
                "Insufficient group or run time for a reservation.");
            tone_ = {id, tone_.generation + 1, nowMs() + ms_reserve, 0, 0, 0,
                request.at("point"), planned, "reserved", 0};
            requests_.insert(id);
            journal("reserved");
            return toneReceipt();
        }
        require(op == "start", "Unsupported owner operation.");
        identifyTone(request);
        require(tone_.phase == "reserved" && nowMs() < tone_.reserved_until &&
            request.at("point").dump() == tone_.requested.dump() && request.at("duration_ms") == cutoff_ms &&
            request.at("admission").dump() == tone_.admitted.dump() && admission(source_, tone_.requested) == tone_.admitted &&
            freshSource(), "Start was cancelled, stale, or changed.");
        tone_.armed_at = nowMs();
        require(tone_.armed_at < tone_.reserved_until &&
            tone_.armed_at + cutoff_ms <= group_deadline_ &&
            tone_.armed_at + cutoff_ms <= expires_, "Reservation expired at Start acceptance.");
        tone_.ceiling = tone_.armed_at + cutoff_ms;
        tone_.phase = "accepted";
        tone_.revision = 1;
        try
        {
            journal("accepted");
            require(worker_->call({{"operation", "start"}, {"source", source_},
                {"point", tone_.requested}, {"admission", tone_.admitted},
                {"cutoff_at_ms", tone_.ceiling - ms_cleanup_margin}}).at("active") == true,
                "Native Start was not confirmed.");
            tone_.phase = "active";
            tone_.revision = 2;
            auto accepted = toneReceipt();
            accepted["phase"] = "accepted";
            accepted["revision"] = 1;
            return accepted;
        }
        catch (...)
        {
            uncertainStop();
            throw;
        }
    }

    bool released() const { return released_; }
    bool unresolved() const { return hold_; }

    void tick()
    {
        if (released_ || hold_) return;
        const auto now = nowMs();
        try
        {
            if (worker_ && ready_until_) pollGroup();
            if (!tone_.request.empty() && tone_.phase == "reserved" && now >= tone_.reserved_until) stop();
            if ((tone_.phase == "accepted" || tone_.phase == "active") &&
                now >= tone_.ceiling - ms_cleanup_margin)
            {
                interrupted_ = true;
                if (!finishExpiredWorker()) uncertainStop();
            }
            if (worker_ && worker_->exited())
            {
                interrupted_ = true;
                if (!finishExpiredWorker()) uncertainStop();
            }
            if (worker_ && now >= group_deadline_) closeGroup();
            if (now >= expires_ && !worker_ && !hold_) release();
            if (!released_ && !interrupted_)
            {
                const bool observe_systemd = env_.realService();
                if (!observe_systemd && now >= service_at_)
                {
                    observed_service_ = env_.service();
                    require(observed_service_ == safe_service, "Normal service changed.");
                    service_at_ = nowMs() + ms_service_poll;
                }
                else if (observe_systemd)
                {
                    if (!service_probe_ && now >= service_at_)
                        service_probe_ = std::make_unique<ServiceProbe>(ms_runtime_service);
                    if (service_probe_)
                    {
                        if (auto service = service_probe_->poll())
                        {
                            observed_service_ = *service;
                            service_probe_.reset();
                            service_at_ = nowMs() + ms_service_poll;
                        }
                    }
                }
            }
        }
        catch (const std::exception& error)
        {
            interrupted_ = true;
            service_failure_ = error.what();
            observed_service_ = {{"service_active", "unknown"},
                {"service_enabled", "unknown"}, {"service_pid", -1}};
            if (worker_ && !hold_) uncertainStop();
            service_probe_.reset();
            if (!hold_) journal("interrupted");
        }
    }

private:
    bool freshSource()
    {
        // A fresh gate supersedes any background probe. Never let a slow
        // synchronous phase leave an older probe to expire behind it.
        service_probe_.reset();
        const auto source = env_.source(routeOf(source_));
        observed_service_ = safe_service;
        service_at_ = nowMs() + ms_service_poll;
        return source == source_;
    }

    void pollGroup()
    {
        try
        {
            require(nowMs() < ready_until_, "Drive group did not become ready within its budget.");
            if (!worker_->ready()) return;
            auto opened = merge(offReceipt(), {{"instance", instance_}, {"worker", worker_id_},
                {"drive_code", index_ / 10}, {"listen", "127.0.0.1"}, {"ancillary_off", true},
                {"lifetime_ms", group_ms}, {"port", worker_->port()}, {"phase", "ready"}});
            journal("group-ready");
            ready_until_ = 0;
            receipts_["open_group:" + instance_] = std::move(opened);
        }
        catch (...)
        {
            uncertainStop();
            throw;
        }
    }

    Json binding() const
    {
        return {{"run", run_}, {"device", source_.at("device")}, {"boot", source_.at("boot")},
            {"owner", owner_}, {"profile", profile}, {"gpio", pin_}, {"route_mux", source_.at("mux")}};
    }

    Json offReceipt() const
    {
        const auto state = hardware_->observe();
        require(state.idle() && state.bank == original_, "Native idle or original bank unverified.");
        return merge(merge(binding(), nativeJson(state, pin_, false)),
            {{"bank", bankJson(state.bank)}, {"service", observed_service_}});
    }

    Json toneReceipt() const
    {
        const auto state = hardware_->observe();
        auto evidence = merge(merge(binding(), nativeJson(state, pin_, tone_.phase == "active")),
            {{"instance", instance_}, {"worker", worker_id_}, {"request", tone_.request},
                {"generation", tone_.generation}, {"revision", tone_.revision}, {"phase", tone_.phase},
                {"plan", tone_.admitted.at("plan")}, {"drive_code", tone_.admitted.at("drive_code")},
                {"clock", "GPCLK0"}, {"cutoff_ms", cutoff_ms}, {"service", observed_service_},
                {"bank", bankJson(state.bank)}, {"reserved_until_ms", tone_.reserved_until},
                {"armed_at_ms", tone_.armed_at}, {"ceiling_ms", tone_.ceiling},
                {"inactive_at_ms", tone_.inactive_at}});
        return evidence;
    }

    void journal(const char* phase)
    {
        if (env_.fault() == "publication") throw std::runtime_error("Simulated journal publication failure.");
        publish(env_.root / "recovery.json", merge(binding(), {{"phase", phase}, {"source", source_},
            {"expires_ms", expires_}, {"original_bank", bankJson(original_)}, {"instance", instance_},
            {"worker", worker_id_}, {"worker_bus", worker_ ? worker_->bus() : 0u},
            {"supervisor_pid", getpid()}, {"worker_pid", worker_ ? worker_->pid() : 0},
            {"request", tone_.request}, {"generation", tone_.generation}, {"point", tone_.requested},
            {"admission", tone_.admitted}, {"reserved_until_ms", tone_.reserved_until},
            {"armed_at_ms", tone_.armed_at}, {"ceiling_ms", tone_.ceiling},
            {"inactive_at_ms", tone_.inactive_at}}));
    }

    void identifyTone(const Json& request) const
    {
        require(!tone_.request.empty() && request.at("instance") == instance_ &&
            request.at("worker") == worker_id_ && request.at("request") == tone_.request &&
            request.at("generation") == tone_.generation, "Wrong request or stale generation.");
    }

    Bcm2835QualificationPlan nativePlan() const
    {
        return wsprrypi::planBcm2835Qualification(pin_, tone_.admitted.at("drive_code").get<int>(),
            tone_.admitted.at("carrier_hz").get<int>(), tone_.admitted.at("ppm_millionths").get<int>() / 1e6);
    }

    void stop()
    {
        if (tone_.phase == "reserved")
        {
            // This record fences late Start before acknowledging the Stop.
            tone_.phase = "cancelled-before-enable";
            tone_.revision = 3;
            tone_.inactive_at = nowMs();
            interrupted_ = true;
            journal("cancelled-before-enable");
        }
        else if (tone_.phase == "accepted" || tone_.phase == "active")
        {
            try
            {
                require(worker_ && worker_->call({{"operation", "stop"}}).at("active") == false,
                    "Native stop was not confirmed.");
                (void)offReceipt();
                tone_.inactive_at = nowMs();
                require(tone_.inactive_at <= tone_.ceiling, "Native shutdown missed its ceiling.");
                tone_.phase = "terminal";
                tone_.revision = 3;
                ++index_;
                journal("terminal");
            }
            catch (...)
            {
                uncertainStop();
                throw;
            }
        }
    }

    void uncertainStop()
    {
        interrupted_ = true;
        if (worker_)
        {
            const auto bus = worker_->bus();
            const bool retired = worker_->retire();
            const bool was_enabled = tone_.phase == "accepted" || tone_.phase == "active";
            const bool terminal = retired && (was_enabled ?
                hardware_->stop(nativePlan(), original_, bus) :
                hardware_->observe().idle() && hardware_->observe().bank == original_);
            // Loss of a worker also loses confirmation that its DMA allocation
            // was retired. Keep the hold even if the registers are now idle.
            tone_.phase = "unresolved";
            tone_.revision = 4;
            hold_ = true;
            journal(terminal ? "unresolved-worker" : "unresolved-output-or-restoration");
        }
    }

    bool finishExpiredWorker()
    {
        if (!worker_ || !tone_.ceiling || nowMs() < tone_.ceiling - ms_cleanup_margin)
            return false;
        if (!worker_->retire() || !worker_->finishedCleanly()) return false;
        auto receipt = offReceipt();
        tone_.inactive_at = nowMs();
        if (tone_.inactive_at > tone_.ceiling) return false;
        tone_.phase = "terminal";
        tone_.revision = 3;
        receipts_["close_group:" + instance_] = merge(receipt,
            {{"instance", instance_}, {"worker", worker_id_}, {"exited", true}});
        worker_.reset();
        journal("cutoff");
        return true;
    }

    void closeGroup()
    {
        stop();
        require(worker_ && !hold_, "Cannot close unresolved worker.");
        try
        {
            worker_->call({{"operation", "exit"}});
            const auto until = nowMs() + 200;
            while (!worker_->exited() && nowMs() < until) usleep(2000);
            require(worker_->exited() && worker_->finishedCleanly(), "Drive worker did not exit cleanly.");
            const auto receipt = merge(offReceipt(),
                {{"instance", instance_}, {"worker", worker_id_}, {"exited", true}});
            receipts_["close_group:" + instance_] = receipt;
            worker_.reset();
            ++group_index_;
            journal("group-closed");
        }
        catch (...)
        {
            hold_ = true;
            journal("unresolved");
            throw;
        }
    }

    void release()
    {
        try
        {
            auto receipt = merge(offReceipt(), {{"released", true}});
            receipts_["release"] = receipt;
            publish(env_.root / "terminal.json", {{"run", run_}, {"source", source_}, {"receipts", receipts_}});
            require(unlink((env_.root / "recovery.json").c_str()) == 0, "Could not clear verified recovery duty.");
            syncDir(env_.root);
            released_ = true;
        }
        catch (...)
        {
            hold_ = true;
            journal("unresolved");
            throw;
        }
    }

    const Environment& env_;
    const std::string run_;
    const Json source_;
    const int pin_;
    const std::string owner_;
    const int listener_;
    const std::int64_t expires_;
    std::unique_ptr<QualificationHardware> hardware_;
    QualificationBank original_{};
    std::unique_ptr<Worker> worker_;
    std::string instance_, worker_id_;
    std::set<std::string> requests_;
    Tone tone_;
    Json receipts_ = Json::object();
    Json observed_service_ = safe_service;
    std::int64_t service_at_{};
    std::unique_ptr<ServiceProbe> service_probe_;
    std::string service_failure_;
    std::int64_t group_deadline_{};
    std::int64_t ready_until_{};
    unsigned index_{};
    unsigned group_index_{};
    bool matrix_{};
    bool hold_{};
    bool interrupted_{};
    bool released_{};
};

void requestShape(const Json& request)
{
    require(request.is_object() && request.value("contract", "") == contract &&
        request.contains("operation") && request.at("operation").is_string(), "Wrong owner contract or operation.");
    token(request.at("call"));
    const auto op = request.at("operation").get<std::string>();
    std::set<std::string> expected{"contract", "operation", "call"};
    if (op == "review" || op == "review_matrix") expected.insert("route");
    else
    {
        expected.insert("source");
        if (op != "admit") { expected.insert("run"); token(request.at("run")); }
    }
    if (op == "admit") expected.insert({"point", "duration_ms"});
    else if (op == "claim") expected.insert("lifetime_ms");
    else if (op == "admit_matrix" || op == "review_matrix") expected.insert({"points", "duration_ms"});
    else if (op == "open_group") expected.insert({"instance", "drive_code", "lifetime_ms"});
    else if (op == "close_group") expected.insert("instance");
    else if (op == "reserve" || op == "start" || op == "status" || op == "stop")
    {
        expected.insert({"instance", "worker", "request"});
        if (op != "reserve") expected.insert("generation");
        if (op == "reserve" || op == "start") expected.insert({"point", "admission", "duration_ms"});
    }
    else if (op == "lookup")
    {
        expected.insert("kind");
        const auto kind = request.at("kind").get<std::string>();
        require(kind == "claim" || kind == "open_group" || kind == "reserve" ||
            kind == "close_group" || kind == "release", "Unsupported lookup.");
        if (kind == "open_group" || kind == "close_group" || kind == "reserve") expected.insert("instance");
        if (kind == "reserve") expected.insert({"request", "worker"});
    }
    else require(op == "review" || op == "release", "Unknown owner operation.");

    require(request.size() == expected.size(), "Invalid request fields.");
    for (const auto& name : expected)
        require(request.contains(name), "Missing or unexpected request field.");
    for (const auto* name : {"instance", "worker", "request"})
        if (request.contains(name)) token(request.at(name));
    if (request.contains("generation")) integer(request.at("generation"), 1, 80);
    if (request.contains("duration_ms")) integer(request.at("duration_ms"), cutoff_ms, cutoff_ms);
    if (request.contains("lifetime_ms"))
    {
        const int limit = op == "claim" ? run_ms : group_ms;
        integer(request.at("lifetime_ms"), limit, limit);
    }
    if (op == "open_group") integer(request.at("drive_code"), 0, 7);
}

sockaddr_un endpoint(const Environment& env)
{
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto path = (env.root / "owner.sock").string();
    require(path.size() < sizeof(address.sun_path), "Private owner socket path is too long.");
    std::copy(path.begin(), path.end(), address.sun_path);
    return address;
}

std::string receive(int socket)
{
    std::string message(max_message + 1, '\0');
    const auto received = recv(socket, message.data(), message.size(), MSG_DONTWAIT);
    require(received > 0 && static_cast<std::size_t>(received) <= max_message, "Incomplete or oversized owner packet.");
    message.resize(received);
    return message;
}

void sendBytes(int socket, std::string_view message)
{
    require(message.size() <= max_message &&
        send(socket, message.data(), message.size(), MSG_DONTWAIT | MSG_NOSIGNAL) == ssize_t(message.size()),
        "Owner packet write was not completed.");
}

std::string proxy(const Environment& env, std::string_view bytes)
{
    Fd socket_fd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
    const auto address = endpoint(env);
    if (connect(socket_fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        require(errno == ENOENT || errno == ECONNREFUSED, "Private owner channel unavailable.");
        return {};
    }
    sendBytes(socket_fd.get(), bytes);
    pollfd poller{socket_fd.get(), POLLIN, 0};
    require(poll(&poller, 1, 2000) > 0, "No reply from the identified supervisor.");
    return receive(socket_fd.get());
}

Json refusal(const std::exception& error, bool unresolved)
{
    return {{"phase", unresolved ? "unresolved" : "refused"},
        {"reason", std::string(error.what()).substr(0, 240)}};
}

void supervise(const Environment& env, const Json& claim, int ready)
{
    wsprrypi::Bcm2835QualificationLease lease;
    Fd test_lease(env.simulated ? open((env.root / "lease").c_str(),
        O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600) : -1);
    if (env.simulated)
    {
        require(test_lease.get() >= 0 && flock(test_lease.get(), LOCK_EX | LOCK_NB) == 0 &&
            !present(env.root / "recovery.json"), "Device-wide lease or recovery unavailable.");
    }
    else lease.acquire();
    const auto address = endpoint(env);
    Fd listener(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    if (unlink(address.sun_path) != 0)
        require(errno == ENOENT, "Could not clear stale private endpoint.");
    require(bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 &&
        listen(listener.get(), 8) == 0, "Could not create private owner listener.");
    Supervisor owner(env, claim, listener.get());
    [[maybe_unused]] const auto signalled = write(ready, "1", 1);
    close(ready);
    while (!owner.released())
    {
        owner.tick();
        pollfd poller{listener.get(), POLLIN, 0};
        if (poll(&poller, 1, 15) <= 0) continue;
        Fd peer(accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK));
        if (peer.get() < 0) continue;
        ucred credentials{};
        socklen_t credential_size = sizeof(credentials);
        if (getsockopt(peer.get(), SOL_SOCKET, SO_PEERCRED, &credentials, &credential_size) ||
            credentials.uid != geteuid()) continue;
        try
        {
            pollfd incoming{peer.get(), POLLIN, 0};
            if (poll(&incoming, 1, 15) <= 0) continue;
            const auto bytes = receive(peer.get());
            const auto request = parse(bytes);
            requestShape(request);
            Json receipt;
            try { receipt = owner.dispatch(request); }
            catch (const std::exception& error) { receipt = refusal(error, owner.unresolved()); }
            sendBytes(peer.get(), envelope(bytes, request, receipt).dump());
        }
        catch (...) {}
    }
    unlink(address.sun_path);
}

void startSupervisor(const Environment& env, const Json& claim)
{
    int pipefd[2];
    require(pipe2(pipefd, O_CLOEXEC) == 0, "Could not start qualification supervisor.");
    Fd reader(pipefd[0]);
    const auto child = fork();
    if (child == 0)
    {
        close(pipefd[0]);
        setsid();
        const int null_device = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (null_device < 0) _exit(2);
        for (int fd = 0; fd <= 2; ++fd) dup2(null_device, fd);
        if (null_device > 2) close(null_device);
        try { supervise(env, claim, pipefd[1]); }
        catch (...) { _exit(2); }
        _exit(0);
    }
    close(pipefd[1]);
    require(child > 0, "Could not fork qualification supervisor.");
    pollfd waiter{reader.get(), POLLIN, 0};
    char ready{};
    require(poll(&waiter, 1, 1500) > 0 && read(reader.get(), &ready, 1) == 1 && ready == '1',
        "Qualification claim did not return readiness. Look up the original run.");
}

Json terminal(const Environment& env, const Json& request)
{
    const auto saved = parse(file(env.root / "terminal.json", max_message, true));
    require(saved.at("run") == request.at("run") && saved.at("source") == request.at("source"),
        "Terminal evidence belongs to a different run, pin, or boot.");
    const auto kind = request.at("operation") == "lookup" ?
        request.at("kind").get<std::string>() : request.at("operation").get<std::string>();
    require(kind == "claim" || kind == "open_group" || kind == "close_group" || kind == "release",
        "No matching terminal receipt.");
    return saved.at("receipts").at(requestKey(request, kind));
}

std::string dispatch(const Environment& env, std::string_view bytes)
{
    const auto request = parse(bytes);
    requestShape(request);
    Json result;
    try
    {
        const auto op = request.at("operation");
        if (op == "review_matrix")
        {
            const auto deadline = nowMs() + ms_review_owner;
            const auto source = env.source(request.at("route"), ms_review_service);
            const auto& points = request.at("points");
            require(points.is_array() && points.size() == 80, "Expected the entire 80-point matrix.");
            Json plans = Json::array();
            for (unsigned index = 0; index < points.size(); ++index)
            {
                require(nowMs() < deadline - ms_review_service, "Read-only matrix planning deadline expired.");
                require(points[index].dump() == point(index).dump(), "Matrix point or order changed.");
                plans.push_back(admission(source, points[index]));
            }
            require(nowMs() < deadline - ms_review_service, "Read-only matrix planning deadline expired.");
            require(env.source(request.at("route"), ms_review_service) == source,
                "Source identity changed during Review.");
            require(nowMs() < deadline, "Read-only matrix Review deadline expired.");
            result = {{"source", source}, {"admissions", plans}};
        }
        else if (op == "review")
            result = env.source(request.at("route"));
        else if (op == "admit")
        {
            const auto source = env.source(routeOf(request.at("source")));
            require(source.dump() == request.at("source").dump(), "Station, boot, build, service, or correction changed.");
            result = admission(source, request.at("point"));
        }
        else
        {
            auto reply = proxy(env, bytes);
            if (!reply.empty()) return reply;
            if (op == "claim")
            {
                if (present(env.root / "terminal.json"))
                {
                    const auto saved = parse(file(env.root / "terminal.json", max_message, true));
                    if (saved.at("run") == request.at("run"))
                        return envelope(bytes, request, terminal(env, request)).dump();
                }
                require(!present(env.root / "recovery.json"), "An unresolved run holds the device lease.");
                startSupervisor(env, request);
                reply = proxy(env, bytes);
                require(!reply.empty(), "Claim outcome is unknown.");
                return reply;
            }
            require(!present(env.root / "recovery.json"), "No live owner; durable recovery hold retained.");
            result = terminal(env, request);
        }
    }
    catch (const std::exception& error) { result = refusal(error, present(env.root / "recovery.json")); }
    return envelope(bytes, request, result).dump();
}
}
}

int main(int argc, char** argv)
{
    using namespace gpio_qualification;
    try
    {
        umask(0077);
        std::signal(SIGPIPE, SIG_IGN);
        Environment env{wsprrypi::bcm2835_qualification_directory, false, {}};
        bool streaming = argc == 2 && std::string_view(argv[1]) == "--runtime-stream";
#ifdef GPIO_QUALIFICATION_TEST
        if ((argc == 3 || argc == 4) && std::string_view(argv[1]) == "--fixture")
        {
            env.root = argv[2];
            env.simulated = true;
            streaming = argc == 4 && std::string_view(argv[3]) == "--runtime-stream";
            require(argc == 3 || streaming, "Invalid fixture mode.");
        }
#endif
        require((env.simulated || ((argc == 1 || streaming) && geteuid() == 0)) &&
            (env.simulated || !getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS")),
            "Use the authorized fixed BCM2835 owner entry point.");
        struct stat state{};
        require(lstat(env.root.c_str(), &state) == 0 && S_ISDIR(state.st_mode) &&
            state.st_uid == geteuid() && (state.st_mode & 077) == 0,
            "Private owner state directory is not provisioned.");
        env.executable = sha256(file(fs::read_symlink("/proc/self/exe"), max_station_file));
        std::array<char, max_message + 1> buffer;
        if (streaming)
        {
            std::cout << "wsprrypi.gpio-qualification-owner.v1 ready\n" << std::flush;
            for (;;)
            {
                if (!std::cin.getline(buffer.data(), buffer.size()))
                {
                    require(std::cin.eof() && std::cin.gcount() == 0, "Incomplete runtime request.");
                    break;
                }
                require(!std::cin.eof() && std::cin.gcount() > 1, "Incomplete runtime request.");
                std::cout << dispatch(env, {buffer.data(), static_cast<std::size_t>(std::cin.gcount() - 1)})
                    << '\n' << std::flush;
                require(std::cout.good(), "Runtime connection closed.");
            }
            return 0;
        }
        std::cin.read(buffer.data(), buffer.size());
        const auto count = std::cin.gcount();
        require(count > 0 && static_cast<std::size_t>(count) <= max_message, "Expected exactly one bounded request.");
        std::cout << dispatch(env, {buffer.data(), static_cast<std::size_t>(count)}) << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "GPIO qualification request refused: " << error.what() << '\n';
        return 2;
    }
}
