#include "support_bundle_http.hpp"

#include "support_bundle_http_internal.hpp"

#include <utility>

void register_support_bundle_http_routes(
    httplib::Server &server,
    SupportBundleJobManager &manager,
    SupportRequestGuardSnapshotProvider snapshot_provider,
    SupportBundleIntakeProductionProvider intake_provider)
{
    auto guard = support_bundle_http_internal::make_request_guard(
        std::move(snapshot_provider));
    support_bundle_http_internal::register_intake_routes(
        server, manager, guard, intake_provider);
    support_bundle_http_internal::register_job_mutation_routes(
        server, manager, guard, intake_provider);
    support_bundle_http_internal::register_download_routes(
        server, manager, guard);
    support_bundle_http_internal::register_query_routes(
        server, manager, std::move(guard));
}
