// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_crypto.hpp"
#include <cstdint>
namespace wsprrypi {
// Existing OpenSSL owns rotation EC generation, serialization and validation.
// Only the private continuation owner may persist exported material. No JSON,
// file/path, CLI, or browser-key provisioning interface is provided.
class PiPairRotationKey {
public:
    static PiPairRotationKey generate();
    static PiPairRotationKey restore(const PiPairSecret &,std::string_view own_public_point);
    static bool valid_private_pair(std::string_view private_key,std::string_view public_point) noexcept;
    static bool valid_public_point(std::string_view) noexcept;
    ~PiPairRotationKey();
    PiPairRotationKey(PiPairRotationKey &&) noexcept;
    PiPairRotationKey &operator=(PiPairRotationKey &&) noexcept;
    PiPairRotationKey(const PiPairRotationKey &)=delete;
    PiPairRotationKey &operator=(const PiPairRotationKey &)=delete;
    PiPairSecret export_private() const;
    std::string public_point() const;
    enum class Role {Controller,Target};
    // Caller authenticates the actual pair/generation and reserves its exact
    // operation. This checks crypto shape/epoch/role; it grants no authority.
    PiPairSecret derive_next(const PiPairSecret &current_psk,std::uint64_t current_epoch,
        const nlohmann::json &prepare,const nlohmann::json &prepared,Role) const;
private:
    struct Impl;
    explicit PiPairRotationKey(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
} // namespace wsprrypi
