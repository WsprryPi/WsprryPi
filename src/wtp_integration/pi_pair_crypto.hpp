// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <array>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
namespace wsprrypi {
enum class PiPairCryptoFailure {Busy,InvalidShape,InvalidProof,Unavailable};
class PiPairCryptoError final:public std::runtime_error {
public:
    explicit PiPairCryptoError(PiPairCryptoFailure code):std::runtime_error("pairing_crypto_failed"),code_(code){}
    PiPairCryptoFailure code() const noexcept{return code_;}
private:PiPairCryptoFailure code_;
};
// Internal secret values have no JSON or stream conversion. Explicit byte and
// storage access belongs to the private transport/store owners, never APIs.
class PiPairSecret {
public:
    explicit PiPairSecret(std::span<const unsigned char>);
    ~PiPairSecret();
    PiPairSecret(PiPairSecret &&) noexcept;
    PiPairSecret &operator=(PiPairSecret &&) noexcept;
    PiPairSecret(const PiPairSecret &)=delete;
    PiPairSecret &operator=(const PiPairSecret &)=delete;
    std::span<const unsigned char,32> bytes() const noexcept{return value_;}
    std::string storage_encoding() const;
    static PiPairSecret from_storage(std::string_view);
private:std::array<unsigned char,32> value_{};
};
class PiPairCode {
public:
    static PiPairCode generate();
    static PiPairCode normalize(std::string_view);
    ~PiPairCode();
    PiPairCode(PiPairCode &&) noexcept;
    PiPairCode &operator=(PiPairCode &&) noexcept;
    PiPairCode(const PiPairCode &)=delete;
    PiPairCode &operator=(const PiPairCode &)=delete;
    std::string display() const;
    std::span<const unsigned char,8> bytes() const noexcept{return value_;}
private:
    explicit PiPairCode(std::array<unsigned char,8>);
    std::array<unsigned char,8> value_{};
};
struct PiPairKeys {
    PiPairSecret tls,commit,retirement;
};
// Context construction validates the exact hello/challenge before derivation.
// The owning window must additionally bind its actual identities, boots, offer,
// generation, monotonic deadline, failure budget, and originating browser.
class PiPairExchange {
public:
    enum class Role {Controller,Target};
    PiPairExchange(Role,const PiPairCode &,const nlohmann::json &hello,const nlohmann::json &challenge);
    ~PiPairExchange();
    PiPairExchange(PiPairExchange &&) noexcept;
    PiPairExchange &operator=(PiPairExchange &&) noexcept;
    PiPairExchange(const PiPairExchange &)=delete;
    nlohmann::json start_controller();
    nlohmann::json target_exchange(const nlohmann::json &);
    nlohmann::json controller_confirmation(const nlohmann::json &);
    nlohmann::json target_confirmation(const nlohmann::json &);
    void controller_confirmed(const nlohmann::json &);
    PiPairKeys keys() const;
    const std::string &request_digest() const;
    // Public transcript binding available only after actual controller-side
    // mutual confirmation. It contains no code, derived key or registration.
    const nlohmann::json &confirmed_controller_hello() const;
    // Uses only public group operations; never imports a registration record.
    static bool valid_peer_point(std::string_view);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string pi_pair_mac(const PiPairSecret &,std::string_view);
bool pi_pair_verify_mac(const PiPairSecret &,std::string_view,std::string_view);
namespace testing {
// Typed host-fixture checkpoint, never a CLI/INI/environment or UI selector.
// Runs while the real process-wide derivation slot is reserved. Replacement
// refuses while a derivation is active and never bypasses actual scrypt work.
void set_pi_pair_derivation_checkpoint(std::function<void()>);
}
} // namespace wsprrypi
