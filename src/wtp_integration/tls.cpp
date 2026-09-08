// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "tls.hpp"
#include "identity.hpp"
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#if OPENSSL_VERSION_NUMBER < 0x30000000L
#error "WsprryPi network integration requires OpenSSL 3 or later"
#endif
#include <openssl/x509v3.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <netdb.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <arpa/inet.h>

namespace wsprrypi {
namespace {
std::atomic_bool resolver_busy{false};
struct Resolution {
  std::mutex mutex;
  std::optional<std::vector<std::string>> addresses;
};
class SystemResolver final : public TlsResolver {
  std::shared_ptr<Resolution> result_;
public:
  bool begin(const std::string &host, unsigned port) override {
    cancel();
    if (resolver_busy.exchange(true)) return false;
    try {
      result_ = std::make_shared<Resolution>();
      std::thread([result = result_, host, port] {
        std::vector<std::string> addresses;
        try {
        addrinfo hints{}, *found = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        struct FreeAddresses { addrinfo *&value; ~FreeAddresses() { if (value) freeaddrinfo(value); } } cleanup{found};
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) == 0) {
          for (auto p = found; p && addresses.size() < 8; p = p->ai_next) {
            char address[NI_MAXHOST]{};
            if (getnameinfo(p->ai_addr, p->ai_addrlen, address, sizeof(address),
                            nullptr, 0, NI_NUMERICHOST) == 0 &&
                std::find(addresses.begin(), addresses.end(), address) == addresses.end())
              addresses.emplace_back(address);
          }

        }
        } catch (...) { addresses.clear(); }
        { std::lock_guard lock(result->mutex); result->addresses = std::move(addresses); }
        resolver_busy = false;
      }).detach();
    } catch (...) { resolver_busy = false; result_.reset(); return false; }
    return true;
  }
  std::optional<std::vector<std::string>> poll() override {
    if (!result_) return std::vector<std::string>{};
    std::lock_guard lock(result_->mutex);
    return result_->addresses;
  }
  void cancel() noexcept override { result_.reset(); }
};
std::vector<unsigned char> read_credential(const std::string &path, bool key) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) throw std::runtime_error("TLS credential is not readable as a regular local file");
  struct Guard { int fd; ~Guard() { ::close(fd); } } guard{fd};
  struct stat st{};
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 262144 ||
      (st.st_mode & 0022) || (key && ((st.st_mode & 0077) ||
                                    (st.st_uid != geteuid() && st.st_uid != 0))))
    throw std::runtime_error("TLS credentials require regular protected files; private key requires owner-only permissions");
  std::vector<unsigned char> bytes(static_cast<std::size_t>(st.st_size));
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto n = ::read(fd, bytes.data() + offset, bytes.size() - offset);
    if (n <= 0) throw std::runtime_error("TLS credential read failed");
    offset += static_cast<std::size_t>(n);
  }
  return bytes;
}
int no_password(char *, int, int, void *) { return 0; }
bool literal(const std::string &s) {
  in6_addr value{};
  return inet_pton(AF_INET, s.c_str(), &value) == 1 ||
         inet_pton(AF_INET6, s.c_str(), &value) == 1;
}
// OpenSSL's socket BIO can raise SIGPIPE. This BIO uses per-send suppression,
// preserving the application's signal policy and nonblocking retry semantics.
BIO_METHOD *socket_method() {
  static BIO_METHOD *method = [] {
    auto *m = BIO_meth_new(BIO_TYPE_SOURCE_SINK, "WTP nonblocking socket");
    BIO_meth_set_create(m, [](BIO *b) { BIO_set_init(b, 1); return 1; });
    BIO_meth_set_destroy(m, [](BIO *) { return 1; });
    BIO_meth_set_read(m, [](BIO *b, char *data, int size) {
      BIO_clear_retry_flags(b);
      const auto n = recv(static_cast<int>(reinterpret_cast<intptr_t>(BIO_get_data(b))), data, size, 0);
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) BIO_set_retry_read(b);
      return static_cast<int>(n);
    });
    BIO_meth_set_write(m, [](BIO *b, const char *data, int size) {
      BIO_clear_retry_flags(b);
#ifdef MSG_NOSIGNAL
      constexpr int flags = MSG_NOSIGNAL;
#else
      constexpr int flags = 0;
#endif
      const auto n = send(static_cast<int>(reinterpret_cast<intptr_t>(BIO_get_data(b))), data, size, flags);
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) BIO_set_retry_write(b);
      return static_cast<int>(n);
    });
    BIO_meth_set_ctrl(m, [](BIO *, int command, long, void *) -> long {
      return command == BIO_CTRL_FLUSH ? 1 : 0;
    });
    return m;
  }();
  return method;
}
} // namespace
std::unique_ptr<TlsResolver> system_tls_resolver() { return std::make_unique<SystemResolver>(); }
struct TlsCredentials::Impl {
  SSL_CTX *context{};
  std::array<unsigned char, 32> fingerprint{};
  ~Impl() { SSL_CTX_free(context); }
};
TlsCredentials::TlsCredentials(const TlsSelection &s) : impl_(std::make_unique<Impl>()) {
  auto ca = read_credential(s.ca_file, false);
  auto cert = read_credential(s.certificate_file, false);
  auto key = read_credential(s.key_file, true);
  struct Clean { std::vector<unsigned char> &v; ~Clean() { OPENSSL_cleanse(v.data(), v.size()); } } clean{key};
  auto *ctx = impl_->context = SSL_CTX_new(TLS_client_method());
  if (!ctx || !SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION) ||
      !SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION))
    throw std::runtime_error("TLS 1.3 is unavailable");
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
  SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET | SSL_OP_NO_RENEGOTIATION);
  SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
  SSL_CTX_set_max_early_data(ctx, 0);
  SSL_CTX_set_default_passwd_cb(ctx, no_password);
  auto *bio = BIO_new_mem_buf(ca.data(), static_cast<int>(ca.size()));
  unsigned count = 0;
  while (auto *x = PEM_read_bio_X509(bio, nullptr, no_password, nullptr)) {
    const bool added = X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx), x) == 1;
    X509_free(x);
    if (!added) { BIO_free(bio); throw std::runtime_error("Invalid TLS trust bundle"); }
    ++count;
  }
  BIO_free(bio); ERR_clear_error();
  if (!count) throw std::runtime_error("TLS trust bundle has no certificates");
  bio = BIO_new_mem_buf(cert.data(), static_cast<int>(cert.size()));
  auto *leaf = PEM_read_bio_X509(bio, nullptr, no_password, nullptr);
  const bool valid = leaf && X509_cmp_current_time(X509_get0_notBefore(leaf)) < 0 &&
      X509_cmp_current_time(X509_get0_notAfter(leaf)) > 0 && SSL_CTX_use_certificate(ctx, leaf) == 1;
  X509_free(leaf);
  if (!valid) { BIO_free(bio); throw std::runtime_error("TLS client certificate is invalid or outside its validity period"); }
  while (auto *chain = PEM_read_bio_X509(bio, nullptr, no_password, nullptr)) {
    if (!SSL_CTX_add_extra_chain_cert(ctx, chain)) { X509_free(chain); BIO_free(bio); throw std::runtime_error("Invalid TLS client chain"); }
  }
  BIO_free(bio); ERR_clear_error();
  bio = BIO_new_mem_buf(key.data(), static_cast<int>(key.size()));
  auto *private_key = PEM_read_bio_PrivateKey(bio, nullptr, no_password, nullptr);
  BIO_free(bio);
  const bool matched = private_key && SSL_CTX_use_PrivateKey(ctx, private_key) == 1 && SSL_CTX_check_private_key(ctx) == 1;
  EVP_PKEY_free(private_key);
  if (!matched) throw std::runtime_error("TLS client certificate and private key do not match or key is unusable");
  auto *digest = EVP_MD_CTX_new();
  EVP_DigestInit_ex(digest, EVP_sha256(), nullptr);
  for (const auto *v : {&ca, &cert, &key}) {
    const auto length = std::to_string(v->size()) + ":";
    EVP_DigestUpdate(digest, length.data(), length.size());
    EVP_DigestUpdate(digest, v->data(), v->size());
  }
  EVP_DigestFinal_ex(digest, impl_->fingerprint.data(), nullptr);
  EVP_MD_CTX_free(digest);
}
TlsCredentials::~TlsCredentials() = default;
bool TlsCredentials::matches(const TlsCredentials &other) const noexcept {
  return CRYPTO_memcmp(impl_->fingerprint.data(), other.impl_->fingerprint.data(), 32) == 0;
}
struct TlsStream::Impl {
  Clock clock;
  Access access;
  std::unique_ptr<TlsResolver> resolver;
  std::shared_ptr<TlsCredentials> credentials;
  TlsSelection selection;
  std::string alpn;
  SSL *ssl{};
  int fd{-1};
  mutable std::mutex mutex;
  TlsObservation status;
  std::atomic_bool cancelled{false};
  std::uint64_t deadline{}, write_deadline{};
  std::vector<std::string> addresses;
  std::size_t next_address{};
  std::vector<std::uint8_t> pending;
  Impl(Clock c, Access a, std::unique_ptr<TlsResolver> r) : clock(std::move(c)), access(a), resolver(std::move(r)) {}
  void state(std::string value, std::string diagnostic = {}) {
    std::lock_guard lock(mutex);
    status.state = std::move(value); status.diagnostic = std::move(diagnostic);
    status.observed_ms = clock();
  }
  std::string state() const { std::lock_guard lock(mutex); return status.state; }
  void close() noexcept {
    resolver->cancel();
    SSL_free(ssl); ssl = nullptr;
    if (fd >= 0) ::close(fd);
    fd = -1; pending.clear(); credentials.reset();
  }
  void fail(const char *why) { close(); state("failed", why); }
  bool check() {
    if (cancelled) { fail("TLS operation cancelled; remote output is unconfirmed"); return false; }
    return ssl != nullptr;
  }
  void connect_next() {
    if (fd >= 0) ::close(fd);
    fd = -1;
    if (clock() >= deadline) { fail("TCP connection deadline exceeded"); return; }
    while (next_address < addresses.size()) {
      const auto address = addresses[next_address++];
      sockaddr_storage storage{};
      socklen_t length{};
      auto &v4 = reinterpret_cast<sockaddr_in &>(storage);
      auto &v6 = reinterpret_cast<sockaddr_in6 &>(storage);
      bool loopback = false;
      if (inet_pton(AF_INET, address.c_str(), &v4.sin_addr) == 1) {
        v4.sin_family = AF_INET; v4.sin_port = htons(selection.port); length = sizeof(v4);
        loopback = (ntohl(v4.sin_addr.s_addr) >> 24) == 127;
      } else if (inet_pton(AF_INET6, address.c_str(), &v6.sin6_addr) == 1) {
        v6.sin6_family = AF_INET6; v6.sin6_port = htons(selection.port); length = sizeof(v6);
        loopback = IN6_IS_ADDR_LOOPBACK(&v6.sin6_addr);
      } else continue;
      if (access == Access::LoopbackTest && !loopback) { fail("Test TLS transport requires loopback addresses"); return; }
      fd = socket(storage.ss_family, SOCK_STREAM, IPPROTO_TCP);
      if (fd < 0) continue;
      if (fcntl(fd, F_SETFL, O_NONBLOCK) || fcntl(fd, F_SETFD, FD_CLOEXEC)) { ::close(fd); fd = -1; continue; }
#ifdef SO_NOSIGPIPE
      int yes = 1;
      setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
      { std::lock_guard lock(mutex); status.address = address; }
      if (::connect(fd, reinterpret_cast<sockaddr *>(&storage), length) == 0 || errno == EINPROGRESS) {
        state("connecting"); return;
      }
      ::close(fd); fd = -1;
    }
    fail("TCP endpoint connection failed");
  }
  bool flush() {
    if (pending.empty()) return true;
    if (!check()) return false;
    if (clock() >= write_deadline) { fail("TLS write deadline exceeded; accepted bytes have an unknown remote effect"); return false; }
    std::size_t written{};
    ERR_clear_error();
    const int result = SSL_write_ex(ssl, pending.data(), pending.size(), &written);
    if (result == 1) { pending.clear(); return true; }
    const int error = SSL_get_error(ssl, result);
    if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return false;
    fail("TLS write failed; accepted bytes have an unknown remote effect"); return false;
  }
};
TlsStream::TlsStream(Clock c, Access a, std::unique_ptr<TlsResolver> r)
    : impl_(std::make_unique<Impl>(std::move(c), a, std::move(r))) {}
