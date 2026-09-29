#include "wtp_server/endpoint.hpp"

#include "wtp_server/codec.hpp"
#include "wtp_server/memory_budget.hpp"

#include <algorithm>
#include <limits>

namespace wsprrypico::wtp {
namespace {
std::string state_body(const std::optional<std::string>& job, State state, bool active) {
    return "{\"job_id\":" + (job ? json::quote(*job) : "null") +
           ",\"state\":" + json::quote(state_name(state)) +
           ",\"output_active\":" + (active ? "true" : "false") + '}';
}
} // namespace
Endpoint::Endpoint(IJobService& service, std::string device, std::string firmware,
                   std::string product, EventIdSource event_ids)
    : service_(service), device_id_(std::move(device)), firmware_version_(std::move(firmware)),
      product_(std::move(product)),
      boot_(service.status().boot_id), event_ids_(std::move(event_ids)),
      observed_(service.status()) {}
void Endpoint::connect(std::string principal) {
    disconnect();
    principal_ = std::move(principal);
    parser_ = FrameParser{};
    closed_ = false;
    observed_ = service_.status();
    if (boot_ != observed_.boot_id) {
        boot_ = observed_.boot_id;
        event_id_ = 0;
    }
}
void Endpoint::disconnect() {
    parser_.end_of_stream();
    parser_ = FrameParser{}; // Release retained frame storage between transport uses.
    pending_input_ = {};
    output_.clear();
    offset_ = 0;
    queued_bytes_ = 0;
    session_.clear();
    principal_.clear();
    closed_ = true;
    closing_ = false;
}
bool Endpoint::enqueue(std::string text, std::uint64_t now, bool advisory) {
    if (text.empty() || text.size() > kMaximumPayloadBytes || output_.size() >= 8 ||
        queued_bytes_ + text.size() + kFrameHeaderBytes > 131072 || !memory_admitted(1024)) {
        if (!advisory)
            disconnect();
        return false;
    }
    const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    // Keep the already encoded payload. Framing must not allocate another
    // maximum-sized buffer while the job and its adjustment response coexist.
    OutputFrame frame{encode_frame_header(bytes), std::move(text)};
    if (output_.empty())
        last_tx_progress_ms_ = now;
    queued_bytes_ += frame.size();
    output_.push_back(std::move(frame));
    return true;
}
bool Endpoint::enqueue(LoadResponseStream text, std::uint64_t now) {
    if (text.empty() || text.size() > kMaximumPayloadBytes || output_.size() >= 8 ||
        queued_bytes_ + text.size() + kFrameHeaderBytes > 131072 || !memory_admitted(1024)) {
        disconnect();
        return false;
    }
    OutputFrame frame{encode_frame_header(text.size(), text.checksum()), std::move(text)};
    if (output_.empty())
        last_tx_progress_ms_ = now;
    queued_bytes_ += frame.size();
    output_.push_back(std::move(frame));
    return true;
}
void Endpoint::event(std::string_view name, std::string body, std::uint64_t now) {
    if (session_.empty() || closed_ || closing_)
        return;
    const auto id = event_ids_ ? event_ids_(boot_) :
        event_id_ == std::numeric_limits<std::uint64_t>::max() ? std::nullopt :
        std::optional<std::uint64_t>(event_id_++);
    if (!id) {
        disconnect();
        return;
    }
    auto text =
        "{\"type\":\"event\",\"protocol\":\"WTP/1\",\"session_id\":" + json::quote(session_) +
        ",\"boot_id\":" + json::quote(boot_) +
        ",\"event_id\":" + json::quote(std::to_string(*id)) +
        ",\"event\":" + json::quote(name) + ",\"body\":" + body + '}';
    enqueue(std::move(text), now, true);
}
void Endpoint::replace_session(std::uint64_t now) {
    event("SESSION_REPLACED", "{\"error\":" + error_json(ErrorCode::SessionReplaced) + '}', now);
    close_after_output();
}
void Endpoint::observe(std::uint64_t now, bool released) {
    auto s = service_.status();
    if (s.boot_id != boot_) {
        boot_ = s.boot_id;
        event_id_ = 0;
        disconnect();
        observed_ = std::move(s);
        return;
    }
    // Include intermediate terminal transitions (e.g. expired loaded job ->
    // aborted -> empty) which can occur within a single service poll.
    for (auto it = s.terminal_records.rbegin(); it != s.terminal_records.rend(); ++it) {
        if (std::find(observed_.terminal_records.begin(), observed_.terminal_records.end(), *it) !=
            observed_.terminal_records.end())
            continue;
        auto b = state_body(it->job_id, it->state, it->output_active);
        if (it->error != ErrorCode::None) {
            b.pop_back();
            b += ",\"error\":" + error_json(it->error) + '}';
        }
        event("JOB_STATE", b, now);
        if (it->state == State::Missed)
            event("MISSED_START", b, now);
        if (it->state == State::Failed)
            event("DEVICE_FAULT", b, now);
    }
    const bool terminal_current = !s.terminal_records.empty() && s.job_id &&
                                  s.terminal_records.front().job_id == *s.job_id &&
                                  s.terminal_records.front().state == s.state;
    if ((s.state != observed_.state || s.job_id != observed_.job_id) && !terminal_current) {
        if (s.state != State::Failed || s.job_id)
            event("JOB_STATE", state_body(s.job_id, s.state, s.output_active), now);
        if (s.state == State::Failed) {
            auto b = "{\"state\":\"failed\",\"output_active\":" +
                     std::string(s.output_active ? "true" : "false") +
                     ",\"error\":" + error_json(ErrorCode::DeviceFault) + '}';
            event("DEVICE_FAULT", b, now);
        }
    }
    if (observed_.owner_id && !s.owner_id) {
        const char* reason =
            released ? "released"
            : (observed_.state == State::Armed || observed_.state == State::Running)
                ? "terminal"
                : "lease_expired";
        event("OWNER_RELEASED",
              "{\"owner_id\":" + json::quote(*observed_.owner_id) +
                  ",\"reason\":" + json::quote(reason) +
                  ",\"output_active\":" + (s.output_active ? "true" : "false") + '}',
              now);
    }
    observed_ = std::move(s);
}
void Endpoint::poll(std::uint64_t now) {
    service_.poll(); // Always serviced, including disconnected/backpressured states.
    observe(now);
    if (closed_)
        return;
    if (!output_.empty() && now >= last_tx_progress_ms_ && now - last_tx_progress_ms_ >= 5000) {
        disconnect();
        return;
    }
    if (!pending_input_.empty()) {
        if (now >= pending_input_since_ms_ && now - pending_input_since_ms_ >= 5000) {
            close_after_output();
        } else if (output_.empty() && memory_admitted(pending_input_workspace_)) {
            const auto began = pending_input_since_ms_;
            payload(std::move(pending_input_), now);
            // A changed budget cannot restart the original admission deadline.
            if (!pending_input_.empty())
                pending_input_since_ms_ = began;
        }
    }
    if (!closing_)
        frame_events(parser_.check_timeout(now), now);
}
void Endpoint::close_after_output() {
    parser_.end_of_stream();
    pending_input_ = {};
    closing_ = true;
    if (output_.empty())
        closed_ = true;
}
void Endpoint::frame_events(std::vector<FrameEvent> events, std::uint64_t now) {
    for (auto& e : events) {
        if (e.kind == FrameEventKind::Payload)
            payload(std::move(e.payload), now);
        else if (e.kind == FrameEventKind::InvalidFrame)
            event("INVALID_FRAME", "{\"error\":" + error_json(ErrorCode::InvalidFrame) + '}', now);
        else
            close_after_output();
    }
}
std::size_t Endpoint::receive(std::span<const std::uint8_t> input, std::uint64_t now) {
    std::size_t count = 0;
    while (count < std::min<std::size_t>(input.size(), 64) && can_receive()) {
        frame_events(parser_.feed(input.subspan(count, 1), now), now);
        ++count;
    }
    return count;
}
void Endpoint::payload(FrameBuffer bytes, std::uint64_t now) {
    // A different endpoint may briefly own a maximum reply. Retain this one
    // already buffered frame for at most five seconds and apply backpressure;
    // do not decode, dispatch or consume the unchanged safety reserve yet.
    pending_input_workspace_ = 6144;
    if (!memory_admitted(pending_input_workspace_)) {
        pending_input_ = std::move(bytes);
        pending_input_since_ms_ = now;
        return;
    }
    // A complete request can take longer than one RF refill interval. Keep
    // execution progressing between the independently bounded codec stages.
    service_.poll();
    auto root = json::parse(bytes.view());
    service_.poll();
    if (!root) {
        close_after_output();
        return;
    }
    const auto replay_id = service_.active_load_replay_id();
    const bool replay = is_active_load_replay(*root, replay_id);
    pending_input_workspace_ = replay || is_small_read_request(*root) ? 6144 : 32768;
    if (!memory_admitted(pending_input_workspace_)) {
        pending_input_ = std::move(bytes);
        pending_input_since_ms_ = now;
        return;
    }
    auto request =
        decode_request(std::move(*root), principal_, bytes.view(), replay ? replay_id : "");
    // Decoded requests own their fields and digest. Release raw input before
    // preparing a maximum job or serializing its independently bounded reply.
    root.reset();
    bytes = {};
    service_.poll();
    if (!request) {
        close_after_output();
        return;
    }
    Response response;
    if (principal_.empty())
        response.error = ErrorCode::AuthenticationRequired;
    else if (session_.empty() && request->operation != "HELLO")
        response.error = ErrorCode::HelloRequired;
    else if (!session_.empty() && session_ != request->session_id) {
        response.error = ErrorCode::SessionReplaced;
        response.close_connection = true;
        event("SESSION_REPLACED", "{\"error\":" + error_json(ErrorCode::SessionReplaced) + '}',
              now);
    } else
        response = service_.handle(std::move(*request));
    if (response.ok && request->operation == "HELLO")
        session_ = request->session_id;
    request->body = std::monostate{};
    service_.poll();
    if (response.ok && request->operation == "LOAD") {
        auto encoded = LoadResponseStream(*request, response);
        service_.poll();
        enqueue(std::move(encoded), now);
    } else {
        auto encoded =
            encode_response(*request, response, service_.config(), device_id_, firmware_version_,
                            product_);
        service_.poll();
        enqueue(std::move(encoded), now, false);
    }
    observe(now, response.ok && request->operation == "RELEASE");
    if (response.close_connection)
        close_after_output();
}
std::span<const std::uint8_t> Endpoint::output() const {
    if (output_.empty())
        return {};
    const auto& frame = output_.front();
    if (offset_ < frame.header.size())
        return std::span(frame.header).subspan(offset_);
    return frame.bytes(offset_ - frame.header.size());
}
void Endpoint::consume_output(std::size_t count, std::uint64_t now) {
    if (count > output().size()) {
        disconnect();
        return;
    }
    if (count == 0)
        return;
    offset_ += count;
    last_tx_progress_ms_ = now;
    if (offset_ == output_.front().size()) {
        queued_bytes_ -= output_.front().size();
        output_.pop_front();
        offset_ = 0;
        if (output_.empty() && closing_)
            closed_ = true;
    }
}
} // namespace wsprrypico::wtp
