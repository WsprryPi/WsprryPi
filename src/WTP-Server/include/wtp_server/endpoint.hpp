#pragma once
#include "wtp_server/codec.hpp"
#include "wtp_server/frame_parser.hpp"
#include "wtp_server/job_service.hpp"
#include "wtp_server/output_buffer.hpp"

#include <type_traits>
#include <variant>
#include <functional>

namespace wsprrypico::wtp {
// Single-owner, inhibited-engine WTP endpoint. Transport is a byte stream and
// does not own job lifecycle. No device USB calls occur in this class.
class Endpoint {
  public:
    using EventIdSource =
        std::function<std::optional<std::uint64_t>(std::string_view boot_id)>;
    Endpoint(IJobService& service, std::string device_id, std::string firmware_version,
             std::string product = "WsprryPico", EventIdSource event_ids = {});
    void connect(std::string principal);
    void disconnect();
    void poll(std::uint64_t now_ms);
    // At most 64 bytes per call; caller retains all unconsumed input.
    std::size_t receive(std::span<const std::uint8_t> input, std::uint64_t now_ms);
    std::span<const std::uint8_t> output() const;
    void consume_output(std::size_t count, std::uint64_t now_ms);
    bool closed() const {
        return closed_;
    }
    const std::string& session_id() const { return session_; }
    void replace_session(std::uint64_t now_ms);
    bool can_receive() const {
        return !closed_ && !closing_ && output_.empty() && pending_input_.empty();
    }
    // Capacity already reserved for the current input frame. This is a direct
    // endpoint value; unrelated heap allocations and releases cannot change it.
    std::size_t input_reserved_bytes() const {
        return parser_.buffered_capacity() + pending_input_.capacity();
    }

  private:
    void payload(FrameBuffer bytes, std::uint64_t now_ms);
    void frame_events(std::vector<FrameEvent> events, std::uint64_t now_ms);
    bool enqueue(std::string payload, std::uint64_t now_ms, bool advisory);
    bool enqueue(LoadResponseStream payload, std::uint64_t now_ms);
    void event(std::string_view name, std::string body, std::uint64_t now_ms);
    void observe(std::uint64_t now_ms, bool released = false);
    void close_after_output();
    IJobService& service_;
    std::string device_id_, firmware_version_, product_, principal_, session_, boot_;
    EventIdSource event_ids_;
    FrameParser parser_;
    FrameBuffer pending_input_;
    std::uint64_t pending_input_since_ms_ = 0;
    std::size_t pending_input_workspace_ = 6144;
    struct OutputFrame {
        std::array<std::uint8_t, kFrameHeaderBytes> header;
        std::variant<std::string, LoadResponseStream> payload;
        std::span<const std::uint8_t> bytes(std::size_t offset = 0) const {
            return std::visit(
                [offset](const auto& value) -> std::span<const std::uint8_t> {
                    if constexpr (std::is_same_v<std::decay_t<decltype(value)>, LoadResponseStream>)
                        return value.at(offset);
                    else
                        return std::span(reinterpret_cast<const std::uint8_t*>(value.data()),
                                         value.size())
                            .subspan(offset);
                },
                payload);
        }
        std::size_t size() const {
            return header.size() +
                   std::visit([](const auto& value) { return value.size(); }, payload);
        }
    };
    std::deque<OutputFrame> output_;
    std::size_t offset_ = 0, queued_bytes_ = 0;
    std::uint64_t last_tx_progress_ms_ = 0, event_id_ = 0;
    bool closed_ = true, closing_ = false;
    ServiceStatus observed_;
};
} // namespace wsprrypico::wtp
