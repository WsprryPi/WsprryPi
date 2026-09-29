#pragma once

#include "wtp_server/input_buffer.hpp"
#include "wtp_server/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace wsprrypico::wtp {

enum class State { Empty, Loaded, Armed, Running, Complete, Aborted, Missed, Failed };
enum class ClockState { Unsynchronized, Synchronized, Holdover };
enum class LeapState { Normal, InsertPending, DeletePending, Unknown };

struct ClockSnapshot {
    ClockState state = ClockState::Unsynchronized;
    std::uint64_t utc_now_ns = 0;
    std::uint64_t monotonic_now_ns = 0;
    std::uint64_t uncertainty_ns = 0;
    std::uint64_t sync_age_ns = 0;
    LeapState leap = LeapState::Unknown;
    std::optional<std::uint64_t> leap_transition_utc_ns;
    bool operator==(const ClockSnapshot&) const = default;
};

class Clock {
  public:
    virtual ~Clock() = default;
    [[nodiscard]] virtual ClockSnapshot snapshot() const = 0;
};

class IdentitySource {
  public:
    virtual ~IdentitySource() = default;
    [[nodiscard]] virtual std::string new_boot_id() = 0;
};

struct RfEvent {
    std::uint64_t offset_ns = 0;
    std::uint64_t duration_ns = 0;
    bool rf_on = false;
    std::optional<std::uint64_t> frequency_nhz;

    bool operator==(const RfEvent&) const = default;
};

// Maximum jobs must not depend on one 20,480-byte heap block. Eight bounded
// pages keep random access and value semantics while using the nullable input
// allocator that the target can refuse without invoking the SDK panic path.
class EventList {
  public:
    enum class Failure { None, LimitExceeded, AllocationFailed };
    static constexpr std::size_t page_events = 64;
    static constexpr std::size_t maximum_events = 512;

    template <bool Constant> class Iterator {
      public:
        using Owner = std::conditional_t<Constant, const EventList, EventList>;
        using value_type = RfEvent;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Constant, const RfEvent&, RfEvent&>;
        using pointer = std::conditional_t<Constant, const RfEvent*, RfEvent*>;
        using iterator_category = std::forward_iterator_tag;

        Iterator() = default;
        Iterator(Owner* owner, std::size_t index) : owner_(owner), index_(index) {}
        reference operator*() const {
            return (*owner_)[index_];
        }
        pointer operator->() const {
            return &(*owner_)[index_];
        }
        Iterator& operator++() {
            ++index_;
            return *this;
        }
        Iterator operator++(int) {
            auto prior = *this;
            ++*this;
            return prior;
        }
        bool operator==(const Iterator&) const = default;

      private:
        Owner* owner_ = nullptr;
        std::size_t index_ = 0;
    };

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;

    EventList() = default;
    EventList(std::initializer_list<RfEvent> values) {
        if (!reserve(values.size()))
            return;
        for (const auto& value : values)
            if (!push_back(value))
                return;
    }
    EventList(const EventList& other) {
        copy(other);
    }
    EventList& operator=(const EventList& other) {
        if (this != &other) {
            clear();
            copy(other);
        }
        return *this;
    }
    EventList(EventList&& other) noexcept
        : pages_(std::move(other.pages_)), size_(std::exchange(other.size_, 0)),
          failure_(std::exchange(other.failure_, Failure::None)) {}
    EventList& operator=(EventList&& other) noexcept {
        if (this != &other) {
            clear();
            pages_ = std::move(other.pages_);
            size_ = std::exchange(other.size_, 0);
            failure_ = std::exchange(other.failure_, Failure::None);
        }
        return *this;
    }
    ~EventList() {
        destroy();
    }

