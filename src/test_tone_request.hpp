#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include "json.hpp"
#include "test_tone_types.hpp"
struct TestToneRequestParseResult {
    std::optional<ParsedTestToneRequest> request;
    std::string error;
    explicit operator bool() const { return request.has_value(); }
};
inline constexpr std::uint64_t MAX_BOUNDED_TONE_DURATION_MS = 60000;
struct ParsedBoundedTestToneRequest {
    ParsedTestToneRequest tone;
    std::string request_id;
    std::uint64_t duration_ms = 0;
};
struct BoundedTestToneRequestParseResult {
    std::optional<ParsedBoundedTestToneRequest> request;
    std::string error;
    explicit operator bool() const { return request.has_value(); }
};
TestToneRequestParseResult parse_test_tone_request(const nlohmann::json &request);
BoundedTestToneRequestParseResult parse_bounded_test_tone_request(
    const nlohmann::json &request);
