#include "support_bundle_intake_validation.hpp"

#include "json.hpp"

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <limits>
#include <memory>
#include <regex>
#include <set>
#include <string_view>

namespace {

using json = nlohmann::json;
constexpr std::size_t kMaximumManifestBytes = 16 * 1024;
constexpr std::size_t kMaximumSignatureEnvelopeBytes = 2048;
constexpr std::int64_t kClockSkewSeconds = 5 * 60;

struct StrictParse {
    bool valid = false;
    json value;
};

StrictParse parse_strict(const std::string &bytes) {
    bool duplicate = false;
    std::vector<std::set<std::string>> object_keys;
    try {
        auto parsed = json::parse(bytes, [&](int, json::parse_event_t event, json &value) {
            if (event == json::parse_event_t::object_start) object_keys.emplace_back();
            if (event == json::parse_event_t::key) {
                if (object_keys.empty() || !object_keys.back().insert(value.get<std::string>()).second)
                    duplicate = true;
            }
            if (event == json::parse_event_t::object_end && !object_keys.empty())
                object_keys.pop_back();
            return true;
        });
        if (!duplicate && parsed.is_object()) return {true, std::move(parsed)};
    } catch (const json::exception &) {
    }
    return {};
}

bool exact_fields(const json &value, const std::set<std::string> &allowed,
                  const std::set<std::string> &required) {
    for (auto it = value.begin(); it != value.end(); ++it)
        if (!allowed.contains(it.key())) return false;
    for (const auto &field : required)
        if (!value.contains(field)) return false;
    return true;
}

std::optional<std::vector<unsigned char>> decode_signature(const std::string &encoded) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    if (encoded.size() != 86 || encoded.find('=') != std::string::npos) return std::nullopt;
    std::uint32_t accumulator = 0;
    unsigned bits = 0;
    std::vector<unsigned char> decoded;
    decoded.reserve(64);
    for (const char character : encoded) {
        const auto position = alphabet.find(character);
        if (position == std::string_view::npos) return std::nullopt;
        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(position);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded.push_back(static_cast<unsigned char>((accumulator >> bits) & 0xff));
        }
    }
    if (bits != 4 || (accumulator & 0x0f) != 0 || decoded.size() != 64) return std::nullopt;
    return decoded;
}

bool verify_ed25519(const std::array<unsigned char, 32> &public_key,
                    const std::vector<unsigned char> &signature,
                    const std::string &message) {
    using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    Key key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                        public_key.data(), public_key.size()), EVP_PKEY_free);
    Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    return key && context &&
           EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) == 1 &&
           EVP_DigestVerify(context.get(), signature.data(), signature.size(),
                            reinterpret_cast<const unsigned char *>(message.data()),
                            message.size()) == 1;
}

std::optional<std::string> sha256(const std::string &bytes) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    if (!SHA256(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), digest.data()))
        return std::nullopt;
    static constexpr char hexadecimal[] = "0123456789abcdef";
    std::string output;
    output.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        output.push_back(hexadecimal[byte >> 4]);
        output.push_back(hexadecimal[byte & 0x0f]);
    }
    return output;
}

std::int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

std::optional<std::int64_t> parse_utc(const std::string &value) {
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':' || value[19] != 'Z') return std::nullopt;
    for (const auto index : {0,1,2,3,5,6,8,9,11,12,14,15,17,18})
        if (!std::isdigit(static_cast<unsigned char>(value[index]))) return std::nullopt;
    const int year = std::stoi(value.substr(0, 4));
    const unsigned month = std::stoul(value.substr(5, 2));
    const unsigned day = std::stoul(value.substr(8, 2));
    const unsigned hour = std::stoul(value.substr(11, 2));
    const unsigned minute = std::stoul(value.substr(14, 2));
    const unsigned second = std::stoul(value.substr(17, 2));
    if (year < 1970 || month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59 || second > 59)
        return std::nullopt;
    static constexpr unsigned lengths[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned maximum_day = lengths[month];
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) ++maximum_day;
    if (day > maximum_day) return std::nullopt;
    return days_from_civil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}

struct SemanticVersion {
    std::array<std::string, 3> core;
    std::vector<std::string> prerelease;
};

bool ascii_digit(unsigned char character) {
    return character >= '0' && character <= '9';
}

