// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_crypto.hpp"
#include "WTP-Client/include/wtp/transport.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
namespace wsprrypi {
enum class PiPairTlsPurpose {Management,Retirement};
struct PiPairTlsIdentity {
    PiPairTlsPurpose purpose;
    std::string pair_id,authorization_generation,epoch;
    bool operator==(const PiPairTlsIdentity &) const = default;
};
std::optional<PiPairTlsIdentity> parse_pi_pair_tls_identity(std::string_view);
std::string pi_pair_tls_identity(const PiPairTlsIdentity &);
// P62-04's fixed target-client/controller-server context. The caller resolves
// the exact retained verifier and checks its authority; this pure derivation
// grants none and never persists the resulting ephemeral transport key.
PiPairSecret pi_pair_retirement_tls_key(const PiPairSecret &verifier,const std::string &target_id,
    const std::string &controller_id,const std::string &pair_id,const std::string &authorization_generation);
// Internal credential snapshots come from the private owner. The server owner
// resolves incoming management only against its active manager, and retirement
// only against its outgoing retirement book. No browser supplies these values.
struct PiPairTlsCredential {
    PiPairTlsIdentity identity;
    std::string self_id,peer_id;
    PiPairSecret key;
};
class PiPairPrincipal {
public:
    const PiPairTlsIdentity &identity() const noexcept{return identity_;}
    const std::string &self_id() const noexcept{return self_;}
    const std::string &peer_id() const noexcept{return peer_;}
private:
    friend class PiPairTlsChannel;
    PiPairPrincipal(PiPairTlsIdentity identity,std::string self,std::string peer)
        :identity_(std::move(identity)),self_(std::move(self)),peer_(std::move(peer)){}
    PiPairTlsIdentity identity_;
    std::string self_,peer_;
};
// Low-level channel for an already connected/admitted socket. The caller owns
// LAN/address policy, shared connection/handshake capacity and HTTP bounds. This
// class owns the fd, performs no DNS/connect, and never selects a WTP backend.
// All methods except destruction belong to one worker. Cancellation is handled
// by that worker, never by closing another thread's possibly reused descriptor.
class PiPairTlsChannel final:public wtp::ByteStream {
public:
    using Lookup=std::function<std::shared_ptr<const PiPairTlsCredential>(const PiPairTlsIdentity &)>;
    enum class Handshake {Pending,Ready,Failed};
    enum class Failure {None,Transport,Deadline,Authentication,KeyExchange,EarlyData,Cipher,Purpose,Authorization};
    // On constructor failure the supplied descriptor is still closed.
    PiPairTlsChannel(int connected_fd,Lookup,std::optional<PiPairTlsIdentity> client = {});
    ~PiPairTlsChannel() override;
    PiPairTlsChannel(const PiPairTlsChannel &)=delete;
    Handshake handshake();
    // Handshake diagnostics only; subsequent I/O reports its own IoResult.
    Failure failure() const noexcept{return failure_;}
    // Numeric library reason only; never expose the raw OpenSSL error stack,
    // credential identifiers or request bytes through diagnostics.
    unsigned long library_reason() const noexcept{return library_reason_;}
    // A principal exists only after Finished, exact PSK-DHE and a fresh owner
    // lookup. It is a channel observation; every mutation must recheck the
    // generation/epoch under its owner's commit lock.
    const PiPairPrincipal *principal() const noexcept;
    wtp::IoResult read(std::span<std::uint8_t>) override;
    wtp::IoResult write(std::span<const std::uint8_t>) override;
    void close() noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::optional<PiPairPrincipal> principal_;
    Failure failure_{Failure::None};
    unsigned long library_reason_{};
};
} // namespace wsprrypi