    bool reserve(std::size_t count) {
        if (count > maximum_events) {
            failure_ = Failure::LimitExceeded;
            return false;
        }
        const auto needed = (count + page_events - 1) / page_events;
        for (std::size_t page = 0; page < needed; ++page) {
            if (pages_[page].capacity())
                continue;
            if (!pages_[page].reserve(page_events * sizeof(RfEvent))) {
                failure_ = Failure::AllocationFailed;
                return false;
            }
        }
        return true;
    }
    bool push_back(const RfEvent& value) {
        if (size_ >= maximum_events || !reserve(size_ + 1)) {
            if (failure_ == Failure::None)
                failure_ = Failure::LimitExceeded;
            return false;
        }
        new (slot(size_)) RfEvent(value);
        ++size_;
        return true;
    }
    bool resize(std::size_t count) {
        if (count < size_) {
            while (size_ > count)
                at(--size_).~RfEvent();
            return true;
        }
        if (!reserve(count))
            return false;
        while (size_ < count) {
            new (slot(size_)) RfEvent();
            ++size_;
        }
        return true;
    }
    bool assign(std::size_t count, const RfEvent& value) {
        clear();
        if (!reserve(count))
            return false;
        while (size_ < count)
            if (!push_back(value))
                return false;
        return true;
    }
    void clear() {
        destroy();
        for (auto& page : pages_)
            page = {};
        failure_ = Failure::None;
    }
    bool valid() const {
        return failure_ == Failure::None;
    }
    Failure failure() const {
        return failure_;
    }
    bool empty() const {
        return size_ == 0;
    }
    std::size_t size() const {
        return size_;
    }
    RfEvent& operator[](std::size_t index) {
        return at(index);
    }
    const RfEvent& operator[](std::size_t index) const {
        return at(index);
    }
    RfEvent& back() {
        return at(size_ - 1);
    }
    const RfEvent& back() const {
        return at(size_ - 1);
    }
    iterator begin() {
        return {this, 0};
    }
    iterator end() {
        return {this, size_};
    }
    const_iterator begin() const {
        return {this, 0};
    }
    const_iterator end() const {
        return {this, size_};
    }
    bool operator==(const EventList& other) const {
        return failure_ == other.failure_ && size_ == other.size_ &&
               std::equal(begin(), end(), other.begin());
    }

  private:
    RfEvent& at(std::size_t index) {
        return *slot(index);
    }
    const RfEvent& at(std::size_t index) const {
        return *slot(index);
    }
    RfEvent* slot(std::size_t index) {
        return reinterpret_cast<RfEvent*>(pages_[index / page_events].data()) + index % page_events;
    }
    const RfEvent* slot(std::size_t index) const {
        return reinterpret_cast<const RfEvent*>(pages_[index / page_events].data()) +
               index % page_events;
    }
    void destroy() {
        while (size_)
            at(--size_).~RfEvent();
    }
    void copy(const EventList& other) {
        failure_ = other.failure_;
        if (!other.valid() || !reserve(other.size_))
            return;
        for (const auto& value : other)
            if (!push_back(value))
                return;
    }

    std::array<InputBuffer, maximum_events / page_events> pages_;
    std::size_t size_ = 0;
    Failure failure_ = Failure::None;
};

struct Job {
    std::string job_id;
    std::string profile = "rf-events/1";
    std::string mode;
    std::uint64_t total_duration_ns = 0;
    EventList events;
    bool allow_frequency_adjustment = false;

    bool operator==(const Job&) const = default;
};

// Stable typed identity, streamed without retaining a serialized event copy.
class JobDigestBuilder {
  public:
    JobDigestBuilder(const Job& header, std::size_t event_count);
    void append(const RfEvent& event);
    PayloadDigest finish() {
        return digest_.finish();
    }

  private:
    void number(std::uint64_t value);
    void text(std::string_view value);
    Sha256 digest_;
};
PayloadDigest job_digest(const Job& job);

enum class EngineState { Idle, Armed, Running, Complete, Failed, Missed };

// The requested UTC second supplies an exclusive latest-start boundary.
constexpr std::uint64_t start_window_ns(std::uint64_t utc_ns) {
    return 1'000'000'000 - utc_ns % 1'000'000'000;
}

struct LocalStartConditions {
    const Clock* clock = nullptr;
    std::uint64_t start_utc_ns = 0;
    std::uint64_t maximum_uncertainty_ns = 0;
    std::uint64_t maximum_holdover_age_ns = 0;
    std::uint64_t start_adjustment_ns = 0;
};

