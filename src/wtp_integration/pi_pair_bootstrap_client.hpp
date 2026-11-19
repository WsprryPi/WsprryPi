// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_socket.hpp"
#include "pi_pair_http.hpp"
#include "pi_pair_admission.hpp"
#include "pi_pair_mutation_guard.hpp"
#include <optional>
namespace wsprrypi {
struct PiPairBootstrapResult {
    enum class State {Refused,Unavailable,Uncertain,Replied};
    State state{State::Refused};
    std::optional<PiPairHttpReply> reply;
};
// Explicit public-bootstrap transport only. The caller reserves one shared
// outgoing connection for its whole exchange and supplies its absolute attempt
// deadline. It cannot send a settings request, code field, cookie or key, and
// is never selected as fallback by the authenticated client.
class PiPairBootstrapClient {
public:
    using WriteAdmission=std::function<std::optional<PiPairMutationGuard>()>;
    explicit PiPairBootstrapClient(PiPairSocket::Access = PiPairSocket::Access::PrivateLan);
    PiPairBootstrapResult request(const PiPairEndpoint &,const std::string &resource,
        const nlohmann::json &body,PiPairAdmission::Connection &,PiPairSocket::Time attempt_deadline,
        const std::function<bool()> &cancelled=[] {return false;},const WriteAdmission & = {});
private:
    PiPairSocket::Access access_;
};
} // namespace wsprrypi
