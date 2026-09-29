#pragma once
#include "wtp_server/job_service.hpp"
#include "wtp_server/json.hpp"
#include "wtp_server/output_buffer.hpp"

namespace wsprrypico::wtp {
// Invalid envelope has no safely echoable identity and closes without a response.
std::optional<Request> decode_request(json::Value root, std::string_view principal,
                                      InputView payload, std::string_view active_replay_id = {});
// Only a LOAD naming the caller's current active job can omit decoded events.
bool is_active_load_replay(json::Value root, std::string_view active_replay_id);
bool is_small_read_request(json::Value root);
std::string encode_response(const Request& request, const Response& response,
                            const ServiceConfig& config, std::string_view device_id,
                            std::string_view firmware_version,
                            std::string_view product = "WsprryPico");
// A maximum LOAD reply uses the nullable wire-buffer allocator. Empty means
// insufficient working space; the endpoint must close and permit reconciliation.
OutputBuffer encode_load_response_buffer(const Request& request, const Response& response,
                                         bool browser = false);
// Own immutable adjustments and one reusable wire page. Partial transport writes
// and TLS retries retain the same page until every byte in it is consumed.
class LoadResponseStream {
  public:
    LoadResponseStream() = default;
    LoadResponseStream(const Request&, const Response&);
    std::size_t size() const {
        return size_;
    }
    bool empty() const {
        return size_ == 0;
    }
    std::uint32_t checksum() const {
        return checksum_;
    }
    std::span<const std::uint8_t> at(std::size_t offset) const;

  private:
    AdjustmentList adjustments_;
    mutable InputBuffer page_;
    mutable std::string piece_;
    mutable std::size_t page_start_ = 0, piece_offset_ = 0, next_adjustment_ = 0;
    mutable bool closing_ = false;
    std::size_t size_ = 0;
    std::uint32_t checksum_ = 0;
};
std::string error_json(ErrorCode code);
std::string status_json(const ServiceStatus& status);
std::string state_name(State state);
} // namespace wsprrypico::wtp
