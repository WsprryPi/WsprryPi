#include "support_bundle_archive_digest.hpp"

#include <cerrno>
#include <fcntl.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace {
constexpr std::size_t kDigestReadBufferBytes = 64 * 1024;

using DigestContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

SupportBundleArchiveDigestValidation failure(SupportBundleArchiveDigestFailure category) {
    return {category};
}

bool is_lowercase_hex_digest(const std::string &digest) {
    if (digest.size() != 64) {
        return false;
    }
    for (const unsigned char character : digest) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

SupportBundleArchiveDigestValidation rewind_or(
    SupportBundleDownloadFile &archive,
    SupportBundleArchiveDigestFailure category) {
    if (lseek(archive.descriptor(), 0, SEEK_SET) == static_cast<off_t>(-1)) {
        return failure(SupportBundleArchiveDigestFailure::seek_failed);
    }
    return failure(category);
}
}  // namespace

SupportBundleArchiveDigestValidation verify_support_bundle_archive_digest(
    SupportBundleDownloadFile &archive,
    const std::string &expected_sha256) {
    if (!archive.valid() || archive.size() == 0) {
        return failure(SupportBundleArchiveDigestFailure::invalid_file);
    }
    if (lseek(archive.descriptor(), 0, SEEK_SET) == static_cast<off_t>(-1)) {
        return failure(SupportBundleArchiveDigestFailure::seek_failed);
    }
    if (!is_lowercase_hex_digest(expected_sha256)) {
        return failure(SupportBundleArchiveDigestFailure::invalid_expected_digest);
    }

    struct stat initial_info {};
    if (fstat(archive.descriptor(), &initial_info) != 0) {
        return rewind_or(archive, SupportBundleArchiveDigestFailure::read_failed);
    }
    if (!S_ISREG(initial_info.st_mode) || initial_info.st_size < 0 ||
        static_cast<std::uint64_t>(initial_info.st_size) != archive.size()) {
        return rewind_or(archive, SupportBundleArchiveDigestFailure::size_mismatch);
    }

    DigestContext context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        return rewind_or(archive, SupportBundleArchiveDigestFailure::digest_failed);
    }

    std::array<unsigned char, kDigestReadBufferBytes> buffer {};
    std::uint64_t remaining = archive.size();
    while (remaining > 0) {
        const std::size_t request_size =
            std::min<std::uint64_t>(buffer.size(), remaining);
        const ssize_t read_count = read(archive.descriptor(), buffer.data(), request_size);
        if (read_count > 0) {
            if (EVP_DigestUpdate(context.get(), buffer.data(),
                                 static_cast<std::size_t>(read_count)) != 1) {
                return rewind_or(archive, SupportBundleArchiveDigestFailure::digest_failed);
            }
            remaining -= static_cast<std::uint64_t>(read_count);
            continue;
        }
        if (read_count < 0 && errno == EINTR) {
            continue;
        }
        return rewind_or(archive, read_count == 0
                                      ? SupportBundleArchiveDigestFailure::truncated
                                      : SupportBundleArchiveDigestFailure::read_failed);
    }

    unsigned char extra_byte = 0;
    while (true) {
        const ssize_t read_count = read(archive.descriptor(), &extra_byte, 1);
        if (read_count == 0) {
            break;
        }
        if (read_count > 0) {
            return rewind_or(archive, SupportBundleArchiveDigestFailure::size_mismatch);
        }
        if (errno != EINTR) {
            return rewind_or(archive, SupportBundleArchiveDigestFailure::read_failed);
        }
    }

    struct stat final_info {};
    if (fstat(archive.descriptor(), &final_info) != 0) {
        return rewind_or(archive, SupportBundleArchiveDigestFailure::read_failed);
    }
    if (!S_ISREG(final_info.st_mode) || final_info.st_size < 0 ||
        static_cast<std::uint64_t>(final_info.st_size) != archive.size()) {
        return rewind_or(archive, SupportBundleArchiveDigestFailure::size_mismatch);
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest {};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &digest_size) != 1 ||
        digest_size != 32) {
        return rewind_or(archive, SupportBundleArchiveDigestFailure::digest_failed);
    }

    constexpr char kHexDigits[] = "0123456789abcdef";
    std::array<char, 64> calculated_hex {};
    for (std::size_t index = 0; index < digest_size; ++index) {
        calculated_hex[index * 2] = kHexDigits[digest[index] >> 4];
        calculated_hex[index * 2 + 1] = kHexDigits[digest[index] & 0x0f];
    }
    if (CRYPTO_memcmp(calculated_hex.data(), expected_sha256.data(),
                      calculated_hex.size()) != 0) {
        return rewind_or(archive, SupportBundleArchiveDigestFailure::digest_mismatch);
    }

    return rewind_or(archive, SupportBundleArchiveDigestFailure::none);
}
