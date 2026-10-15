// SPDX-License-Identifier: MIT
#include "consumer_radio.hpp"
#include "browser_api.hpp"
#include "identity.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <memory>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <thread>
namespace wsprrypi {
namespace {
using Json = nlohmann::json;
using Bytes = std::vector<unsigned char>;
PicoHttpResponse error(unsigned status, const char *code) {
  return {status, Json{{"error", {{"code", code}}}}.dump(), {}};
}
void require(bool ok) {
  if (!ok)
    throw std::runtime_error("consumer_protocol");
}
bool fields(const Json &j, std::initializer_list<const char *> keys) {
  return j.is_object() && j.size() == keys.size() &&
         std::all_of(keys.begin(), keys.end(),
                     [&](auto key) { return j.contains(key); });
}
bool hex_id(const std::string &value) {
  return value.size() == 32 &&
         std::all_of(value.begin(), value.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
std::string hex(std::span<const unsigned char> bytes) {
  std::string result;
  result.reserve(bytes.size() * 2);
  for (auto b : bytes) {
    result += "0123456789abcdef"[b >> 4];
    result += "0123456789abcdef"[b & 15];
  }
  return result;
}
Bytes unhex(const std::string &value, std::size_t size) {
  require(value.size() == size * 2);
  Bytes result(size);
  const std::string digits = "0123456789abcdef";
  for (std::size_t i = 0; i < size; ++i) {
    auto a = digits.find(value[i * 2]), b = digits.find(value[i * 2 + 1]);
    require(a < 16 && b < 16);
    result[i] = a * 16 + b;
  }
  return result;
}
std::string b64(std::span<const unsigned char> data) {
  std::string value((data.size() + 2) / 3 * 4, '\0');
  EVP_EncodeBlock(reinterpret_cast<unsigned char *>(value.data()), data.data(),
                  static_cast<int>(data.size()));
  while (value.ends_with('='))
    value.pop_back();
  std::replace(value.begin(), value.end(), '+', '-');
  std::replace(value.begin(), value.end(), '/', '_');
  return value;
}
Bytes unb64(const std::string &input) {
  require(input.size() == 43);
  auto value = input;
  std::replace(value.begin(), value.end(), '-', '+');
  std::replace(value.begin(), value.end(), '_', '/');
  value += '=';
  Bytes bytes(33);
  require(EVP_DecodeBlock(bytes.data(),
                          reinterpret_cast<const unsigned char *>(value.data()),
                          44) == 33);
  bytes.resize(32);
  require(b64(bytes) == input);
  return bytes;
}
std::uint64_t decimal(const Json &value) {
  require(value.is_string());
  const auto s = value.get<std::string>();
  std::uint64_t number{};
  const auto r = std::from_chars(s.data(), s.data() + s.size(), number);
  require(!s.empty() && s.size() <= 20 && s.front() != '0' &&
          r.ec == std::errc() && r.ptr == s.data() + s.size());
  return number;
}
template <typename T> void wipe(T &bytes) {
  if (!bytes.empty())
    OPENSSL_cleanse(bytes.data(), bytes.size());
}
std::array<unsigned char, 32> hash(std::span<const unsigned char> bytes) {
  std::array<unsigned char, 32> result{};
  unsigned count{};
  require(EVP_Digest(bytes.data(), bytes.size(), result.data(), &count,
                     EVP_sha256(), nullptr) == 1 &&
          count == 32);
  return result;
}
void derive(std::span<const unsigned char> secret,
            std::span<const unsigned char> salt, std::string_view info,
            std::span<unsigned char> output) {
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
      EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr), EVP_PKEY_CTX_free);
  std::size_t size = output.size();
  require(ctx && EVP_PKEY_derive_init(ctx.get()) == 1 &&
          EVP_PKEY_CTX_set_hkdf_md(ctx.get(), EVP_sha256()) == 1 &&
          EVP_PKEY_CTX_set1_hkdf_salt(ctx.get(), salt.data(), salt.size()) ==
              1 &&
          EVP_PKEY_CTX_set1_hkdf_key(ctx.get(), secret.data(), secret.size()) ==
              1 &&
          EVP_PKEY_CTX_add1_hkdf_info(
              ctx.get(), reinterpret_cast<const unsigned char *>(info.data()),
              info.size()) == 1 &&
          EVP_PKEY_derive(ctx.get(), output.data(), &size) == 1 &&
          size == output.size());
}
std::array<unsigned char, 28> header(std::span<const unsigned char, 16> session,
                                     std::uint64_t seq) {
  std::array<unsigned char, 28> result{'C', 'R', '0', '1'};
  std::copy(session.begin(), session.end(), result.begin() + 4);
  for (unsigned i = 0; i < 8; ++i)
    result[20 + i] = seq >> (56 - i * 8);
  return result;
}
// Local secrets have a lexical lifetime even if parsing/allocation/crypto
// throws.
struct Secrets {
  std::array<unsigned char, 64> secret{}, traffic{};
  std::array<unsigned char, 32> login{}, password{};
  ~Secrets() {
    wipe(secret);
    wipe(traffic);
    wipe(login);
    wipe(password);
  }
};
} // namespace
bool valid_management_contract(const Json &j, const std::string &carrier,
                               const std::string &device,
                               const std::string &boot,
                               const std::string &generation) {
  try {
    if (!fields(j, {"schema", "target", "profile", "preclock",
                    "profile_generation", "selected", "carriers", "unsupported",
                    "operations", "limits"}) ||
        j["schema"] != "transmitter-management/1" || j["selected"] != carrier ||
        !j["preclock"].is_boolean() ||
        j["target"] !=
            Json{{"scope", "member"}, {"device_id", device}, {"boot_id", boot}})
      return false;
    const bool consumer =
        carrier == "consumer_station" || carrier == "consumer_ap";
    if (j["profile"] != (consumer ? "consumer" : "engineering"))
      return false;
    if (consumer) {
      if (j["profile_generation"] != generation ||
          !decimal(j["profile_generation"]))
        return false;
    } else if (!(j["profile_generation"].is_null() ||
                 decimal(j["profile_generation"])))
      return false;
    if (!j["carriers"].is_array() || j["carriers"].size() != 4)
      return false;
    const char *names[] = {"engineering_mtls", "engineering_field",
                           "consumer_station", "consumer_ap"};
    bool admitted = false;
    for (unsigned i = 0; i < 4; ++i) {
      const auto &row = j["carriers"][i];
      if (!fields(row, {"carrier", "built_in", "available"}) ||
          row["carrier"] != names[i] || !row["built_in"].is_boolean() ||
          !row["available"].is_boolean() ||
          (row["available"] == true && row["built_in"] != true))
        return false;
      if (row["carrier"] == carrier)
        admitted = row["built_in"] == true && row["available"] == true;
    }
    const Json operations = {{"GET",
                              {"capabilities", "status", "config", "schedules",
                               "network", "application", "management"}},
                             {"PUT", {"config", "schedules", "network"}},
                             {"POST", {"config/update"}}};
    const Json limits = {{"http_body", 32768},    {"consumer_plaintext", 32724},
                         {"config", 1800},        {"update", 2048},
                         {"login_ms", 30000},     {"idle_ms", 300000},
                         {"absolute_ms", 900000}, {"consumer_sessions", 1}};
    for (const auto &[name, value] : limits.items()) {
      (void)value;
      if (!j["limits"].is_object() || !j["limits"].contains(name) ||
          !j["limits"][name].is_number_integer())
        return false;
    }
    return admitted && j["operations"] == operations && j["limits"] == limits &&
           j["unsupported"] == Json::array({"usb_json", "plain_wtp_json",
                                            "tls_wtp_json", "ble_json"});
  } catch (...) {
    return false;
  }
}
ConsumerRadioClient::ConsumerRadioClient(Transport transport,
                                         TlsStream::Clock clock)
    : transport_(std::move(transport)), clock_(std::move(clock)) {}
ConsumerRadioClient::~ConsumerRadioClient() { clear(); }
void ConsumerRadioClient::clear() noexcept {
  wipe(request_key_);
  wipe(response_key_);
  wipe(transcript_);
  wipe(session_);
  authenticated_ = false;
  sequence_ = started_ = seen_ = 0;
  device_.clear();
  boot_.clear();
  generation_.clear();
}
bool ConsumerRadioClient::active() const {
  const auto now = clock_();
  return authenticated_ && now >= started_ && now >= seen_ &&
         now - started_ < 900000 && now - seen_ < 300000;
}
std::string ConsumerRadioClient::seal(std::string_view plain,
                                      std::span<const unsigned char, 32> key,
                                      std::uint64_t seq) {
  require(plain.size() <= 32724);
  const auto h = header(session_, seq);
  std::array<unsigned char, 12> nonce{};
  std::copy(h.begin() + 20, h.end(), nonce.begin() + 4);
  std::array<unsigned char, 60> aad{};
  std::copy(transcript_.begin(), transcript_.end(), aad.begin());
  std::copy(h.begin(), h.end(), aad.begin() + 32);
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(
      EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  std::string packet(44 + plain.size(), '\0');
  std::copy(h.begin(), h.end(), packet.begin());
  int count{}, tail{};
  require(ctx &&
          EVP_EncryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr,
                             key.data(), nonce.data()) == 1 &&
          EVP_EncryptUpdate(ctx.get(), nullptr, &count, aad.data(),
                            aad.size()) == 1 &&
          EVP_EncryptUpdate(
              ctx.get(), reinterpret_cast<unsigned char *>(packet.data() + 28),
              &count, reinterpret_cast<const unsigned char *>(plain.data()),
              plain.size()) == 1 &&
          static_cast<std::size_t>(count) == plain.size() &&
          EVP_EncryptFinal_ex(
              ctx.get(),
              reinterpret_cast<unsigned char *>(packet.data() + 28 + count),
              &tail) == 1 &&
          tail == 0 &&
          EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_GET_TAG, 16,
                              packet.data() + packet.size() - 16) == 1);
  return packet;
}
std::optional<std::string> ConsumerRadioClient::open(std::string_view packet,
                                                     std::uint64_t seq) {
  if (packet.size() < 44 || packet.size() > 32768)
    return {};
  const auto h = header(session_, seq);
  if (CRYPTO_memcmp(h.data(), packet.data(), h.size()))
    return {};
  std::array<unsigned char, 12> nonce{};
  std::copy(h.begin() + 20, h.end(), nonce.begin() + 4);
  std::array<unsigned char, 60> aad{};
  std::copy(transcript_.begin(), transcript_.end(), aad.begin());
  std::copy(h.begin(), h.end(), aad.begin() + 32);
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(
      EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  std::string plain(packet.size() - 44, '\0');
  int count{}, tail{};
  if (!(ctx &&
        EVP_DecryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr,
                           response_key_.data(), nonce.data()) == 1 &&
        EVP_DecryptUpdate(ctx.get(), nullptr, &count, aad.data(), aad.size()) ==
            1 &&
        EVP_DecryptUpdate(
            ctx.get(), reinterpret_cast<unsigned char *>(plain.data()), &count,
            reinterpret_cast<const unsigned char *>(packet.data() + 28),
            plain.size()) == 1 &&
        static_cast<std::size_t>(count) == plain.size() &&
        EVP_CIPHER_CTX_ctrl(
            ctx.get(), EVP_CTRL_AEAD_SET_TAG, 16,
            const_cast<char *>(packet.data() + packet.size() - 16)) == 1 &&
        EVP_DecryptFinal_ex(
            ctx.get(), reinterpret_cast<unsigned char *>(plain.data() + count),
            &tail) == 1 &&
        tail == 0)) {
    wipe(plain);
    return {};
  }
  return plain;
}
PicoHttpResponse ConsumerRadioClient::login(const std::string &device,
                                            const std::string &carrier,
                                            std::string_view password) {
  if (active())
    return error(409, "management_session_already_selected");
  clear();
  try {
    require(hex_id(device) &&
            (carrier == "consumer_station" || carrier == "consumer_ap") &&
            password.size() >= 8 && password.size() <= 63 &&
            std::all_of(password.begin(), password.end(),
                        [](unsigned char c) { return c >= 32 && c <= 126; }));
    const auto before = clock_();
    auto r =
        transport_("/api/consumer/v1/identity", "GET", {}, "application/json");
    require(r.status == 200 && r.body.size() <= 512 &&
            r.media_type == "application/json");
    auto identity = strict_browser_json(r.body);
    require(fields(identity, {"version", "device_id", "boot_id", "generation",
                              "authentication"}) &&
            identity["version"].is_number_integer() &&
            identity["version"] == 1 && identity["device_id"] == device &&
            identity["authentication"] == "saved_wifi_passphrase");
    device_ = device;
    boot_ = identity["boot_id"].get<std::string>();
    require(hex_id(boot_));
    const auto gen = decimal(identity["generation"]);
    generation_ = identity["generation"];
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> maker(
        EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY *raw = nullptr;
    require(maker && EVP_PKEY_keygen_init(maker.get()) == 1 &&
            EVP_PKEY_keygen(maker.get(), &raw) == 1);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(raw, EVP_PKEY_free);
    std::array<unsigned char, 32> pub{};
    std::size_t size = 32;
    require(EVP_PKEY_get_raw_public_key(key.get(), pub.data(), &size) == 1 &&
            size == 32);
    std::array<unsigned char, 16> nonce{};
    require(RAND_bytes(nonce.data(), nonce.size()) == 1);
    Json start = {{"version", 1},           {"device_id", device_},
                  {"boot_id", boot_},       {"generation", generation_},
                  {"public_key", b64(pub)}, {"nonce", hex(nonce)}};
    r = transport_("/api/consumer/v1/start", "POST", start.dump(),
                   "application/json");
    if (r.status == 409 && r.body.size() <= 256 &&
        strict_browser_json(r.body)
                .value("error", Json::object())
                .value("code", "") == "radio_session_busy") {
      clear();
      return error(409, "radio_session_busy");
    }
    require(r.status == 200 && r.body.size() <= 1024 &&
            r.media_type == "application/json");
    const auto answer = strict_browser_json(r.body);
    require(
        fields(answer, {"version", "device_id", "boot_id", "generation",
                        "session_id", "public_key", "challenge", "login_ms"}) &&
        answer["version"].is_number_integer() && answer["version"] == 1 &&
        answer["device_id"] == device_ && answer["boot_id"] == boot_ &&
        answer["generation"] == generation_ &&
        answer["login_ms"].is_number_integer() && answer["login_ms"] == 30000);
    const auto sid = unhex(answer["session_id"], 16),
               challenge = unhex(answer["challenge"], 16),
               peer = unb64(answer["public_key"]);
    std::copy(sid.begin(), sid.end(), session_.begin());
    constexpr char domain[] = "WsprryPico/Consumer-Radio/1";
    Bytes transcript(domain, domain + sizeof(domain));
    for (const auto &part : {unhex(device_, 16), unhex(boot_, 16), sid})
      transcript.insert(transcript.end(), part.begin(), part.end());
    for (unsigned i = 0; i < 8; ++i)
      transcript.push_back(gen >> (56 - i * 8));
    for (const auto &part : {Bytes(pub.begin(), pub.end()), peer, challenge,
                             Bytes(nonce.begin(), nonce.end())})
      transcript.insert(transcript.end(), part.begin(), part.end());
    transcript_ = hash(transcript);
    Secrets secrets;
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> remote(
        EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peer.data(),
                                    peer.size()),
        EVP_PKEY_free);
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> agreement(
        EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free);
    size = 32;
    require(remote && agreement && EVP_PKEY_derive_init(agreement.get()) == 1 &&
            EVP_PKEY_derive_set_peer(agreement.get(), remote.get()) == 1 &&
            EVP_PKEY_derive(agreement.get(), secrets.secret.data(), &size) ==
                1 &&
            size == 32 &&
            std::any_of(secrets.secret.begin(), secrets.secret.begin() + 32,
                        [](auto b) { return b != 0; }));
    key.reset();
    secrets.password =
        hash({reinterpret_cast<const unsigned char *>(password.data()),
              password.size()});
    std::copy(secrets.password.begin(), secrets.password.end(),
              secrets.secret.begin() + 32);
    derive(secrets.secret, transcript_, "WsprryPico radio login v1",
           secrets.login);
    derive(secrets.secret, transcript_, "WsprryPico radio traffic v1",
           secrets.traffic);
    std::copy_n(secrets.traffic.begin(), 32, request_key_.begin());
    std::copy_n(secrets.traffic.begin() + 32, 32, response_key_.begin());
    require(clock_() >= before && clock_() - before < 22000);
    r = transport_(
        "/api/consumer/v1/finish", "POST",
        seal({reinterpret_cast<const char *>(transcript_.data()), 32},
             secrets.login, 0),
        "application/octet-stream");
    require(r.status == 200 && r.media_type == "application/octet-stream");
    auto plain = open(r.body, 0);
    require(plain.has_value());
    auto authenticated = strict_browser_json(*plain);
    wipe(*plain);
    require(fields(authenticated, {"authenticated", "device_id", "boot_id"}) &&
            authenticated["authenticated"] == true &&
            authenticated["device_id"] == device_ &&
            authenticated["boot_id"] == boot_);
    authenticated_ = true;
    sequence_ = 1;
    started_ = seen_ = clock_();
    r = request("management", "GET");
    if (r.status != 200 ||
        !valid_management_contract(strict_browser_json(r.body), carrier,
                                   device_, boot_, generation_)) {
      logout();
      return error(409, "unsupported_management_contract");
    }
    return {200,
            Json{{"connected", true},
                 {"carrier", carrier},
                 {"device_id", device_},
                 {"boot_id", boot_},
                 {"generation", generation_}}
                .dump(),
            {}};
  } catch (...) {
    clear();
    return error(401, "consumer_authentication_or_connection_failed");
  }
}
PicoHttpResponse ConsumerRadioClient::request(const std::string &resource,
                                              const std::string &method,
                                              const std::string &body,
                                              const std::string &revision) {
  const bool mutation = method != "GET";
  if (!active()) {
    clear();
    return error(401, "consumer_login_required");
  }
  const bool allowed =
      (method == "GET" && (resource == "capabilities" || resource == "status" ||
                           resource == "config" || resource == "schedules" ||
                           resource == "network" || resource == "application" ||
                           resource == "management")) ||
      (method == "PUT" && (resource == "config" || resource == "schedules" ||
                           resource == "network")) ||
      (method == "POST" &&
       (resource == "config/update" || resource == "consumer/logout"));
  if (!allowed)
    return error(404, "not_found");
  if (body.size() > (resource == "config/update" ? 2048U : 1800U) ||
      revision.size() > 80)
    return error(413, "body_too_large");
  if (mutation && resource != "consumer/logout" && revision.empty())
    return error(428, "revision_required");
  try {
    require(sequence_ != UINT64_MAX);
    const auto seq = sequence_++;
    Json envelope = {
        {"method", method},
        {"path", "/api/v1/" + resource},
        {"if_match", revision.empty() ? Json(nullptr) : Json(revision)},
        {"body", method == "GET" ? Json(nullptr) : strict_browser_json(body)}};
    auto plain = envelope.dump();
    auto packet = seal(plain, request_key_, seq);
    wipe(plain);
    const auto r = transport_("/api/consumer/v1/request", "POST", packet,
                              "application/octet-stream");
    require(r.status == 200 && r.media_type == "application/octet-stream");
    auto opened = open(r.body, seq);
    require(opened.has_value());
    auto result = strict_browser_json(*opened);
    wipe(*opened);
    require(fields(result, {"status", "etag", "body"}) &&
            result["status"].is_number_integer() && result["status"] >= 200 &&
            result["status"] <= 599 &&
            (result["etag"].is_null() || result["etag"].is_string()) &&
            result["body"].is_object());
    const auto etag = result["etag"].is_null()
                          ? std::string{}
                          : result["etag"].get<std::string>();
    require(etag.size() <= 128);
    seen_ = clock_();
    return {result["status"].get<unsigned>(), result["body"].dump(), etag};
  } catch (...) {
    clear();
    return error(503, mutation ? "management_outcome_unknown"
                               : "consumer_session_unconfirmed");
  }
}
void ConsumerRadioClient::logout() {
  if (active())
    (void)request("consumer/logout", "POST", "{}");
  clear();
}