struct EngineReport {
    EngineState state = EngineState::Idle;
    bool output_active = false;
    std::optional<std::uint64_t> launch_monotonic_ns = {};
};

struct FrequencyAdjustment {
    std::size_t event_index;
    std::uint64_t requested_frequency_nhz;
    std::uint64_t realized_frequency_nhz;

    bool operator==(const FrequencyAdjustment&) const = default;
};

// LOAD adjustments never change after preparation. Share one immutable list
// across active-job, response-cache and terminal-history lifetimes. Copying a
// response must not duplicate a maximum 512-entry allocation.
class AdjustmentList {
  public:
    AdjustmentList() = default;
    AdjustmentList(std::vector<FrequencyAdjustment> values) {
        if (!values.empty())
            values_ = std::make_shared<const std::vector<FrequencyAdjustment>>(std::move(values));
    }
    std::size_t size() const {
        return values_ ? values_->size() : 0;
    }
    bool empty() const {
        return size() == 0;
    }
    const FrequencyAdjustment* begin() const {
        return values_ ? values_->data() : nullptr;
    }
    const FrequencyAdjustment* end() const {
        return values_ ? values_->data() + size() : nullptr;
    }
    const FrequencyAdjustment& operator[](std::size_t index) const {
        return (*values_)[index];
    }
    void clear() {
        values_.reset();
    }
    bool operator==(const AdjustmentList& other) const {
        if (values_ == other.values_)
            return true;
        if (size() != other.size())
            return false;
        for (std::size_t i = 0; i < size(); ++i)
            if ((*this)[i] != other[i])
                return false;
        return true;
    }

  private:
    std::shared_ptr<const std::vector<FrequencyAdjustment>> values_;
};

struct PrepareResult {
    bool accepted = false;
    std::vector<FrequencyAdjustment> adjustments;
};

class RfEngine {
  public:
    static constexpr std::uint64_t maximum_completion_acknowledgement_ns = 100'000'000;
    virtual ~RfEngine() = default;
    virtual PrepareResult prepare(const Job& job) = 0;
    virtual bool set_frequency_correction_ppb(std::int32_t) {
        return false;
    }
    virtual std::string_view diagnostic() const {
        return {};
    }
    [[nodiscard]] virtual bool schedules_locally() const {
        return false;
    }
    // Immutable capability: after successful schedule()/begin(), execution
    // retains no references into the caller's Job. Default preserves input for
    // adapters that borrow it. Preparation alone does not transfer ownership.
    [[nodiscard]] virtual bool owns_execution_plan() const {
        return false;
    }
    [[nodiscard]] virtual std::uint64_t start_resolution_ns() const {
        return 1;
    }
    // Finite locally scheduled hardware may finish its IRQ acknowledgement just
    // after nominal RF end. This never authorizes additional waveform samples.
    [[nodiscard]] virtual std::uint64_t completion_acknowledgement_ns() const {
        return 0;
    }
    virtual bool schedule(const Job&, std::uint64_t, const LocalStartConditions&) {
        return false;
    }
    virtual bool begin(const Job& job, std::uint64_t start_monotonic_ns) = 0;
    [[nodiscard]] virtual EngineReport poll(std::uint64_t monotonic_now_ns) = 0;
    virtual bool disable(std::uint64_t deadline_monotonic_ns) = 0;
    [[nodiscard]] virtual bool output_active() const = 0;
};

enum class ErrorCode {
    None,
    InvalidFrame,
    InvalidMessage,
    UnsupportedVersion,
    HelloRequired,
    UnknownOperation,
    AuthenticationRequired,
    SessionReplaced,
    Busy,
    NotOwner,
    LeaseExpired,
    RequestIdReuse,
    InvalidState,
    JobNotFound,
    JobIdConflict,
    JobLimitExceeded,
    UnsupportedProfile,
    UnsupportedMode,
    FrequencyRejected,
    ArmConflict,
    ClockUnsynchronized,
    ClockUncertain,
    LeapUnsafe,
    ArmTooLate,
    ArmTooFar,
    MissedStart,
    OutputStateUnknown,
    DeviceFault,
    InternalError,
};

