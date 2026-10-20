#pragma once

#include "support_bundle_http.hpp"

#include "json.hpp"
#include "support_bundle_download_file.hpp"
#include "support_bundle_download_preparation.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace support_bundle_http_internal {
constexpr std::size_t kMaximumRequestBodyBytes = 8192;

using RequestGuard =
    std::function<bool(const httplib::Request &, httplib::Response &)>;

RequestGuard make_request_guard(
    SupportRequestGuardSnapshotProvider snapshot_provider);
void remove_permissive_cors_headers(httplib::Response &response);
void set_json(
    httplib::Response &response, int status, const nlohmann::json &body);
void set_error(httplib::Response &response, int status, const char *error);
void set_no_content(httplib::Response &response);
void set_private_headers(httplib::Response &response);
void set_handoff_headers(httplib::Response &response);
void set_private_json(
    httplib::Response &response, int status, const nlohmann::json &body);
bool active_intake_authorizes_encryption(
    const SupportBundleIntakeProductionResult &result);
nlohmann::json snapshot_json(const SupportBundleJobSnapshot &snapshot);
std::string attachment_disposition(const std::string &basename);
void set_download_error(
    httplib::Response &response,
    SupportBundleDownloadPreparationFailure failure);
void set_artifact_download(
    httplib::Response &response,
    SupportBundleDownloadFile file,
    std::string content_type,
    std::function<void(bool, std::uint64_t)> completion = {});

void register_intake_routes(
    httplib::Server &server,
    SupportBundleJobManager &manager,
    RequestGuard guard,
    SupportBundleIntakeProductionProvider intake_provider);
void register_job_mutation_routes(
    httplib::Server &server,
    SupportBundleJobManager &manager,
    RequestGuard guard,
    SupportBundleIntakeProductionProvider intake_provider);
void register_download_routes(
    httplib::Server &server,
    SupportBundleJobManager &manager,
    RequestGuard guard);
void register_query_routes(
    httplib::Server &server,
    SupportBundleJobManager &manager,
    RequestGuard guard);
} // namespace support_bundle_http_internal