bool ascii_alphanumeric(unsigned char character) {
    return ascii_digit(character) ||
           (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z');
}

bool valid_identifier(const std::string &identifier, bool reject_numeric_leading_zero) {
    if (identifier.empty() || !std::all_of(identifier.begin(), identifier.end(),
            [](unsigned char character) { return ascii_alphanumeric(character) || character == '-'; }))
        return false;
    const bool numeric = std::all_of(identifier.begin(), identifier.end(),
        [](unsigned char character) { return ascii_digit(character); });
    return !(reject_numeric_leading_zero && numeric && identifier.size() > 1 && identifier[0] == '0');
}

std::optional<std::vector<std::string>> split_identifiers(
    const std::string &value, bool reject_numeric_leading_zero) {
    std::vector<std::string> identifiers;
    std::size_t offset = 0;
    while (offset <= value.size()) {
        const auto separator = value.find('.', offset);
        const auto identifier = value.substr(
            offset, separator == std::string::npos ? std::string::npos : separator - offset);
        if (!valid_identifier(identifier, reject_numeric_leading_zero)) return std::nullopt;
        identifiers.push_back(identifier);
        if (separator == std::string::npos) break;
        offset = separator + 1;
    }
    return identifiers;
}

std::optional<SemanticVersion> parse_semver(const std::string &value) {
    if (value.empty() || value.size() > 128) return std::nullopt;
    if (!std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return ascii_alphanumeric(character) || character == '.' ||
                   character == '-' || character == '+';
        })) return std::nullopt;
    const auto plus = value.find('+');
    if (plus != std::string::npos && value.find('+', plus + 1) != std::string::npos)
        return std::nullopt;
    const auto before_build = value.substr(0, plus);
    const auto dash = before_build.find('-');
    const auto core_text = before_build.substr(0, dash);
    auto core = split_identifiers(core_text, true);
    if (!core || core->size() != 3) return std::nullopt;
    if (!std::all_of(core->begin(), core->end(), [](const std::string &identifier) {
            return std::all_of(identifier.begin(), identifier.end(),
                [](unsigned char character) { return ascii_digit(character); });
        })) return std::nullopt;
    SemanticVersion parsed;
    std::copy(core->begin(), core->end(), parsed.core.begin());
    if (dash != std::string::npos) {
        auto prerelease = split_identifiers(before_build.substr(dash + 1), true);
        if (!prerelease) return std::nullopt;
        parsed.prerelease = std::move(*prerelease);
    }
    if (plus != std::string::npos) {
        if (!split_identifiers(value.substr(plus + 1), false)) return std::nullopt;
    }
    return parsed;
}