struct HelloBody {
    std::vector<std::string> versions;
};
struct ClaimBody {
    std::string owner_id;
    std::uint32_t lease_ms = 0;
};
struct RenewBody {
    std::string owner_id;
    std::uint32_t lease_ms = 0;
};
struct ArmBody {
    std::string job_id;
    std::uint64_t start_utc_ns = 0;
    std::uint64_t max_start_uncertainty_ns = 0;
};
struct AbortBody {
    std::string job_id;
};
struct PingBody {
    std::optional<std::string> token;
};

// Internal decoder result for an already resident active job. It cannot create
// a new job: dispatch rechecks ownership and the current/retained typed digest.
struct LoadReplayBody {
    std::string job_id;
    PayloadDigest digest;
};
using RequestBody = std::variant<std::monostate, HelloBody, ClaimBody, RenewBody, Job, ArmBody,
                                 AbortBody, PingBody, LoadReplayBody>;

struct Request {
    std::string protocol = "WTP/1";
    std::string session_id;
    std::string request_id;
    std::string principal;
    std::string operation;
    PayloadDigest payload_digest{};
    RequestBody body;
    bool body_valid = true; // Adapter schema result; checked after replay/operation recognition.
};

struct TerminalRecord {
    std::string job_id;
    State state;
    std::uint64_t ended_monotonic_ns;
    bool output_active;
    ErrorCode error;
    bool operator==(const TerminalRecord&) const = default;
};

// Internal activity observation: no identity/history copies or retained views.
// Each observation still queries the engine's live output state.
struct ServiceActivity {
    State state;
    bool output_active;
    bool owned;
};

struct ServiceStatus {
    std::string boot_id;
    State state;
    bool output_active;
    std::optional<std::string> owner_id;
    std::optional<std::string> job_id;
    std::vector<TerminalRecord> terminal_records;
    bool operator==(const ServiceStatus&) const = default;
};

struct Response {
    bool ok = false;
    ErrorCode error = ErrorCode::None;
    State state = State::Empty;
    std::string job_id;
    std::string boot_id;
    std::string owner_id;
    std::uint64_t start_monotonic_ns = 0;
    std::uint64_t expires_monotonic_ns = 0;
    std::uint32_t granted_lease_ms = 0;
    bool output_active = false;
    bool close_connection = false;
    std::optional<std::string> ping_token;
    AdjustmentList adjustments;
    std::optional<ClockSnapshot> clock_snapshot;
    std::optional<ServiceStatus> status_snapshot;
    std::uint64_t start_utc_ns = 0;

    bool operator==(const Response&) const = default;
};

struct ServiceConfig {
    std::string capability_engine = "inhibited-no-rf";
    std::vector<std::string> supported_modes{"wspr", "qrss", "fskcw", "dfcw", "cw", "tone"};
    std::uint64_t minimum_frequency_nhz = 1;
    std::uint64_t maximum_frequency_nhz = std::numeric_limits<std::uint64_t>::max();
    std::size_t max_events = 512;
    std::uint64_t max_job_duration_ns = 86'400'000'000'000ULL;
    std::uint64_t minimum_arm_lead_ns = 100'000'000ULL;
    std::uint64_t maximum_arm_ahead_ns = 604'800'000'000'000ULL;
    std::uint64_t maximum_arm_uncertainty_ns = 1'000'000ULL;
    std::uint64_t maximum_holdover_age_ns = 0;
    std::uint64_t output_disable_timeout_ns = 100'000'000ULL;
    std::size_t response_cache_entries = 8;
    std::uint64_t response_cache_ttl_ns = 300'000'000'000ULL;
    std::size_t terminal_record_entries = 8;
    std::uint64_t terminal_record_ttl_ns = 3'600'000'000'000ULL;
};

// Narrow endpoint-facing surface. A host may serialize this through its own
// transmitter authority without changing WTP framing or session semantics.
class IJobService {
  public:
    virtual ~IJobService() = default;
    virtual Response handle(Request&& request) = 0;
    virtual void poll() = 0;
    [[nodiscard]] virtual ServiceStatus status() const = 0;
    [[nodiscard]] virtual std::string active_load_replay_id() const = 0;
    [[nodiscard]] virtual const ServiceConfig& config() const = 0;
};

