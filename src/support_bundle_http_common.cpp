#include "support_bundle_http_internal.hpp"

#include "support_bundle_encryption_production.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <unistd.h>

namespace support_bundle_http_internal {
namespace {
constexpr std::size_t kDownloadReadBufferBytes = 64 * 1024;

bool allow_support_request(
    const httplib::Request &request,
    httplib::Response &response,
    const SupportRequestGuardSnapshotProvider &snapshot_provider)
{
    remove_permissive_cors_headers(response);
    const std::optional<std::string> origin = request.has_header("Origin")
        ? std::optional<std::string>(request.get_header_value("Origin"))
        : std::nullopt;
    const SupportRequestGuard guard(snapshot_provider());
    if (!guard.evaluate(
            request.remote_addr,
            request.get_header_value("Host"),
            origin).allowed()) {
        set_error(response, 403, "forbidden");
        return false;
    }
    return true;
}

const char *state_name(SupportBundleJobState state)
{
    switch (state) {
    case SupportBundleJobState::queued:
        return "queued";
    case SupportBundleJobState::running:
        return "running";
    case SupportBundleJobState::succeeded:
        return "succeeded";
    case SupportBundleJobState::failed:
        return "failed";
    }
    return "failed";
}

const char *private_state_name(SupportBundlePrivateLifecycle state)
{
    switch (state) {
    case SupportBundlePrivateLifecycle::collecting:
        return "collecting";
    case SupportBundlePrivateLifecycle::candidate_ready:
        return "candidate_ready";
    case SupportBundlePrivateLifecycle::candidate_downloaded:
        return "candidate_downloaded";
    case SupportBundlePrivateLifecycle::finalized:
        return "finalized";
    case SupportBundlePrivateLifecycle::encrypted_downloaded:
        return "encrypted_downloaded";
    case SupportBundlePrivateLifecycle::upload_page_opened:
        return "upload_page_opened";
    case SupportBundlePrivateLifecycle::upload_reported_complete:
        return "upload_reported_complete";
    case SupportBundlePrivateLifecycle::none:
        break;
    }
    return "";
}
} // namespace

RequestGuard make_request_guard(
    SupportRequestGuardSnapshotProvider snapshot_provider)
{
    return [snapshot_provider = std::move(snapshot_provider)](
               const httplib::Request &request,
               httplib::Response &response) {
        return allow_support_request(request, response, snapshot_provider);
    };
}

void remove_permissive_cors_headers(httplib::Response &response)
{
    response.headers.erase("Access-Control-Allow-Origin");
    response.headers.erase("Access-Control-Allow-Methods");
    response.headers.erase("Access-Control-Allow-Headers");
    response.headers.erase("Access-Control-Allow-Credentials");
}

void set_json(
    httplib::Response &response,
    int status,
    const nlohmann::json &body)
{
    remove_permissive_cors_headers(response);
    response.status = status;
    response.set_content(body.dump(), "application/json");
}

void set_error(httplib::Response &response, int status, const char *error)
{
    set_json(response, status, {{"error", error}});
}

void set_no_content(httplib::Response &response)
{
    remove_permissive_cors_headers(response);
    response.status = 204;
}

void set_private_headers(httplib::Response &response)
{
    response.set_header("Cache-Control", "no-store");
    response.set_header("X-Content-Type-Options", "nosniff");
}

void set_handoff_headers(httplib::Response &response)
{
    set_private_headers(response);
    response.set_header("Referrer-Policy", "no-referrer");
}

void set_private_json(
    httplib::Response &response,
    int status,
    const nlohmann::json &body)
{
    set_json(response, status, body);
    set_private_headers(response);
}

bool active_intake_authorizes_encryption(
    const SupportBundleIntakeProductionResult &result)
{
    return result.status == SupportBundleIntakeProductionStatus::active &&
           result.generation > 0 && !result.expires_at.empty() &&
           !result.minimum_upload_version.empty() &&
           !result.signing_key_id.empty() &&
           result.bundle_key_id == kSupportBundleProductionKeyId &&
           result.request_url && !result.request_url->empty() &&
           result.release_url.empty();
}

nlohmann::json snapshot_json(const SupportBundleJobSnapshot &snapshot)
{
    nlohmann::json output = {
        {"id", snapshot.id},
        {"state", state_name(snapshot.state)},
        {"probe_i2c_requested", snapshot.probe_i2c_requested},
        {"i2c_probe_status", snapshot.i2c_probe_status},
        {"failure_category", snapshot.failure_category},
        {"failure_message", snapshot.failure_message},
        {"download_available", snapshot.download_available},
    };
    if (snapshot.private_lifecycle != SupportBundlePrivateLifecycle::none) {
        output["case_id"] = snapshot.case_id;
        output["workflow_state"] =
            private_state_name(snapshot.private_lifecycle);
    }
    return output;
}

std::string attachment_disposition(const std::string &basename)
{
    std::string value = "attachment; filename=\"";
    for (const char character : basename) {
        if (character == '\\' || character == '"') {
            value.push_back('\\');
        }
        value.push_back(character);
    }
    value.push_back('"');
    return value;
}

void set_download_error(
    httplib::Response &response,
    SupportBundleDownloadPreparationFailure failure)
{
    switch (failure) {
    case SupportBundleDownloadPreparationFailure::unavailable:
        set_error(response, 503, "artifact_unavailable");
        return;
    case SupportBundleDownloadPreparationFailure::unsafe:
        set_error(response, 409, "artifact_unsafe");
        return;
    case SupportBundleDownloadPreparationFailure::corrupt:
        set_error(response, 409, "artifact_corrupt");
        return;
    case SupportBundleDownloadPreparationFailure::internal:
        set_error(response, 500, "internal_error");
        return;
    case SupportBundleDownloadPreparationFailure::none:
        break;
    }
    set_error(response, 500, "internal_error");
}

void set_artifact_download(
    httplib::Response &response,
    SupportBundleDownloadFile file,
    std::string content_type,
    std::function<void(bool, std::uint64_t)> completion)
{
    auto download =
        std::make_shared<SupportBundleDownloadFile>(std::move(file));
    response.status = 200;
    response.set_header(
        "Content-Disposition", attachment_disposition(download->basename()));
    set_private_headers(response);
    response.set_content_provider(
        static_cast<std::size_t>(download->size()),
        content_type,
        [download](
            std::size_t offset,
            std::size_t length,
            httplib::DataSink &sink) {
            if (offset >= download->size() || length == 0)
                return false;
            std::array<char, kDownloadReadBufferBytes> buffer{};
            const auto count = std::min<std::uint64_t>(
                {buffer.size(), length, download->size() - offset});
            const ssize_t read = pread(
                download->descriptor(), buffer.data(), count, offset);
            return read > 0 &&
                   sink.write(buffer.data(), static_cast<std::size_t>(read));
        },
        [download, completion](bool success) {
            if (completion)
                completion(success, download->size());
        });
}
} // namespace support_bundle_http_internal
