// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/types.h>
namespace wsprrypi {
enum class LocalPeer {Administrator,WebService};
enum class LocalOperation {SettingsRead,SettingsWrite,DiagnosticRead,RouteRead,ListenerRead,EventsRead,BrowserOpen,BrowserMessage,HttpOpen,HttpBody,HttpDispatch,HttpRead,HttpComplete,WebBootstrap,WebAttach,WebHeartbeat,BrowserCheck,RecoveryCapture,RecoveryObserve,RecoveryValidate,RecoveryPrepare,RecoveryReview,RecoveryCommand,RecoverySessionIssue};
enum class LocalChannel {Control,Events,Browser,Http,WebService,Recovery};
struct LocalRequest {
    LocalOperation operation;
    nlohmann::json arguments;
    // Set only by the verified server transport; never read from JSON.
    std::string transport_connection{};
    int transport_descriptor{-1};
};
enum class LocalRefusal {InvalidRequest,RevisionConflict,Busy,SetupRequired,StorageUnavailable};
// The handler may use this only for an admission refusal before mutation.
class LocalCommandRefused:public std::runtime_error {
public:
    explicit LocalCommandRefused(LocalRefusal value):std::runtime_error("Local command refused before mutation"),value_(value){}
    LocalRefusal reason() const noexcept{return value_;}
private:LocalRefusal value_;
};
// Peers are derived from the connected OS socket, never request JSON.
struct LocalTransportOptions {
    std::string directory;
    uid_t backend_uid;
    uid_t web_uid;
    gid_t socket_group;
    LocalChannel channel{LocalChannel::Control};
};
class LocalControlError:public std::runtime_error {
public:
    enum class Code {Unavailable,Protocol,Unauthorized,UnknownOutcome};
    LocalControlError(Code);
    Code code() const noexcept{return code_;}
private:Code code_;
};
class LocalControlServer {
public:
    using Handler=std::function<nlohmann::json(LocalPeer,const LocalRequest &)>;
    using Disconnected=std::function<void(LocalPeer,const std::string &)>;
    LocalControlServer(LocalTransportOptions,std::string boot_id,Handler,Disconnected = {});
    ~LocalControlServer();
    LocalControlServer(const LocalControlServer &)=delete;
    LocalControlServer &operator=(const LocalControlServer &)=delete;
    void start();
    void stop() noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Browser sessions retain one authenticated OS connection. Sequence numbers
// advance exactly once; reconnect never replays an earlier command.
class LocalControlConnection {
public:
    explicit LocalControlConnection(const LocalTransportOptions &);
    ~LocalControlConnection();
    LocalControlConnection(const LocalControlConnection &)=delete;
    nlohmann::json request(const LocalRequest &);
    const std::string &boot() const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// One request per verified connection. No request, mutation, or Start replay.
nlohmann::json local_control_request(const LocalTransportOptions &,const LocalRequest &);
}