class JobService : public IJobService {
  public:
    using Admission = std::function<bool(const Request&)>;
    JobService(Clock& clock, RfEngine& engine, IdentitySource& identities,
               ServiceConfig config = {});

    // Lvalue callers retain value semantics. Production adapters transfer a
    // decoded request so a maximum LOAD does not allocate a second event list.
    Response handle(const Request& request);
    Response handle(Request&& request) override;
    Response handle(Request&& request, const Admission& admission);
    // Physical Console safety control only; never exposed as a network operation.
    Response local_abort();
    void poll() override;
    void reset();
    [[nodiscard]] ServiceActivity activity() const;
    [[nodiscard]] std::string active_load_replay_id() const override {
        return job_ && (state_ == State::Loaded || state_ == State::Armed) ? job_->job_id : "";
    }
    [[nodiscard]] ServiceStatus status() const override;
    [[nodiscard]] std::string_view owner_session_id() const {
        return owner_ ? std::string_view(owner_->session_id) : std::string_view{};
    }
    [[nodiscard]] ClockSnapshot clock_snapshot() const {
        return clock_.snapshot();
    }
    [[nodiscard]] const ServiceConfig& config() const override {
        return config_;
    }

  private:
    struct Session {
        std::string session_id;
        std::string principal;
        std::uint64_t last_seen_monotonic_ns;
    };
    struct Owner {
        std::string owner_id;
        std::string session_id;
        std::string principal;
        std::uint64_t expires_monotonic_ns;
        bool release_after_terminal = false;
    };
    struct ArmRecord {
        std::string job_id;
        std::uint64_t start_utc_ns;
        std::uint64_t max_uncertainty_ns;
        std::uint64_t start_monotonic_ns;
        Response response;
        bool scheduled_locally = false;
        std::optional<std::uint64_t> launch_monotonic_ns = {};
    };
    struct RetainedJob {
        std::string job_id;
        PayloadDigest digest;
        Response load_response;
        std::optional<ArmRecord> arm;
    };

    struct ReplayEntry {
        std::string session_id;
        std::string request_id;
        PayloadDigest payload_digest;
        Response response;
        std::uint64_t completed_monotonic_ns;
        std::uint64_t lru;
    };

    Response handle_owned(Request& request, const Admission& admission);
    Response dispatch(Request& request, const Admission& admission);
    Response abort_job(std::string_view job_id);
    Response reject(ErrorCode code) const;
    Response success() const;
    bool valid_id(std::string_view value) const;
    bool owns(const Request& request) const;
    ErrorCode validate_job(const Job& job) const;
    ErrorCode validate_arm(const ArmBody& arm, const ClockSnapshot& now,
                           std::uint64_t& start_monotonic_ns) const;
    void expire_resources(std::uint64_t monotonic_now_ns);
    void record_terminal(State state, ErrorCode error, std::uint64_t now_ns);
    void clear_job();
    void release_execution_input();
    void touch_terminal(std::string_view job_id);
    void prune_replay(std::uint64_t now_ns);
    void prune_terminals(std::uint64_t now_ns);
    void prune_sessions(std::uint64_t now_ns);
    std::optional<Response> replay(const Request& request, std::uint64_t now_ns, bool& conflict);
    void remember(const Request& request, const Response& response, std::uint64_t now_ns);

    Clock& clock_;
    RfEngine& engine_;
    IdentitySource& identities_;
    ServiceConfig config_;
    std::vector<Session> sessions_;
    std::optional<Owner> owner_;
    std::optional<Job> job_;
    PayloadDigest job_digest_{};
    AdjustmentList adjustments_;
    std::optional<ArmRecord> arm_;
    std::deque<ReplayEntry> replay_cache_;
    std::deque<TerminalRecord> terminal_records_;
    std::deque<RetainedJob> retained_jobs_;
    State state_ = State::Empty;
    std::string boot_id_;
    std::uint64_t lru_sequence_ = 0;
    bool ready_ = false;
};

} // namespace wsprrypico::wtp
