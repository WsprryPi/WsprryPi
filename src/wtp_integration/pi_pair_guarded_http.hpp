// SPDX-License-Identifier: MIT
#pragma once
#include "native_socket.hpp"
#include "httplib.hpp"
#include "pi_pair_admission.hpp"
namespace wsprrypi {
// Parent-owned adapter through httplib's existing Stream/Server extension
// points. No vendored library modification. New pairing paths get absolute
// framing/I/O bounds and the same shared admission as the TLS listener.
class PiPairGuardedHttpServer final:public httplib::Server {
public:
    using Clock=std::function<std::chrono::steady_clock::time_point()>;
    explicit PiPairGuardedHttpServer(PiPairAdmission &,Clock=[] {return std::chrono::steady_clock::now();});
private:
    bool process_and_close_socket(socket_t) override;
    PiPairAdmission &admission_;
    Clock clock_;
};
} // namespace wsprrypi
