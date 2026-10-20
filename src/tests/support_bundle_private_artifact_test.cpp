#include "support_bundle_private_artifact.hpp"
#include "json.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
void write_private(const fs::path &path, const std::string &contents) {
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0);
    assert(write(fd, contents.data(), contents.size()) ==
           static_cast<ssize_t>(contents.size()));
    assert(close(fd) == 0);
}

std::string read_all(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

int helper(int argc, char **argv) {
    assert(argc == 7);
    assert(std::string(argv[1]) == "--encrypt");
    assert(std::string(argv[2]) == "--recipient");
    assert(std::string(argv[4]) == "--output");
    const std::string recipient = argv[3];
    if (recipient == "age1test-exit") return 23;
    if (recipient == "age1test-sleep") {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return 0;
    }
    std::ifstream input(argv[6], std::ios::binary);
    std::string plaintext{std::istreambuf_iterator<char>(input), {}};
    const int fd = open(argv[5], O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) return 24;
    if (recipient == "age1test-mode" && fchmod(fd, 0644) != 0) return 26;
    std::string output;
    if (recipient == "age1test-empty") output = "";
    else if (recipient == "age1test-large") output.assign(4096, 'x');
    else output = "AGE-TEST:" + plaintext;
    const bool ok = output.empty() || write(fd, output.data(), output.size()) ==
                                      static_cast<ssize_t>(output.size());
    return close(fd) == 0 && ok ? 0 : 25;
}

SupportBundleEncryptionRequest request_for(const FinalizedSupportBundle &bundle,
                                           const fs::path &root,
                                           const fs::path &executable,
                                           std::string artifact,
                                           std::string recipient = "age1test-recipient") {
    return {&bundle, root, "7K3M-9QFX-2DPA", std::move(artifact),
            std::move(recipient), "wsprrypi-bundle-2026-01", executable,
            1024, std::chrono::milliseconds(500)};
}
}  // namespace

