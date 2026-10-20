#include "support_bundle_http_internal.hpp"

#include <string>

namespace support_bundle_http_internal {
void register_query_routes(
    httplib::Server &server,
    SupportBundleJobManager &manager,
    RequestGuard guard)
{
    server.Delete(
        R"(/api/support-bundles/(.*))",
        [&manager, guard](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response))
                return;
            const std::string id = request.matches.size() > 1
                ? request.matches[1].str() : "";
            if (!SupportBundleJobManager::valid_id(id)) {
                set_error(response, 404, "not_found");
                return;
            }

            switch (manager.delete_download(id).status) {
            case SupportBundleDownloadDeletionStatus::removed:
            case SupportBundleDownloadDeletionStatus::already_removed:
                set_no_content(response);
                return;
            case SupportBundleDownloadDeletionStatus::malformed_or_unknown_id:
                set_error(response, 404, "not_found");
                return;
            case SupportBundleDownloadDeletionStatus::not_terminal:
                set_error(response, 409, "not_terminal");
                return;
            case SupportBundleDownloadDeletionStatus::no_retained_download:
                set_error(response, 409, "no_download");
                return;
            case SupportBundleDownloadDeletionStatus::cleanup_failed:
                set_error(response, 503, "cleanup_failed");
                return;
            }
            set_error(response, 500, "internal_error");
        });

    server.Get(
        R"(/api/support-bundles/(.*))",
        [&manager, guard](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response))
                return;
            const std::string id = request.matches.size() > 1
                ? request.matches[1].str() : "";
            if (!SupportBundleJobManager::valid_id(id)) {
                set_error(response, 404, "not_found");
                return;
            }
            const auto snapshot = manager.lookup(id);
            if (!snapshot) {
                set_error(response, 404, "not_found");
                return;
            }
            set_json(response, 200, snapshot_json(*snapshot));
        });

    // Prevent a server-wide default CORS policy from turning the collection
    // root into a permissive listing endpoint.
    server.Get(
        "/api/support-bundles",
        [guard](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response))
                return;
            set_error(response, 404, "not_found");
        });

    server.Options(
        "/api/support-bundles",
        [guard](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response))
                return;
            remove_permissive_cors_headers(response);
            response.status = 204;
        });
    server.Options(
        R"(/api/support-bundles/(.*)/download)",
        [guard](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response))
                return;
            remove_permissive_cors_headers(response);
            response.status = 204;
        });
    server.Options(
        R"(/api/support-bundles/(.*))",
        [guard](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response))
                return;
            remove_permissive_cors_headers(response);
            response.set_header("Allow", "GET, DELETE, OPTIONS");
            response.status = 204;
        });
}
} // namespace support_bundle_http_internal