int compare_numeric(const std::string &left, const std::string &right) {
    if (left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
    if (left == right) return 0;
    return left < right ? -1 : 1;
}

int compare_semver(const SemanticVersion &left, const SemanticVersion &right) {
    for (std::size_t index = 0; index < left.core.size(); ++index) {
        const auto compared = compare_numeric(left.core[index], right.core[index]);
        if (compared != 0) return compared;
    }
    if (left.prerelease.empty() || right.prerelease.empty()) {
        if (left.prerelease.empty() == right.prerelease.empty()) return 0;
        return left.prerelease.empty() ? 1 : -1;
    }
    const auto count = std::min(left.prerelease.size(), right.prerelease.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto &a = left.prerelease[index];
        const auto &b = right.prerelease[index];
        const bool a_numeric = std::all_of(a.begin(), a.end(),
            [](unsigned char character) { return ascii_digit(character); });
        const bool b_numeric = std::all_of(b.begin(), b.end(),
            [](unsigned char character) { return ascii_digit(character); });
        if (a_numeric && b_numeric) {
            const auto compared = compare_numeric(a, b);
            if (compared != 0) return compared;
        } else if (a_numeric != b_numeric) {
            return a_numeric ? -1 : 1;
        } else if (a != b) {
            return a < b ? -1 : 1;
        }
    }
    if (left.prerelease.size() == right.prerelease.size()) return 0;
    return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
}

bool valid_key_id(const std::string &value, const char *purpose) {
    return std::regex_match(value, std::regex(
        std::string("^wsprrypi-") + purpose + "-[0-9]{4}-[0-9]{2}$"));
}

bool valid_dropbox_request_url(const std::string &url) {
    constexpr std::string_view prefix = "https://www.dropbox.com/request/";
    if (!url.starts_with(prefix) || url.size() == prefix.size()) return false;
    return url.find_first_of("/?#@", prefix.size()) == std::string::npos &&
           std::all_of(url.begin() + static_cast<std::ptrdiff_t>(prefix.size()), url.end(),
                       [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

bool valid_release_url(const std::string &url) {
    constexpr std::string_view prefix = "https://github.com/";
    return url.starts_with(prefix) && url.size() > prefix.size() &&
           url.find_first_of("?#@", prefix.size()) == std::string::npos &&
           std::none_of(url.begin(), url.end(), [](unsigned char c) { return c <= 0x20 || c == 0x7f; });
}

template <typename Type>
bool get_exact(const json &value, const char *field, Type &output) {
    try {
        output = value.at(field).get<Type>();
        return true;
    } catch (const json::exception &) {
        return false;
    }
}

bool get_unsigned(const json &value, const char *field, std::uint64_t &output) {
    if (!value.contains(field) || !value.at(field).is_number_unsigned()) return false;
    output = value.at(field).get<std::uint64_t>();
    return true;
}

SupportBundleIntakeValidationResult failed(SupportBundleIntakeFailure failure) {
    SupportBundleIntakeValidationResult result;
    result.failure = failure;
    return result;
}

SupportBundleIntakeValidationResult upgrade_required(
    const SupportBundleIntakeManifest &manifest) {
    SupportBundleIntakeValidationResult result;
    result.failure = SupportBundleIntakeFailure::upgrade_required;
    result.upgrade = SupportBundleIntakeValidationResult::UpgradeRequirement{
        manifest.minimum_upload_version, manifest.release_url, manifest.user_message};
    result.accepted_state = SupportBundleIntakePreviousState{
        manifest.generation, manifest.manifest_sha256};
    return result;
}

} // namespace

bool valid_support_bundle_semver(const std::string &value) noexcept {
    try {
        return parse_semver(value).has_value();
    } catch (...) {
        return false;
    }
}

SupportBundleIntakeValidationResult validate_support_bundle_intake(
    const SupportBundleIntakeValidationRequest &request) {
    if (request.manifest_bytes.empty() || request.manifest_bytes.size() > kMaximumManifestBytes)
        return failed(SupportBundleIntakeFailure::manifest_oversized);
    if (request.signature_envelope_bytes.empty() ||
        request.signature_envelope_bytes.size() > kMaximumSignatureEnvelopeBytes)
        return failed(SupportBundleIntakeFailure::signature_envelope_oversized);

    const auto envelope = parse_strict(request.signature_envelope_bytes);
    const std::set<std::string> envelope_fields = {"schema_version", "algorithm", "key_id", "signature"};
    if (!envelope.valid || !exact_fields(envelope.value, envelope_fields, envelope_fields))
        return failed(SupportBundleIntakeFailure::invalid_signature_envelope);
    std::uint64_t envelope_schema = 0;
    std::string algorithm, key_id, encoded_signature;
    if (!get_unsigned(envelope.value, "schema_version", envelope_schema) || envelope_schema != 1 ||
        !get_exact(envelope.value, "algorithm", algorithm) || algorithm != "Ed25519" ||
        !get_exact(envelope.value, "key_id", key_id) || !valid_key_id(key_id, "intake") ||
        !get_exact(envelope.value, "signature", encoded_signature))
        return failed(SupportBundleIntakeFailure::invalid_signature_envelope);
    const auto signature = decode_signature(encoded_signature);
    if (!signature) return failed(SupportBundleIntakeFailure::invalid_signature_envelope);
    const auto signing_key = std::find_if(request.signing_keys.begin(), request.signing_keys.end(),
        [&](const auto &candidate) { return candidate.key_id == key_id; });
    if (signing_key == request.signing_keys.end())
        return failed(SupportBundleIntakeFailure::unknown_signing_key);
    if (!verify_ed25519(signing_key->public_key, *signature, request.manifest_bytes))
        return failed(SupportBundleIntakeFailure::invalid_signature);

    const auto digest = sha256(request.manifest_bytes);
    if (!digest) return failed(SupportBundleIntakeFailure::invalid_manifest);
    const auto parsed = parse_strict(request.manifest_bytes);
    const std::set<std::string> allowed = {
        "schema_version", "project_id", "generation", "published_at", "expires_at", "status",
        "minimum_client_protocol", "minimum_upload_version", "request_url", "release_url",
        "user_message", "bundle_encryption_key_id"};
    const std::set<std::string> required = {
        "schema_version", "project_id", "generation", "published_at", "expires_at", "status",
        "minimum_client_protocol", "minimum_upload_version", "release_url", "user_message",
        "bundle_encryption_key_id"};
    if (!parsed.valid || !exact_fields(parsed.value, allowed, required))
        return failed(SupportBundleIntakeFailure::invalid_manifest);

    std::uint64_t schema = 0;
    std::string project;
    SupportBundleIntakeManifest manifest;
    if (!get_unsigned(parsed.value, "schema_version", schema) || schema != 1 ||
        !get_exact(parsed.value, "project_id", project) ||
        !get_unsigned(parsed.value, "generation", manifest.generation) ||
        !get_exact(parsed.value, "published_at", manifest.published_at) ||
        !get_exact(parsed.value, "expires_at", manifest.expires_at) ||
        !get_exact(parsed.value, "status", manifest.status) ||
        !get_unsigned(parsed.value, "minimum_client_protocol", manifest.minimum_client_protocol) ||
        !get_exact(parsed.value, "minimum_upload_version", manifest.minimum_upload_version) ||
        !get_exact(parsed.value, "release_url", manifest.release_url) ||
        !get_exact(parsed.value, "bundle_encryption_key_id", manifest.bundle_encryption_key_id))
        return failed(SupportBundleIntakeFailure::invalid_manifest);
    if (project != "wsprrypi") return failed(SupportBundleIntakeFailure::wrong_project);
    if (manifest.generation == 0)
        return failed(SupportBundleIntakeFailure::invalid_generation);
    if (manifest.minimum_client_protocol == 0)
        return failed(SupportBundleIntakeFailure::invalid_manifest);
    if (!parsed.value.at("user_message").is_null()) {
        std::string message;
        if (!get_exact(parsed.value, "user_message", message) || message.size() > 1024)
            return failed(SupportBundleIntakeFailure::invalid_manifest);
        manifest.user_message = std::move(message);
    }
    if (parsed.value.contains("request_url")) {
        std::string url;
        if (!get_exact(parsed.value, "request_url", url))
            return failed(SupportBundleIntakeFailure::invalid_manifest);
        manifest.request_url = std::move(url);
    }

    if (request.previous) {
        if (manifest.generation < request.previous->generation)
            return failed(SupportBundleIntakeFailure::rollback);
        if (manifest.generation == request.previous->generation && *digest != request.previous->manifest_sha256)
            return failed(SupportBundleIntakeFailure::same_generation_mutated);
    }
    const auto published = parse_utc(manifest.published_at);
    const auto expires = parse_utc(manifest.expires_at);
    if (!published || !expires || *published >= *expires)
        return failed(SupportBundleIntakeFailure::invalid_time);
    if (request.now_utc_seconds < *published - kClockSkewSeconds ||
        request.now_utc_seconds > *expires + kClockSkewSeconds)
        return failed(SupportBundleIntakeFailure::outside_validity_window);
    const auto minimum_version = parse_semver(manifest.minimum_upload_version);
    if (!minimum_version)
        return failed(SupportBundleIntakeFailure::invalid_manifest);
    if (manifest.minimum_client_protocol > request.client_protocol)
        return failed(SupportBundleIntakeFailure::incompatible_client);
    if (manifest.status == "active") {
        if (!manifest.request_url || !valid_dropbox_request_url(*manifest.request_url))
            return failed(SupportBundleIntakeFailure::invalid_request_url);
    } else if (manifest.status == "disabled") {
        if (manifest.request_url) return failed(SupportBundleIntakeFailure::invalid_request_url);
    } else {
        return failed(SupportBundleIntakeFailure::unsupported_status);
    }
    if (!valid_release_url(manifest.release_url))
        return failed(SupportBundleIntakeFailure::invalid_release_url);
    if (!valid_key_id(manifest.bundle_encryption_key_id, "bundle") ||
        std::find(request.recognized_bundle_key_ids.begin(), request.recognized_bundle_key_ids.end(),
                  manifest.bundle_encryption_key_id) == request.recognized_bundle_key_ids.end())
        return failed(SupportBundleIntakeFailure::unknown_bundle_key);
    manifest.manifest_sha256 = *digest;
    manifest.signing_key_id = key_id;
    const auto installed_version = parse_semver(request.installed_upload_version);
    if (!installed_version)
        return failed(SupportBundleIntakeFailure::invalid_client_version);
    if (manifest.status == "active" &&
        compare_semver(*installed_version, *minimum_version) < 0)
        return upgrade_required(manifest);

    SupportBundleIntakeValidationResult result;
    result.failure = SupportBundleIntakeFailure::none;
    result.manifest = std::move(manifest);
    return result;
}