#ifdef WSPRRYPI_CONSUMER_CRYPTO_TEST
void consumer_radio_fixed_vectors(const Json &vector) {
  ConsumerRadioClient client({}, [] { return 1000; });
  const auto digest = unhex(vector.at("transcript_sha256"), 32);
  const auto session = unhex(vector.at("session_id"), 16);
  const auto request_key = unhex(vector.at("request_key_hex"), 32);
  std::copy(digest.begin(), digest.end(), client.transcript_.begin());
  std::copy(session.begin(), session.end(), client.session_.begin());
  std::copy(request_key.begin(), request_key.end(),
            client.request_key_.begin());
  const auto packet = client.seal(vector.at("request_plain").get<std::string>(),
                                  client.request_key_, 1);
  require(hex({reinterpret_cast<const unsigned char *>(packet.data()),
               packet.size()}) == vector.at("request_hex").get<std::string>());
  std::array<unsigned char, 32> private_key{};
  for (unsigned i = 0; i < 32; ++i)
    private_key[i] = i + 65;
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
      EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, private_key.data(),
                                   private_key.size()),
      EVP_PKEY_free);
  wipe(private_key);
  const auto peer = unb64(vector.at("pico_public_key"));
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> remote(
      EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peer.data(),
                                  peer.size()),
      EVP_PKEY_free);
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> agreement(
      EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free);
  Secrets secrets;
  std::size_t size = 32;
  require(agreement && remote && EVP_PKEY_derive_init(agreement.get()) == 1 &&
          EVP_PKEY_derive_set_peer(agreement.get(), remote.get()) == 1 &&
          EVP_PKEY_derive(agreement.get(), secrets.secret.data(), &size) == 1 &&
          size == 32);
  const auto password = vector.at("password").get<std::string>();
  secrets.password =
      hash({reinterpret_cast<const unsigned char *>(password.data()),
            password.size()});
  std::copy(secrets.password.begin(), secrets.password.end(),
            secrets.secret.begin() + 32);
  derive(secrets.secret, client.transcript_, "WsprryPico radio login v1",
         secrets.login);
  derive(secrets.secret, client.transcript_, "WsprryPico radio traffic v1",
         secrets.traffic);
  require(std::equal(secrets.traffic.begin(), secrets.traffic.begin() + 32,
                     request_key.begin()));
  const auto login = client.seal(
      {reinterpret_cast<const char *>(digest.data()), 32}, secrets.login, 0);
  require(hex({reinterpret_cast<const unsigned char *>(login.data()),
               login.size()}) == vector.at("login_hex").get<std::string>());
  std::copy_n(secrets.traffic.begin() + 32, 32, client.response_key_.begin());
  const auto response =
      unhex(vector.at("response_hex"),
            vector.at("response_hex").get<std::string>().size() / 2);
  auto opened = client.open(
      {reinterpret_cast<const char *>(response.data()), response.size()}, 1);
  require(opened && *opened == vector.at("response_plain").get<std::string>());
  require(!client.open(
      {reinterpret_cast<const char *>(response.data()), response.size()}, 2));
  auto bad = response;
  bad.back() ^= 1;
  require(!client.open({reinterpret_cast<const char *>(bad.data()), bad.size()},
                       1));
}
#endif
PicoHttpResponse
consumer_http_request(PlainTcpStream &stream, TlsStream::Clock clock,
                      const std::string &host, const std::string &path,
                      const std::string &method, const std::string &body,
                      const std::string &media) {
  const auto identity = canonical_network_identity(host);
  if (!identity || *identity != host || host.find(':') != std::string::npos ||
      (method != "GET" && method != "POST") || body.size() > 32768 ||
      (path != "/api/consumer/v1/identity" &&
       path != "/api/consumer/v1/start" && path != "/api/consumer/v1/finish" &&
       path != "/api/consumer/v1/request"))
    return error(400, "invalid_consumer_endpoint");
  if (!stream.begin_open(host, 80))
    return error(503, "network_unavailable");
  struct Close {
    PlainTcpStream &stream;
    ~Close() { stream.close(); }
  } close{stream};
  const auto deadline = clock() + 8000;
  while (stream.opening() && clock() < deadline) {
    stream.poll_open();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  if (!stream.ready())
    return error(503, "network_unavailable");
  std::string request = method + " " + path + " HTTP/1.1\r\nHost: " + host +
                        "\r\nConnection: close\r\n";
  if (method == "POST")
    request += "Origin: http://" + host +
               "\r\nX-WsprryPico-Radio: 1\r\nContent-Type: " + media +
               "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
  request += "\r\n" + body;
  return wtp_json_http_exchange(stream, clock, request, deadline,
                                method == "POST", true);
}
} // namespace wsprrypi
