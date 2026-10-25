// SPDX-License-Identifier: MIT
#pragma once
#include "network_http.hpp"
#include "plain_tcp.hpp"
#include <array>
#include <functional>
#include <optional>
#include <span>
namespace wsprrypi {
// Volatile keys only. Caller owns the application admission/operation lock.
class ConsumerRadioClient {
public:
  using Transport =
      std::function<PicoHttpResponse(const std::string &, const std::string &,
                                     const std::string &, const std::string &)>;
  ConsumerRadioClient(Transport, TlsStream::Clock);
  ~ConsumerRadioClient();
  ConsumerRadioClient(const ConsumerRadioClient &) = delete;
  ConsumerRadioClient &operator=(const ConsumerRadioClient &) = delete;
  PicoHttpResponse login(const std::string &device, const std::string &carrier,
                         std::string_view password);
  PicoHttpResponse request(const std::string &resource,
                           const std::string &method,
                           const std::string &body = {},
                           const std::string &revision = {});
  PicoHttpResponse recovery(const std::string &device, const std::string &carrier,
                            const std::string &host);
  PicoHttpResponse retire(const std::string &device, const std::string &carrier,
                          const std::string &host, std::string_view password,
                          const nlohmann::json &scope);
  const nlohmann::json &lost_session() const { return lost_session_; }
  void logout();
  void clear() noexcept;
  bool active() const;
  const std::string &device() const { return device_; }
  const std::string &boot() const { return boot_; }
  const std::string &generation() const { return generation_; }

private:
#ifdef WSPRRYPI_CONSUMER_CRYPTO_TEST
  friend void consumer_radio_fixed_vectors(const nlohmann::json &);
#endif
  PicoHttpResponse handshake(const std::string &, const std::string &,
                             std::string_view, const nlohmann::json &);
  nlohmann::json lost_session_ = nullptr;
  Transport transport_;
  TlsStream::Clock clock_;
  std::string device_, boot_, generation_;
  std::array<unsigned char, 16> session_{};
  std::array<unsigned char, 32> transcript_{}, request_key_{}, response_key_{};
  std::uint64_t sequence_{}, started_{}, seen_{};
  // Conservative defaults until authenticated management discovery is checked.
  std::uint64_t idle_ms_{300000}, absolute_ms_{900000};
  bool authenticated_{};
  std::string seal(std::string_view, std::span<const unsigned char, 32>,
                   std::uint64_t);
  std::optional<std::string> open(std::string_view, std::uint64_t);
};
PicoHttpResponse consumer_http_request(PlainTcpStream &, TlsStream::Clock,
                                       const std::string &host,
                                       const std::string &path,
                                       const std::string &method,
                                       const std::string &body,
                                       const std::string &media);
#ifdef WSPRRYPI_CONSUMER_CRYPTO_TEST
void consumer_radio_fixed_vectors(const nlohmann::json &);
#endif
bool valid_management_contract(const nlohmann::json &,
                               const std::string &carrier,
                               const std::string &device,
                               const std::string &boot,
                               const std::string &generation = {});
bool valid_management_descriptor(const nlohmann::json &,
                                 const std::string &carrier);
} // namespace wsprrypi