int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "--encrypt") return helper(argc, argv);

    const auto all_zero = [](unsigned char *bytes, std::size_t size) {
        std::fill(bytes, bytes + size, 0); return true;
    };
    const auto all_ff = [](unsigned char *bytes, std::size_t size) {
        std::fill(bytes, bytes + size, 0xff); return true;
    };
    const auto fail_entropy = [](unsigned char *, std::size_t) { return false; };
    assert(generate_support_bundle_case_id(all_zero) == "0000-0000-0000");
    assert(generate_support_bundle_case_id(all_ff) == "ZZZZ-ZZZZ-ZZZZ");
    assert(!generate_support_bundle_case_id(fail_entropy));
    assert(generate_support_bundle_artifact_id(all_zero) ==
           "00000000000000000000000000000000");
    assert(generate_support_bundle_artifact_id(all_ff) ==
           "ffffffffffffffffffffffffffffffff");
    assert(valid_support_bundle_case_id("7K3M-9QFX-2DPA"));
    assert(!valid_support_bundle_case_id("7K3M-9QFU-2DPA"));
    assert(!valid_support_bundle_case_id("7k3m-9QFX-2DPA"));
    assert(valid_support_bundle_artifact_id("0123456789abcdef0123456789abcdef"));
    assert(!valid_support_bundle_artifact_id("0123456789ABCDEF0123456789ABCDEF"));

    assert(valid_support_bundle_context({SupportBundleContextKind::existing_github_issue,
        "https://github.com/WsprryPi/WsprryPi/issues/352", "", ""}));
    assert(!valid_support_bundle_context({SupportBundleContextKind::existing_github_issue,
        "https://github.com/WsprryPi/WsprryPi/issues/0", "", ""}));
    assert(!valid_support_bundle_context({SupportBundleContextKind::existing_github_issue,
        "https://github.com/other/project/issues/352", "", ""}));
    assert(valid_support_bundle_context({SupportBundleContextKind::no_github, "",
        "The transmitter stops after scheduling.", "operator@example.test"}));
    assert(!valid_support_bundle_context({SupportBundleContextKind::new_github_issue, "",
        "   ", "operator@example.test"}));
    assert(!valid_support_bundle_context({SupportBundleContextKind::no_github, "",
        "Description\nwith control", "operator@example.test"}));
    assert(!valid_support_bundle_context({static_cast<SupportBundleContextKind>(99), "",
        "Description", "operator@example.test"}));

    char root_template[] = "/tmp/wsprrypi-private-artifact-test.XXXXXX";
    assert(mkdtemp(root_template));
    const fs::path root(root_template);
    assert(chmod(root.c_str(), 0700) == 0);
    const fs::path executable = root / "fake-age";
    assert(fs::copy_file(fs::canonical(argv[0]), executable));
    assert(chmod(executable.c_str(), 0500) == 0);

    const fs::path wrong = root / "wrong.tar.gz";
    write_private(wrong, "readable-bundle");
    auto wrong_digest = finalize_support_bundle(wrong, std::string(64, '0'), 1024);
    assert(wrong_digest.failure == SupportBundleFinalizationFailure::digest_mismatch);
    assert((fs::status(wrong).permissions() & fs::perms::owner_write) != fs::perms::none);

    const fs::path archive = root / "WsprryPi-support-test.tar.gz";
    write_private(archive, "readable-bundle");
    constexpr const char *digest =
        "302392d7f5a88795cb87a3adb1f3fb66cfece9db052e8dc5a5431308a9d964a7";
    auto finalized = finalize_support_bundle(archive, digest, 1024);
    assert(finalized.finalized());
    assert(finalized.bundle.size() == 15);
    assert((fs::status(archive).permissions() & fs::perms::owner_write) == fs::perms::none);
    assert(!finalize_support_bundle(archive, digest, 1024).finalized());
    const fs::path link = root / "linked.tar.gz";
    assert(symlink(archive.c_str(), link.c_str()) == 0);
    assert(!finalize_support_bundle(link, digest, 1024).finalized());

    const fs::path changed_archive = root / "WsprryPi-support-changed.tar.gz";
    write_private(changed_archive, "readable-bundle");
    auto changed = finalize_support_bundle(changed_archive, digest, 1024);
    assert(changed.finalized());
    assert(chmod(changed_archive.c_str(), 0600) == 0);
    const int changed_fd = open(changed_archive.c_str(), O_WRONLY | O_TRUNC);
    assert(changed_fd >= 0);
    assert(write(changed_fd, "changed-content", 15) == 15);
    assert(close(changed_fd) == 0);
    assert(chmod(changed_archive.c_str(), 0400) == 0);
    assert(encrypt_support_bundle(request_for(changed.bundle, root, executable,
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")).failure ==
        SupportBundleEncryptionFailure::invalid_request);

    const std::string artifact1 = "0123456789abcdef0123456789abcdef";
    auto encrypted = encrypt_support_bundle(
        request_for(finalized.bundle, root, executable, artifact1));
    assert(encrypted.encrypted());
    assert(read_all(encrypted.artifact.path) == "AGE-TEST:readable-bundle");
    assert(encrypted.artifact.size == 24);
    assert(!encrypted.artifact.sha256.empty());
    assert(encrypt_support_bundle(
        request_for(finalized.bundle, root, executable, artifact1)).failure ==
        SupportBundleEncryptionFailure::output_collision);

    auto failure_request = [&](const std::string &artifact, const std::string &mode) {
        return encrypt_support_bundle(request_for(finalized.bundle, root, executable,
                                                  artifact, mode));
    };
    assert(failure_request("11111111111111111111111111111111", "age1test-exit").failure ==
           SupportBundleEncryptionFailure::process_failed);
    assert(failure_request("22222222222222222222222222222222", "age1test-empty").failure ==
           SupportBundleEncryptionFailure::empty_output);
    assert(failure_request("33333333333333333333333333333333", "age1test-mode").failure ==
           SupportBundleEncryptionFailure::unsafe_output);
    assert(failure_request("44444444444444444444444444444444", "age1test-large").failure ==
           SupportBundleEncryptionFailure::oversized_output);
    auto timeout_request = request_for(finalized.bundle, root, executable,
        "55555555555555555555555555555555", "age1test-sleep");
    timeout_request.timeout = std::chrono::milliseconds(25);
    assert(encrypt_support_bundle(timeout_request).failure ==
           SupportBundleEncryptionFailure::timed_out);
    for (const auto &entry : fs::directory_iterator(root))
        assert(!entry.path().filename().string().ends_with(".partial"));
    assert(read_all(archive) == "readable-bundle");

    SupportBundleReceipt receipt;
    receipt.case_id = "7K3M-9QFX-2DPA";
    receipt.artifact_id = artifact1;
    receipt.created_at_utc = "2026-08-16T18:30:00Z";
    receipt.archive_filename = finalized.bundle.basename();
    receipt.archive_size = finalized.bundle.size();
    receipt.archive_sha256 = finalized.bundle.sha256();
    receipt.encrypted_filename = encrypted.artifact.basename;
    receipt.encrypted_size = encrypted.artifact.size;
    receipt.encrypted_sha256 = encrypted.artifact.sha256;
    receipt.bundle_encryption_key_id = encrypted.artifact.key_id;
    receipt.issue_url = "https://github.com/WsprryPi/WsprryPi/issues/352";
    const auto receipt_result = write_support_bundle_receipt(root, receipt);
    assert(receipt_result.written());
    const auto receipt_json = nlohmann::json::parse(read_all(receipt_result.path));
    assert(receipt_json.size() == 14);
    assert(receipt_json["schema_version"] == 1);
    assert(receipt_json["case_id"] == receipt.case_id);
    assert(receipt_json["issue_url"] == *receipt.issue_url);
    assert(!receipt_json.contains("contact"));
    assert(!receipt_json.contains("problem_description"));
    assert(write_support_bundle_receipt(root, receipt).failure ==
           SupportBundleReceiptFailure::output_collision);
    receipt.archive_filename = "../plaintext.tar.gz";
    assert(write_support_bundle_receipt(root, receipt).failure ==
           SupportBundleReceiptFailure::invalid_receipt);

    fs::remove_all(root);
    std::cout << "support_bundle_private_artifact_test: PASS\n";
}
