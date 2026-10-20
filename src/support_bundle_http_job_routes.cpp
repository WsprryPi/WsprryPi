#include "support_bundle_http_internal.hpp"

#include "support_bundle_encryption_production.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <utility>

namespace support_bundle_http_internal {
namespace {
std::string trim_ascii(std::string value)
{
    const auto not_space = [](unsigned char character) {
        return character != ' ' && character != '\t' &&
               character != '\r' && character != '\n';
    };
    const auto first = std::find_if(value.begin(), value.end(), not_space);
    const auto last = std::find_if(
        value.rbegin(), value.rend(), not_space).base();
    if (first >= last)
        return {};
    return {first, last};
}

bool is_json_content_type(const httplib::Request &request)
{
    if (!request.has_header("Content-Type"))
        return false;
    const auto lowercase = [](std::string value) {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        return value;
    };

    const std::string content_type =
        request.get_header_value("Content-Type");
    const auto separator = content_type.find(';');
    if (lowercase(trim_ascii(content_type.substr(0, separator))) !=
        "application/json") {
        return false;
    }
    if (separator == std::string::npos)
        return true;

    const std::string parameter =
        trim_ascii(content_type.substr(separator + 1));
    const auto equals = parameter.find('=');
    if (parameter.empty() || parameter.find(';') != std::string::npos ||
        equals == std::string::npos || equals == 0 ||
        equals + 1 == parameter.size()) {
        return false;
    }
    const std::string name =
        lowercase(trim_ascii(parameter.substr(0, equals)));
    const std::string value =
        lowercase(trim_ascii(parameter.substr(equals + 1)));
    return name == "charset" && value == "utf-8";
}
} // namespace

void register_job_mutation_routes(
    httplib::Server &server,
    SupportBundleJobManager &manager,
    RequestGuard guard,
    SupportBundleIntakeProductionProvider intake_provider)
{
    server.Post(
        "/api/support-bundles",
        [&manager, guard](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response))
                return;
            if (request.body.size() > kMaximumRequestBodyBytes) {
                set_error(response, 413, "invalid_request");
                return;
            }
            if (!is_json_content_type(request)) {
                set_error(response, 415, "invalid_request");
                return;
            }

            nlohmann::json body;
            try {
                body = nlohmann::json::parse(request.body);
            } catch (...) {
                set_error(response, 400, "invalid_request");
                return;
            }
            if (!body.is_object()) {
                set_error(response, 400, "invalid_request");
                return;
            }

            bool probe_i2c = false;
            std::optional<SupportBundlePrivateRequest> private_request;
            for (const auto &[key, value] : body.items()) {
                if (key == "probe_i2c" && value.is_boolean()) {
                    probe_i2c = value.get<bool>();
                    continue;
                }
                if (key != "support_context" || !value.is_object() ||
                    private_request) {
                    set_error(response, 400, "invalid_request");
                    return;
                }
                SupportBundleContext context;
                const auto &context_json = value;
                if (!context_json.contains("kind") ||
                    !context_json["kind"].is_string()) {
                    set_error(response, 400, "invalid_request");
                    return;
                }
                const std::string kind =
                    context_json["kind"].get<std::string>();
                if (kind == "existing_github_issue") {
                    context.kind =
                        SupportBundleContextKind::existing_github_issue;
                    if (context_json.size() != 2 ||
                        !context_json.contains("issue_url") ||
                        !context_json["issue_url"].is_string()) {
                        set_error(response, 400, "invalid_request");
                        return;
                    }
                    context.issue_url =
                        context_json["issue_url"].get<std::string>();
                } else if (
                    kind == "new_github_issue" || kind == "no_github") {
                    context.kind = kind == "new_github_issue"
                        ? SupportBundleContextKind::new_github_issue
                        : SupportBundleContextKind::no_github;
                    if (context_json.size() != 3 ||
                        !context_json.contains("problem_description") ||
                        !context_json["problem_description"].is_string() ||
                        !context_json.contains("contact") ||
                        !context_json["contact"].is_string()) {
                        set_error(response, 400, "invalid_request");
                        return;
                    }
                    context.problem_description =
                        context_json["problem_description"].get<std::string>();
                    context.contact =
                        context_json["contact"].get<std::string>();
                } else {
                    set_error(response, 400, "invalid_request");
                    return;
                }
                if (!valid_support_bundle_context(context)) {
                    set_error(response, 400, "invalid_request");
                    return;
                }
                private_request =
                    SupportBundlePrivateRequest{std::move(context)};
            }

            std::string error;
            const auto snapshot = manager.create(
                {probe_i2c, std::move(private_request)}, error);
            if (!snapshot) {
                if (error == "job_active")
                    set_error(response, 409, "job_active");
                else if (error == "storage_unavailable")
                    set_error(response, 503, "unavailable");
                else
                    set_error(response, 500, "unavailable");
                return;
            }
            set_json(response, 202, snapshot_json(*snapshot));
        });

    server.Post(
        R"(/api/support-bundles/(.*)/finalize)",
        [&manager, guard](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response))
                return;
            if (!request.body.empty()) {
                set_error(response, 400, "invalid_request");
                return;
            }
            const std::string id = request.matches.size() > 1
                ? request.matches[1].str() : "";
            if (!SupportBundleJobManager::valid_id(id)) {
                set_error(response, 404, "not_found");
                return;
            }
            const auto outcome = manager.finalize_candidate(id);
            switch (outcome.status) {
            case SupportBundleFinalizationStatus::finalized:
            case SupportBundleFinalizationStatus::already_finalized:
                set_json(response, 200, snapshot_json(*outcome.snapshot));
                return;
            case SupportBundleFinalizationStatus::malformed_or_unknown_id:
                set_error(response, 404, "not_found");
                return;
            case SupportBundleFinalizationStatus::not_private:
                set_error(response, 409, "not_private");
                return;
            case SupportBundleFinalizationStatus::not_ready:
                set_error(response, 409, "not_ready");
                return;
            case SupportBundleFinalizationStatus::download_required:
                set_error(response, 409, "download_required");
                return;
            case SupportBundleFinalizationStatus::artifact_invalid:
                set_error(response, 409, "artifact_invalid");
                return;
            }
            set_error(response, 500, "internal_error");
        });

    server.Post(
        R"(/api/support-bundles/(.*)/encrypt)",
        [&manager, guard, intake_provider](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response))
                return;
            if (!request.body.empty()) {
                set_private_json(
                    response, 400, {{"error", "invalid_request"}});
                return;
            }
            const std::string id = request.matches.size() > 1
                ? request.matches[1].str() : "";
            if (!SupportBundleJobManager::valid_id(id)) {
                set_private_json(response, 404, {{"error", "not_found"}});
                return;
            }
            SupportBundleIntakeProductionResult intake;
            try {
                intake = intake_provider();
            } catch (...) {
                set_private_json(
                    response, 503, {{"error", "intake_unavailable"}});
                return;
            }
            if (!active_intake_authorizes_encryption(intake)) {
                set_private_json(
                    response, 409, {{"error", "intake_not_active"}});
                return;
            }
            const auto outcome = manager.encrypt_candidate(
                id,
                std::string(kSupportBundleProductionKeyId),
                std::string(kSupportBundleProductionAgeRecipient));
            switch (outcome.status) {
            case SupportBundleEncryptionStatus::encrypted:
            case SupportBundleEncryptionStatus::already_encrypted:
                set_private_json(
                    response, 200, {{"workflow_state", "encrypted"}});
                return;
            case SupportBundleEncryptionStatus::malformed_or_unknown_id:
                set_private_json(response, 404, {{"error", "not_found"}});
                return;
            case SupportBundleEncryptionStatus::not_finalized:
                set_private_json(
                    response, 409, {{"error", "not_finalized"}});
                return;
            case SupportBundleEncryptionStatus::key_mismatch:
                set_private_json(
                    response, 409, {{"error", "key_mismatch"}});
                return;
            default:
                set_private_json(
                    response, 503, {{"error", "encryption_failed"}});
                return;
            }
        });
}
} // namespace support_bundle_http_internal