TlsStream::~TlsStream() { close(); }
bool TlsStream::begin_open(const TlsSelection &s, std::shared_ptr<TlsCredentials> credentials, std::string alpn) {
  close(); auto &p = *impl_; p.cancelled = false;
  { std::lock_guard lock(p.mutex); p.status = {}; }
  if (p.access == Access::Production) {
    const char *disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
    if (disabled && std::string(disabled) == "1") { p.fail("Hardware access disabled; network transmitter access is prohibited"); return false; }
  }
  const auto host = canonical_network_identity(s.host);
  const auto identity = canonical_network_identity(s.expected_identity.empty() ? s.host : s.expected_identity);
  if (!credentials || !host || !identity || !s.port || s.port > 65535 ||
      (alpn != "wtp/1" && alpn != "http/1.1")) { p.fail("Invalid TLS endpoint"); return false; }
  p.selection = s; p.selection.host = *host; p.selection.expected_identity = *identity;
  p.credentials = std::move(credentials); p.alpn = std::move(alpn);
  p.deadline = p.clock() + resolve_timeout_ms;
  if (!p.resolver->begin(p.selection.host, s.port)) { p.fail("Resolver unavailable; another lookup is outstanding"); return false; }
  p.state("resolving"); return true;
}
void TlsStream::poll_open() {
  auto &p = *impl_;
  if (!opening()) return;
  if (p.cancelled) { p.fail("TLS connection cancelled"); return; }
  if (p.clock() >= p.deadline) { p.fail("TLS resolution, connection or handshake deadline exceeded"); return; }
  if (p.state() == "resolving") {
    auto result = p.resolver->poll(); if (!result) return;
    p.resolver->cancel(); p.addresses = std::move(*result); p.next_address = 0;
    p.deadline = p.clock() + connect_timeout_ms; p.connect_next(); return;
  }
  if (p.state() == "connecting") {
    pollfd fd{p.fd, POLLOUT, 0};
    const auto result = ::poll(&fd, 1, 0);
    if (!result || (result < 0 && errno == EINTR)) return;
    int error = 0; socklen_t size = sizeof(error);
    if (result < 0 || getsockopt(p.fd, SOL_SOCKET, SO_ERROR, &error, &size) || error) { p.connect_next(); return; }
    p.ssl = SSL_new(p.credentials->impl_->context);
    if (!p.ssl) { p.fail("TLS session allocation failed"); return; }
    auto *bio = BIO_new(socket_method());
    if (!bio) { p.fail("TLS socket allocation failed"); return; }
    BIO_set_data(bio, reinterpret_cast<void *>(static_cast<intptr_t>(p.fd)));
    SSL_set_bio(p.ssl, bio, bio);
    SSL_set_connect_state(p.ssl);
    const auto name = p.selection.expected_identity.empty() ? p.selection.host : p.selection.expected_identity;
    auto *parameters = SSL_get0_param(p.ssl);
    X509_VERIFY_PARAM_set_hostflags(parameters, X509_CHECK_FLAG_NO_WILDCARDS | X509_CHECK_FLAG_NEVER_CHECK_SUBJECT);
    const bool identity = literal(name) ? X509_VERIFY_PARAM_set1_ip_asc(parameters, name.c_str()) == 1
        : SSL_set1_host(p.ssl, name.c_str()) == 1 && SSL_set_tlsext_host_name(p.ssl, name.c_str()) == 1;
    std::string wire(1, static_cast<char>(p.alpn.size())); wire += p.alpn;
    if (!identity || SSL_set_alpn_protos(p.ssl, reinterpret_cast<const unsigned char *>(wire.data()), wire.size())) {
      p.fail("TLS identity or ALPN setup failed"); return;
    }
    p.deadline = p.clock() + handshake_timeout_ms; p.state("handshaking");
  }
  if (p.state() != "handshaking") return;
  ERR_clear_error();
  const int result = SSL_do_handshake(p.ssl);
  if (result == 1) {
    const unsigned char *selected{}; unsigned length{};
    SSL_get0_alpn_selected(p.ssl, &selected, &length);
    if (SSL_version(p.ssl) != TLS1_3_VERSION || SSL_get_verify_result(p.ssl) != X509_V_OK ||
        std::string_view(reinterpret_cast<const char *>(selected), length) != p.alpn) {
      p.fail("TLS identity, version or ALPN verification failed"); return;
    }
    { std::lock_guard lock(p.mutex); p.status.authenticated_identity = p.selection.expected_identity.empty() ? p.selection.host : p.selection.expected_identity; }
    p.state("ready"); return;
  }
  const int error = SSL_get_error(p.ssl, result);
  if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE)
    p.fail("TLS handshake or certificate authentication failed");
}
bool TlsStream::ready() const { return impl_->state() == "ready"; }
bool TlsStream::opening() const { auto s = impl_->state(); return s == "resolving" || s == "connecting" || s == "handshaking"; }
void TlsStream::cancel() noexcept { impl_->cancelled = true; }
TlsObservation TlsStream::observation() const { std::lock_guard lock(impl_->mutex); return impl_->status; }
wtp::IoResult TlsStream::write(std::span<const std::uint8_t> bytes) {
  auto &p = *impl_;
  if (!ready() || !p.check()) return {wtp::IoState::Failed};
  if (!p.flush()) return {ready() ? wtp::IoState::WouldBlock : wtp::IoState::Failed};
  if (bytes.empty()) return {wtp::IoState::WouldBlock};
  const auto count = std::min<std::size_t>(4096, bytes.size());
  p.pending.assign(bytes.begin(), bytes.begin() + count);
  p.write_deadline = p.clock() + io_timeout_ms;
  // Progress means accepted into our owned bounded buffer, including before
  // SSL has emitted ciphertext. Session therefore retains uncertainty on loss.
  return {wtp::IoState::Progress, count};
}
wtp::IoResult TlsStream::read(std::span<std::uint8_t> bytes) {
  auto &p = *impl_;
  if (!ready() || !p.check()) return {wtp::IoState::Failed};
  if (!p.flush()) return {ready() ? wtp::IoState::WouldBlock : wtp::IoState::Failed};
  if (bytes.empty()) return {wtp::IoState::WouldBlock};
  std::size_t count{}; ERR_clear_error();
  const int result = SSL_read_ex(p.ssl, bytes.data(), std::min<std::size_t>(bytes.size(), 4096), &count);
  if (result == 1) return {wtp::IoState::Progress, count};
  const int error = SSL_get_error(p.ssl, result);
  if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return {wtp::IoState::WouldBlock};
  p.fail(error == SSL_ERROR_ZERO_RETURN ? "TLS peer closed; remote output is unconfirmed" : "TLS read failed or peer closed abruptly; remote output is unconfirmed");
  return {error == SSL_ERROR_ZERO_RETURN ? wtp::IoState::Closed : wtp::IoState::Failed};
}
void TlsStream::close() noexcept { impl_->close(); if (observation().state != "failed") impl_->state("closed"); }
} // namespace wsprrypi
