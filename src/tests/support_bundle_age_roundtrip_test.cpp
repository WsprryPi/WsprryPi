#include "support_bundle_private_artifact.hpp"
#include "json.hpp"

#include <array>
#include <cassert>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <openssl/evp.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
void write_private(const fs::path &path, const std::string &contents) {
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(descriptor >= 0);
    assert(write(descriptor, contents.data(), contents.size()) ==
           static_cast<ssize_t>(contents.size()));
    assert(close(descriptor) == 0);
}

std::string read_all(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

std::string sha256_file(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    using DigestContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    DigestContext context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    assert(context && EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) == 1);
    std::array<char, 4096> buffer {};
    while (input) {
        input.read(buffer.data(), buffer.size());
        const auto count = input.gcount();
        if (count > 0) {
            assert(EVP_DigestUpdate(context.get(), buffer.data(),
                                    static_cast<std::size_t>(count)) == 1);
        }
    }
    assert(input.eof());
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest {};
    unsigned int size = 0;
    assert(EVP_DigestFinal_ex(context.get(), digest.data(), &size) == 1);
    assert(size == 32);
    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(size * 2);
    for (unsigned int index = 0; index < size; ++index) {
        output.push_back(hex[digest[index] >> 4]);
        output.push_back(hex[digest[index] & 0x0f]);
    }
    return output;
}

bool decrypt(const fs::path &age,
             const fs::path &identity,
             const fs::path &encrypted,
             const fs::path &output) {
    std::string executable = age.string();
    std::string identity_value = identity.string();
    std::string output_value = output.string();
    std::string encrypted_value = encrypted.string();
    std::vector<char *> arguments = {
        executable.data(), const_cast<char *>("--decrypt"),
        const_cast<char *>("--identity"), identity_value.data(),
        const_cast<char *>("--output"), output_value.data(),
        encrypted_value.data(), nullptr,
    };
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        execv(age.c_str(), arguments.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
}  // namespace

int main(int argc, char **argv) {
    assert(argc == 4);
    const fs::path age = fs::canonical(argv[1]);
    const fs::path identity = fs::canonical(argv[2]);
    const std::string recipient = argv[3];
    assert(recipient.starts_with("age1"));

    char root_template[] = "/tmp/wsprrypi-age-roundtrip.XXXXXX";
    assert(mkdtemp(root_template));
    const fs::path root = fs::canonical(root_template);
    assert(chmod(root.c_str(), 0700) == 0);

    const std::string plaintext = "readable-bundle";
    const fs::path archive = root / "WsprryPi-support-test.tar.gz";
    write_private(archive, plaintext);
    constexpr const char *archive_digest =
        "302392d7f5a88795cb87a3adb1f3fb66cfece9db052e8dc5a5431308a9d964a7";
    auto finalized = finalize_support_bundle(archive, archive_digest, 1024);
    assert(finalized.finalized());

    SupportBundleEncryptionRequest request;
    request.bundle = &finalized.bundle;
    request.job_directory = root;
    request.case_id = "7K3M-9QFX-2DPA";
    request.artifact_id = "0123456789abcdef0123456789abcdef";
    request.recipient = recipient;
    request.key_id = "wsprrypi-bundle-qualification-only";
    request.executable = age;
    request.maximum_encrypted_bytes = 4096;
    request.timeout = std::chrono::seconds(10);
    const auto encrypted = encrypt_support_bundle(request);
    assert(encrypted.encrypted());
    assert(encrypted.artifact.basename ==
           "wsprrypi-support-7K3M-9QFX-2DPA-"
           "0123456789abcdef0123456789abcdef.tar.gz.age");
    assert(encrypted.artifact.size > 0 && encrypted.artifact.sha256.size() == 64);
    struct stat encrypted_info {};
    assert(lstat(encrypted.artifact.path.c_str(), &encrypted_info) == 0);
    assert(S_ISREG(encrypted_info.st_mode) && !S_ISLNK(encrypted_info.st_mode));
    assert(encrypted_info.st_uid == geteuid());
    assert((encrypted_info.st_mode & 0777) == 0600);
    assert(static_cast<std::uint64_t>(encrypted_info.st_size) ==
           encrypted.artifact.size);
    assert(sha256_file(encrypted.artifact.path) == encrypted.artifact.sha256);

    const fs::path decrypted = root / "decrypted.tar.gz";
    assert(decrypt(age, identity, encrypted.artifact.path, decrypted));
    assert(read_all(decrypted) == plaintext);
    assert(read_all(archive) == plaintext);

    SupportBundleReceipt receipt;
    receipt.case_id = request.case_id;
    receipt.artifact_id = request.artifact_id;
    receipt.created_at_utc = "2026-08-16T18:30:00Z";
    receipt.archive_filename = finalized.bundle.basename();
    receipt.archive_size = finalized.bundle.size();
    receipt.archive_sha256 = finalized.bundle.sha256();
    receipt.encrypted_filename = encrypted.artifact.basename;
    receipt.encrypted_size = encrypted.artifact.size;
    receipt.encrypted_sha256 = encrypted.artifact.sha256;
    receipt.bundle_encryption_key_id = encrypted.artifact.key_id;
    const auto receipt_result = write_support_bundle_receipt(root, receipt);
    assert(receipt_result.written());
    const auto receipt_json = nlohmann::json::parse(read_all(receipt_result.path));
    assert(receipt_json["upload_state"] == "encrypted_artifact_downloaded");
    assert(receipt_json["archive_filename"] == finalized.bundle.basename());
    assert(receipt_json["archive_size"] == finalized.bundle.size());
    assert(receipt_json["archive_sha256"] == finalized.bundle.sha256());
    assert(receipt_json["encrypted_filename"] == encrypted.artifact.basename);
    assert(receipt_json["encrypted_size"] == encrypted.artifact.size);
    assert(receipt_json["encrypted_sha256"] == encrypted.artifact.sha256);
    assert(receipt_json["bundle_encryption_key_id"] == encrypted.artifact.key_id);
    assert(!receipt_json.contains("contact"));
    assert(!receipt_json.contains("problem_description"));
    assert(read_all(receipt_result.path).find("AGE-SECRET-KEY-") == std::string::npos);

    for (const auto &entry : fs::directory_iterator(root)) {
        assert(!entry.path().filename().string().ends_with(".partial"));
    }
    fs::remove_all(root);
    std::cout << "support_bundle_age_roundtrip_test: PASS\n";
}
