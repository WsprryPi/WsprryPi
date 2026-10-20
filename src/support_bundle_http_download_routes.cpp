#include "support_bundle_http_internal.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <memory>
#include <string>
#include <utility>
#include <unistd.h>

namespace support_bundle_http_internal {
namespace {
constexpr std::size_t kDownloadReadBufferBytes = 64 * 1024;
} // namespace

void register_download_routes(
    httplib::Server &server,
    SupportBundleJobManager &manager,
    RequestGuard guard)
{
    server.Get(
        R"(/api/support-bundles/(.*)/encrypted)",
        [&manager, guard](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response)) {
                set_private_headers(response);
                return;
            }
            const std::string id = request.matches.size() > 1
                ? request.matches[1].str() : "";
            auto opened = open_support_bundle_download_file(
                manager.encrypted_reference(id));
            if (!opened.available()) {
                set_private_json(response, 409, {{"error", "not_ready"}});
                return;
            }
            set_artifact_download(
                response,
                std::move(opened.file),
                "application/octet-stream",
                [&manager, id](bool success, std::uint64_t size) {
                    if (success)
                        (void)manager.mark_encrypted_downloaded(id, size);
                });
        });

    server.Get(
        R"(/api/support-bundles/(.*)/receipt)",
        [&manager, guard](
            const httplib::Request &request,
            httplib::Response &response) {
            if (!guard(request, response)) {
                set_private_headers(response);
                return;
            }
            const std::string id = request.matches.size() > 1
                ? request.matches[1].str() : "";
            auto opened = open_support_bundle_download_file(
                manager.receipt_reference(id), 16 * 1024);
            if (!opened.available()) {
                set_private_json(response, 409, {{"error", "not_ready"}});
                return;
            }
            set_artifact_download(
                response, std::move(opened.file), "application/json");
        });

    server.Get(
        R"(/api/support-bundles/(.*)/download)",
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

            const auto reference = manager.download_reference(id);
            switch (reference.status) {
            case SupportBundleDownloadReferenceStatus::
                malformed_or_unknown_id:
                set_error(response, 404, "not_found");
                return;
            case SupportBundleDownloadReferenceStatus::not_ready:
                set_error(response, 409, "not_ready");
                return;
            case SupportBundleDownloadReferenceStatus::no_download:
                set_error(response, 409, "no_download");
                return;
            case SupportBundleDownloadReferenceStatus::available:
                break;
            }

            auto prepared = prepare_support_bundle_download(reference);
            if (!prepared.available()) {
                set_download_error(response, prepared.failure);
                return;
            }

            auto download = std::make_shared<SupportBundlePreparedDownload>(
                std::move(prepared.download));
            response.status = 200;
            response.set_header(
                "Content-Disposition",
                attachment_disposition(download->basename()));
            response.set_header("Cache-Control", "no-store");
            response.set_header("X-Content-Type-Options", "nosniff");
            response.set_content_provider(
                static_cast<std::size_t>(download->size()),
                "application/gzip",
                [download](
                    std::size_t offset,
                    std::size_t length,
                    httplib::DataSink &sink) {
                    if (offset >= download->size() || length == 0)
                        return false;

                    const std::uint64_t remaining =
                        download->size() - offset;
                    const std::size_t read_size =
                        static_cast<std::size_t>(std::min<std::uint64_t>(
                            {kDownloadReadBufferBytes, length, remaining}));
                    std::array<char, kDownloadReadBufferBytes> buffer{};
                    ssize_t read_count = 0;
                    do {
                        read_count = pread(
                            download->descriptor(),
                            buffer.data(),
                            read_size,
                            static_cast<off_t>(offset));
                    } while (read_count < 0 && errno == EINTR);

                    return read_count > 0 && sink.write(
                        buffer.data(), static_cast<std::size_t>(read_count));
                },
                [&manager, id, download](bool success) {
                    if (success)
                        (void)manager.mark_candidate_downloaded(
                            id, download->size());
                });
        });
}
} // namespace support_bundle_http_internal
