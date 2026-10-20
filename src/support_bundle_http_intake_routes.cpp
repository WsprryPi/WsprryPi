#include "support_bundle_http_internal.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace support_bundle_http_internal {
namespace {
bool valid_dropbox_handoff_url(const std::string &url)
{
    constexpr std::string_view prefix = "https://www.dropbox.com/request/";
    if (!url.starts_with(prefix) || url.size() == prefix.size())
        return false;
    return url.find_first_of("/?#@", prefix.size()) == std::string::npos &&
           std::all_of(
               url.begin() + static_cast<std::ptrdiff_t>(prefix.size()),
               url.end(),
               [](unsigned char character) {
                   return std::isalnum(character) || character == '-' ||
                          character == '_';
               });
}

bool empty_identity(const SupportBundleIntakeProductionResult &result)
{
    return result.generation == 0 && result.expires_at.empty() &&
           result.minimum_upload_version.empty() &&
           result.signing_key_id.empty() && result.bundle_key_id.empty() &&
           !result.request_url && result.release_url.empty() &&
           !result.user_message;
}

void set_intake_unavailable(httplib::Response &response)
{
    set_private_json(response, 503, {{"status", "unavailable"}});
}

void set_intake_result(
    httplib::Response &response,
    const SupportBundleIntakeProductionResult &result)
{
    nlohmann::json body;
    switch (result.status) {
    case SupportBundleIntakeProductionStatus::active:
        if (result.generation == 0 || result.expires_at.empty() ||
            result.minimum_upload_version.empty() ||
            result.signing_key_id.empty() || result.bundle_key_id.empty() ||
            !result.request_url || result.request_url->empty() ||
            !result.release_url.empty()) {
            set_intake_unavailable(response);
            return;
        }
        body = {
            {"status", "active"},
            {"generation", result.generation},
            {"expires_at", result.expires_at},
            {"minimum_upload_version", result.minimum_upload_version},
            {"signing_key_id", result.signing_key_id},
            {"bundle_key_id", result.bundle_key_id},
            {"request_url", *result.request_url}};
        break;
    case SupportBundleIntakeProductionStatus::disabled:
        if (result.generation == 0 || result.expires_at.empty() ||
            result.signing_key_id.empty() || result.bundle_key_id.empty() ||
            !result.minimum_upload_version.empty() || result.request_url ||
            !result.release_url.empty()) {
            set_intake_unavailable(response);
            return;
        }
        body = {
            {"status", "disabled"},
            {"generation", result.generation},
            {"expires_at", result.expires_at},
            {"signing_key_id", result.signing_key_id},
            {"bundle_key_id", result.bundle_key_id}};
        break;
    case SupportBundleIntakeProductionStatus::upgrade_required:
        if (result.minimum_upload_version.empty() ||
            result.release_url.empty() || result.generation != 0 ||
            !result.expires_at.empty() || !result.signing_key_id.empty() ||
            !result.bundle_key_id.empty() || result.request_url) {
            set_intake_unavailable(response);
            return;
        }
        body = {
            {"status", "upgrade_required"},
            {"minimum_upload_version", result.minimum_upload_version},
            {"release_url", result.release_url}};
        break;
    case SupportBundleIntakeProductionStatus::unavailable:
        if (!empty_identity(result)) {
            set_intake_unavailable(response);
            return;
        }
        set_intake_unavailable(response);
        return;
    }
    if (result.user_message)
        body["user_message"] = *result.user_message;
    set_private_json(response, 200, body);
}
} // namespace

void register_intake_routes(
    httplib::Server &server,
    SupportBundleJobManager &manager,
    RequestGuard guard,
    SupportBundleIntakeProductionProvider intake_provider)
{
    server.Get(
        "/api/support-intake",
        [guard, intake_provider](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response)) {
                set_private_headers(response);
                return;
            }
            if (!request.body.empty()) {
                set_error(response, 400, "invalid_request");
                set_private_headers(response);
                return;
            }
            try {
                if (!intake_provider) {
                    set_intake_unavailable(response);
                    return;
                }
                set_intake_result(response, intake_provider());
            } catch (...) {
                set_intake_unavailable(response);
            }
        });

    server.Options(
        "/api/support-intake",
        [guard](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response)) {
                set_private_headers(response);
                return;
            }
            set_no_content(response);
            set_private_headers(response);
        });

    server.Get(
        R"(/api/support-bundles/(.*)/handoff)",
        [&manager, guard, intake_provider](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response)) {
                set_handoff_headers(response);
                return;
            }
            if (!request.body.empty()) {
                set_private_json(
                    response, 400, {{"error", "invalid_request"}});
                set_handoff_headers(response);
                return;
            }
            const std::string id = request.matches.size() > 1
                ? request.matches[1].str() : "";
            if (!SupportBundleJobManager::valid_id(id)) {
                set_private_json(response, 404, {{"error", "not_found"}});
                set_handoff_headers(response);
                return;
            }
            if (manager.receipt_reference(id).status !=
                SupportBundleDownloadReferenceStatus::available) {
                set_private_json(
                    response,
                    409,
                    {{"error", "encrypted_download_required"}});
                set_handoff_headers(response);
                return;
            }
            try {
                const auto result = intake_provider();
                if (!active_intake_authorizes_encryption(result) ||
                    !valid_dropbox_handoff_url(*result.request_url)) {
                    set_private_json(
                        response, 409, {{"error", "intake_not_active"}});
                    set_handoff_headers(response);
                    return;
                }
                const auto transition = manager.mark_upload_page_opened(id);
                if (transition !=
                        SupportBundleUploadTransitionStatus::transitioned &&
                    transition != SupportBundleUploadTransitionStatus::
                        already_transitioned) {
                    set_private_json(
                        response,
                        409,
                        {{"error", "encrypted_download_required"}});
                    set_handoff_headers(response);
                    return;
                }
                remove_permissive_cors_headers(response);
                response.set_redirect(*result.request_url, 302);
                set_handoff_headers(response);
            } catch (...) {
                set_private_json(
                    response, 503, {{"error", "intake_unavailable"}});
                set_handoff_headers(response);
            }
        });

    server.Post(
        R"(/api/support-bundles/(.*)/upload-reported-complete)",
        [&manager, guard](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response)) {
                set_private_headers(response);
                return;
            }
            if (!request.body.empty()) {
                set_private_json(
                    response, 400, {{"error", "invalid_request"}});
                return;
            }
            const std::string id = request.matches.size() > 1
                ? request.matches[1].str() : "";
            const auto status = manager.report_upload_complete(id);
            switch (status) {
            case SupportBundleUploadTransitionStatus::transitioned:
            case SupportBundleUploadTransitionStatus::already_transitioned:
                set_private_json(
                    response,
                    200,
                    {{"workflow_state", "upload_reported_complete"}});
                return;
            case SupportBundleUploadTransitionStatus::malformed_or_unknown_id:
                set_private_json(response, 404, {{"error", "not_found"}});
                return;
            case SupportBundleUploadTransitionStatus::unavailable:
                set_private_json(
                    response, 409, {{"error", "upload_page_not_opened"}});
                return;
            }
            set_private_json(response, 500, {{"error", "internal_error"}});
        });
}
} // namespace support_bundle_http_internal
