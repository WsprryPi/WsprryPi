#pragma once
#include "scheduling.hpp"
#include "test_tone_request.hpp"
#include "json.hpp"
nlohmann::json build_test_tone_response(const ParsedTestToneRequest&, const TestToneStartResult&);
void attach_rp1_operation_record(nlohmann::json&, const nlohmann::json&, bool terminal);

nlohmann::json test_tone_status_json(const TestToneStatus &status);
