// SPDX-License-Identifier: MIT
#include "udp_logging/io.hpp"
#include "udp_logging/network_policy.hpp"
#include "udp_logging/resource_limits.hpp"
#include "udp_logging/udp_event_policy.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <net/if.h>
#include <openssl/sha.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-journal.h>
#include <thread>
using namespace wsprrypi::udp;
namespace {
volatile std::sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }
std::uint64_t monotonic() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
std::uint64_t receipt() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
std::string self_id() {
  auto s = read_file("/etc/machine-id", 128, 0);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
    s.pop_back();
  require(wsprry::logging::hex_id(s), "udp_machine_identity");
  s = "wsprrypi-wtp-device-v1:" + s;
  unsigned char digest[32];
  SHA256(reinterpret_cast<const unsigned char *>(s.data()), s.size(), digest);
  constexpr char hex[] = "0123456789abcdef";
  std::string id;
  for (unsigned i = 0; i < 16; ++i) {
    id += hex[digest[i] >> 4];
    id += hex[digest[i] & 15];
  }
  return id;
}
struct Bucket {
  double tokens = 0;
  std::uint64_t updated = 0;
  bool take(std::uint64_t now, unsigned rate, unsigned burst) {
    if (!updated || now < updated)
      tokens = burst;
    else
      tokens =
          std::min<double>(burst, tokens + double(now - updated) / 1e9 * rate);
    updated = now;
    if (tokens < 1)
      return false;
    --tokens;
    return true;
  }
};
std::string journal_field(sd_journal *journal, const char *key,
                          std::size_t maximum = 256) {
  const void *data{};
  std::size_t length{};
  if (sd_journal_get_data(journal, key, &data, &length) < 0)
    return {};
  const auto prefix = std::char_traits<char>::length(key) + 1;
  if (length < prefix || length - prefix > maximum)
    return {};
  return std::string(static_cast<const char *>(data) + prefix, length - prefix);
}
bool journal_event(sd_journal *journal, const Json &policy,
                   const std::string &self, wsprry::logging::UdpEvent &e) {
  if (journal_field(journal, "_SYSTEMD_UNIT") != "wsprrypi.service" ||
      journal_field(journal, "WSPRRY_COLLECTOR_ID") != self ||
      journal_field(journal, "WSPRRY_FORWARDERS") != "[]")
    return false;
  const auto ingress = journal_field(journal, "WSPRRY_INGRESS");
  e = {};
  const auto kind = journal_field(journal, "WSPRRY_ORIGIN_KIND");
  if (kind != "pico" && kind != "pi")
    return false;
  e.pico = kind == "pico";
  if (ingress == "native") {
    if (e.pico || !policy["sender"]["native_enabled"].get<bool>() ||
        journal_field(journal, "WSPRRY_ORIGIN_ID") != self)
      return false;
    const auto event = journal_field(journal, "WSPRRY_EXPORT_EVENT", 32);
    const auto approved = journal_field(journal, "WSPRRY_EXPORT_MESSAGE", 192);
    if (!wsprry::logging::export_safe_pi(event, approved))
      return false;
    e.code.assign(event);
    e.message.assign(approved);
  } else if (ingress == "usb") {
    if (!e.pico)
      return false;
    bool selected = false;
    for (const auto &source : policy["sender"]["usb_sources"])
      selected |=
          source["device_id"] == journal_field(journal, "WSPRRY_ORIGIN_ID") &&
          source["profile_id"] == journal_field(journal, "WSPRRY_PROFILE_ID");
    if (!selected)
      return false;
    const auto code = journal_field(journal, "WSPRRY_EVENT", 32),
               message = journal_field(journal, "MESSAGE", 192);
    const bool safe = wsprry::logging::export_safe_pico(code, message);
    if (!safe)
      return false;
    e.code.assign(code);
    e.message.assign(message);
    e.relayed = true;
    e.relay_id.assign(self);
    if (!wsprry::logging::decimal(journal_field(journal, "WSPRRY_RECEIPT_US"),
                                  e.relay_receipt_us))
      return false;
  } else
    return false; // UDP imports and worker diagnostics never forward.
  if (!e.origin.assign(journal_field(journal, "WSPRRY_ORIGIN_ID", 32)) ||
      !e.boot.assign(journal_field(journal, "WSPRRY_ORIGIN_BOOT", 32)) ||
      !e.hostname.assign(journal_field(journal, "WSPRRY_ORIGIN_NAME", 63)))
    return false;
  std::uint64_t severity{};
  if (!wsprry::logging::decimal(journal_field(journal, "PRIORITY"), severity) ||
      severity > 7)
    return false;
  e.severity = static_cast<unsigned>(severity);
  if (!wsprry::logging::decimal(
          journal_field(journal, "WSPRRY_ORIGIN_SEQUENCE"), e.sequence) ||
      !wsprry::logging::decimal(
          journal_field(journal, "WSPRRY_ORIGIN_MONOTONIC_NS"), e.monotonic_ns))
    return false;
  const auto time = journal_field(journal, "WSPRRY_ORIGIN_UTC_NS");
  std::uint64_t utc{};
  if (!time.empty()) {
    if (!wsprry::logging::decimal(time, utc))
      return false;
    e.utc_ns = utc;
  }
  const auto dropped = journal_field(journal, "WSPRRY_DROPPED");
  if (!dropped.empty() && !wsprry::logging::decimal(dropped, e.dropped))
    return false;
  return wsprry::logging::valid(e);
}
struct Pending {
  std::string packet;
  std::uint64_t accepted;
  unsigned peer_port = 0;
};
struct Peer {
  std::string slot, ip;
  Bucket bucket;
  std::deque<Pending> queue;
  struct Seen {
    std::string key, packet;
    std::uint64_t expires;
  };
  std::deque<Seen> seen;
  std::uint64_t received = 0, last = 0;
};
class Worker {
  std::string ini_, self_, revision_;
  Json policy_, active_;
  bool policy_valid_ = false;
  std::uint64_t duplicate_identity_notice_at_ = 0;
  sd_journal *journal_ = nullptr;
  int receiver_ = -1, sender_ = -1, activation_ = -1;
  std::optional<in_addr> sender_address_;
  std::string sender_interface_, network_generation_;
  std::deque<Pending> outgoing_;
  std::vector<Peer> peers_;
  Bucket send_bucket_, journal_bucket_;
  std::uint64_t reload_at_ = 0, status_at_ = 0;
  std::size_t next_peer_ = 0;
  void close_sockets() {
    if (receiver_ >= 0)
      ::close(receiver_);
    if (sender_ >= 0)
      ::close(sender_);
    receiver_ = sender_ = -1;
    sender_address_.reset();
  }
  void reset_active() {
    active_ = {{"applied_revision", nullptr},
               {"sender",
                {{"state", "disabled"},
                 {"receipt", "unknown"},
                 {"queued", 0},
                 {"counters", counters()}}},
               {"collector",
                {{"state", "disabled"},
                 {"bind_ipv4", nullptr},
                 {"port", nullptr},
                 {"counters", collector_counters()},
                 {"peers", Json::array()}}}};
  }
  void reload() {
    try {
      const auto bytes = read_file(ini_ + ".udp-logs.json", 16384, 0, true);
      Json p;
      if (bytes.empty()) {
        p = defaults();
        p["schema"] = "host-udp-logging/1";
        p["collector_id"] = self_;
        p["revision"] = "disabled";
      } else
        p = parse(bytes);
      require(fields(p, {"schema", "collector_id", "revision", "sender",
                         "collector"}) &&
                  p["schema"] == "host-udp-logging/1" &&
                  p["collector_id"] == self_ && text(p["revision"], 128),
              "udp_policy_identity");
      settings({{"sender", p["sender"]}, {"collector", p["collector"]}});
      policy_valid_ = true;
      if (p["revision"] == revision_)
        return;
      auto previous_sender = active_["sender"]["counters"];
      const auto previous_collector = active_["collector"]["counters"];
      for (std::size_t i = 0; i < outgoing_.size(); ++i)
        count(previous_sender, "reconfigured_drops");
      close_sockets();
      outgoing_.clear();
      peers_.clear();
      policy_ = p;
      revision_ = p["revision"].get<std::string>();
      // A settings revision starts at the current tail. Disabled-period
      // events and old filter/destination selections are never caught up.
      if (journal_) {
        sd_journal_seek_tail(journal_);
        sd_journal_previous(journal_);
      }
      reset_active();
      active_["sender"]["counters"] = std::move(previous_sender);
      active_["collector"]["counters"] = previous_collector;
      active_["applied_revision"] = revision_;
      for (const auto &value : p["collector"]["peers"]) {
        Peer peer;
        peer.slot = value["slot"];
        peer.ip = value["ipv4"];
        peers_.push_back(std::move(peer));
      }
      send_bucket_ = {};
      journal_bucket_ = {};
      if (journal_) {
        sd_journal_seek_tail(journal_);
        sd_journal_previous(journal_);
      }
    } catch (...) {
      policy_valid_ = false;
      close_sockets();
      outgoing_.clear();
      peers_.clear();
      policy_ = defaults();
      revision_.clear();
      reset_active();
      active_["collector"]["state"] = "policy_invalid";
      active_["sender"]["state"] = "unavailable";
    }
  }
  void listen() {
    if (!policy_valid_)
      return;
    auto &a = active_["collector"];
    const auto &c = policy_["collector"];
    if (!c["enabled"].get<bool>()) {
      a["state"] = "disabled";
      return;
    }
    const auto address = interface_address(c["interface"].get<std::string>());
    if (!address) {
      if (receiver_ >= 0)
        ::close(receiver_);
      receiver_ = -1;
      a["state"] = "awaiting_network";
      a["bind_ipv4"] = nullptr;
      a["port"] = nullptr;
      return;
    }
    in_addr wanted{};
    inet_pton(AF_INET, c["bind_ipv4"].get<std::string>().c_str(), &wanted);
    if (address->s_addr != wanted.s_addr) {
      if (receiver_ >= 0)
        ::close(receiver_);
      receiver_ = -1;
      for (auto &peer : peers_)
        peer.queue.clear();
      a["bind_ipv4"] = nullptr;
      a["port"] = nullptr;
      a["state"] = "awaiting_network";
      return;
    }
    if (receiver_ >= 0)
      return;
    const auto port = c["port"].get<unsigned>();
    if (port == 514) {
      const auto *socket_revision = std::getenv("WSPRRY_UDP_SOCKET_REVISION");
      if (!socket_revision || revision_ != socket_revision) {
        a["state"] = "unavailable";
        return;
      }
      if (activation_ >= 0) {
        sockaddr_in bound{};
        socklen_t length = sizeof(bound);
        int kind{};
        socklen_t kind_length = sizeof(kind);
        if (getsockname(activation_, reinterpret_cast<sockaddr *>(&bound),
                        &length) == 0 &&
            bound.sin_family == AF_INET &&
            bound.sin_addr.s_addr == wanted.s_addr &&
            ntohs(bound.sin_port) == port &&
            getsockopt(activation_, SOL_SOCKET, SO_TYPE, &kind, &kind_length) ==
                0 &&
            kind == SOCK_DGRAM)
          receiver_ = dup(activation_);
      }
    } else {
      receiver_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
      if (receiver_ >= 0) {
        sockaddr_in bind_to{};
        bind_to.sin_family = AF_INET;
        bind_to.sin_addr = wanted;
        bind_to.sin_port = htons(static_cast<std::uint16_t>(port));
        if (bind(receiver_, reinterpret_cast<sockaddr *>(&bind_to),
                 sizeof(bind_to))) {
          ::close(receiver_);
          receiver_ = -1;
        }
      }
    }
    if (receiver_ < 0) {
      a["state"] = "bind_failed";
      return;
    }
    fcntl(receiver_, F_SETFL, fcntl(receiver_, F_GETFL) | O_NONBLOCK);
    int packet_info = 1;
    if (setsockopt(receiver_, IPPROTO_IP, IP_PKTINFO, &packet_info,
                   sizeof(packet_info))) {
      ::close(receiver_);
      receiver_ = -1;
      a["state"] = "unavailable";
      return;
    }
    fcntl(receiver_, F_SETFD, fcntl(receiver_, F_GETFD) | FD_CLOEXEC);
    // Linux doubles SO_RCVBUF for accounting; verify the actual 64 KiB budget.
    int budget = 32768, actual = 0;
    socklen_t length = sizeof(actual);
    if (setsockopt(receiver_, SOL_SOCKET, SO_RCVBUF, &budget, sizeof(budget)) ||
        getsockopt(receiver_, SOL_SOCKET, SO_RCVBUF, &actual, &length) ||
        actual != 65536) {
      ::close(receiver_);
      receiver_ = -1;
      a["state"] = "unavailable";
      return;
    }
    a["state"] = "ready";
    a["bind_ipv4"] = c["bind_ipv4"];
    a["port"] = port;
  }
  void receive(std::uint64_t now) {
    if (receiver_ < 0)
      return;
    auto &c = active_["collector"]["counters"];
    for (unsigned work = 0; work < 32; ++work) {
      std::array<char, 481> bytes{};
      sockaddr_in address{};
      iovec vector{bytes.data(), bytes.size()};
      std::array<char, CMSG_SPACE(sizeof(in_pktinfo))> ancillary{};
      msghdr msg{};
      msg.msg_control = ancillary.data();
      msg.msg_controllen = ancillary.size();
      msg.msg_name = &address;
      msg.msg_namelen = sizeof(address);
      msg.msg_iov = &vector;
      msg.msg_iovlen = 1;
      const auto n = recvmsg(receiver_, &msg, MSG_DONTWAIT | MSG_TRUNC);
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          break;
        count(c, "malformed");
        break;
      }
      count(c, "received");
      bool correct_interface = false;
      const auto expected_index = if_nametoindex(
          policy_["collector"]["interface"].get<std::string>().c_str());
      for (auto *header = CMSG_FIRSTHDR(&msg); header;
           header = CMSG_NXTHDR(&msg, header))
        if (header->cmsg_level == IPPROTO_IP &&
            header->cmsg_type == IP_PKTINFO &&
            header->cmsg_len >= CMSG_LEN(sizeof(in_pktinfo)))
          correct_interface =
              static_cast<unsigned>(
                  reinterpret_cast<in_pktinfo *>(CMSG_DATA(header))
                      ->ipi_ifindex) == expected_index;
      if (!correct_interface || (msg.msg_flags & MSG_CTRUNC)) {
        count(c, "disallowed");
        continue;
      }
      char observed[INET_ADDRSTRLEN]{};
      inet_ntop(AF_INET, &address.sin_addr, observed, sizeof(observed));
      auto peer = std::find_if(peers_.begin(), peers_.end(),
                               [&](const auto &p) { return p.ip == observed; });
      if (peer == peers_.end()) {
        count(c, "disallowed");
        continue;
      }
      if ((msg.msg_flags & MSG_TRUNC) || n > 480) {
        count(c, "oversize");
        continue;
      }
      if (n == 0) {
        count(c, "malformed");
        continue;
      }
      if (!peer->bucket.take(now, 8, 16)) {
        count(c, "limited");
        continue;
      }
      if (peer->received != UINT64_MAX)
        ++peer->received;
      peer->last = receipt();
      if (peer->queue.size() == 8) {
        count(c, "overflow");
        continue;
      }
      peer->queue.push_back(
          {std::string(bytes.data(), static_cast<std::size_t>(n)), now,
           ntohs(address.sin_port)});
    }
    for (unsigned work = 0; work < 32 && !peers_.empty(); ++work) {
      auto &peer = peers_[next_peer_++ % peers_.size()];
      if (peer.queue.empty())
        continue;
      auto packet = std::move(peer.queue.front());
      peer.queue.pop_front();
      if (now - packet.accepted >= 5'000'000'000ULL) {
        count(c, "limited");
        continue;
      }
      wsprry::logging::UdpEvent e;
      if (!wsprry::logging::decode_udp(packet.packet, e, self_)) {
        count(c, "malformed");
        continue;
      }
      while (!peer.seen.empty() && peer.seen.front().expires <= now)
        peer.seen.pop_front();
      const auto key =
          std::string(e.pico ? "pico:" : "pi:") + std::string(e.origin.view()) +
          ":" + std::string(e.boot.view()) + ":" + std::to_string(e.sequence) +
          (e.relayed ? ":relay" : ":direct");
      if (now >= duplicate_identity_notice_at_) {
        bool duplicate_identity = false;
        for (const auto &other : peers_)
          if (other.slot != peer.slot)
            for (const auto &seen : other.seen)
              duplicate_identity |=
                  seen.expires > now && seen.key.substr(seen.key.find(':') + 1,
                                                        32) == e.origin.view();
        if (duplicate_identity) {
          std::fputs("UDP origin identity claimed by multiple configured "
                     "peers; arrival scopes remain separate\n",
                     stderr);
          duplicate_identity_notice_at_ = now + 60'000'000'000ULL;
        }
      }
      const auto old =
          std::find_if(peer.seen.begin(), peer.seen.end(),
                       [&](const auto &s) { return s.key == key; });
      if (old != peer.seen.end()) {
        count(c, old->packet == packet.packet ? "duplicate" : "conflict");
        continue;
      }
      if (!journal_bucket_.take(now, 128, 256)) {
        count(c, "limited");
        continue;
      }
      const auto relay =
          e.relayed ? Json::array(
                          {{{"id", e.relay_id.view()},
                            {"receipt_us", std::to_string(e.relay_receipt_us)},
                            {"ingress", "usb"}}})
                          .dump()
                    : "[]";
      const auto time = e.utc_ns ? std::to_string(*e.utc_ns) : std::string{};
      const auto submitted = sd_journal_send(
          "MESSAGE=%s", e.message.bytes.data(), "PRIORITY=%u", e.severity,
          "SYSLOG_FACILITY=23", "SYSLOG_IDENTIFIER=wsprrypi-log",
          "WSPRRY_LOG_SCHEMA=wsprry-log/1", "WSPRRY_ORIGIN_KIND=%s",
          e.pico ? "pico" : "pi", "WSPRRY_ORIGIN_ID=%s", e.origin.bytes.data(),
          "WSPRRY_ORIGIN_BOOT=%s", e.boot.bytes.data(), "WSPRRY_ORIGIN_NAME=%s",
          e.hostname.bytes.data(), "WSPRRY_ORIGIN_SEQUENCE=%s",
          std::to_string(e.sequence).c_str(), "WSPRRY_ORIGIN_MONOTONIC_NS=%s",
          std::to_string(e.monotonic_ns).c_str(), "WSPRRY_ORIGIN_UTC_NS=%s",
          time.c_str(), "WSPRRY_DROPPED=%s", std::to_string(e.dropped).c_str(),
          "WSPRRY_EVENT=%s", e.code.bytes.data(), "WSPRRY_COLLECTOR_ID=%s",
          self_.c_str(), "WSPRRY_INGRESS=udp", "WSPRRY_PEER_SLOT=%s",
          peer.slot.c_str(), "WSPRRY_PEER_IPV4=%s", peer.ip.c_str(),
          "WSPRRY_PEER_PORT=%u", packet.peer_port, "WSPRRY_RECEIPT_US=%s",
          std::to_string(receipt()).c_str(), "WSPRRY_FORWARDERS=%s",
          relay.c_str(), nullptr);
      if (submitted < 0) {
        count(c, "journal_failed");
        continue;
      }
      count(c, "journal_submitted");
      if (peer.seen.size() == 64)
        peer.seen.pop_front();
      peer.seen.push_back(
          {key, std::move(packet.packet), now + 60'000'000'000ULL});
    }
  }
  void forward(std::uint64_t now) {
    if (!policy_valid_)
      return;
    auto &a = active_["sender"];
    auto &c = a["counters"];
    const auto &s = policy_["sender"];
    while (!outgoing_.empty() &&
           now - outgoing_.front().accepted >= 5'000'000'000ULL) {
      outgoing_.pop_front();
      count(c, "expired");
    }
    a["queued"] = outgoing_.size();
    if (journal_) {
      const auto change = sd_journal_process(journal_);
      if (change == SD_JOURNAL_INVALIDATE) {
        count(active_["collector"]["counters"], "discontinuity");
        sd_journal_seek_tail(journal_);
        sd_journal_previous(journal_);
      }
    }
    if (!s["enabled"].get<bool>()) {
      if (journal_) {
        sd_journal_seek_tail(journal_);
        sd_journal_previous(journal_);
      }
      a["state"] = "disabled";
      return;
    }
    if (!journal_ || access("/run/systemd/journal/socket", W_OK) != 0) {
      a["state"] = "unavailable";
      return;
    }
    const auto iface = s["interface"].get<std::string>();
    const auto destination = ipv4(s["destination_ipv4"].get<std::string>());
    const auto inventory = interface_inventory(iface, destination);
    auto address = inventory.valid && inventory.destination_allowed
                       ? inventory.address
                       : std::nullopt;
    std::array<char, 65536> route_bytes{};
    std::ifstream routes("/proc/net/route");
    routes.read(route_bytes.data(), route_bytes.size());
    const auto route_identity = route_generation(
        std::string_view(route_bytes.data(), routes.gcount()), iface);
    if (!routes.eof() || !route_identity)
      address.reset();
    std::string generation = route_identity.value_or("unavailable");
    if (address)
      generation += std::to_string(address->s_addr);
    generation += iface;
    const bool generation_changed = network_generation_ != generation;
    network_generation_ = std::move(generation);
    if (sender_address_ && (generation_changed || !address ||
                            address->s_addr != sender_address_->s_addr ||
                            iface != sender_interface_)) {
      for (std::size_t i = 0; i < outgoing_.size(); ++i)
        count(c, "reconfigured_drops");
      outgoing_.clear();
      ::close(sender_);
      sender_ = -1;
      sender_address_.reset();
    }
    if (!address) {
      a["state"] = !inventory.valid     ? "unavailable"
                   : !inventory.address ? "awaiting_network"
                                        : "invalid_destination";
    } // No fallback to another interface or destination.
    else if (sender_ < 0) {
      sender_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
      if (sender_ >= 0) {
        sockaddr_in source{};
        source.sin_family = AF_INET;
        source.sin_addr = *address;
        const auto selected_index = htonl(if_nametoindex(iface.c_str()));
        int discover = IP_PMTUDISC_DO;
        if (!selected_index ||
            setsockopt(sender_, IPPROTO_IP, IP_UNICAST_IF, &selected_index,
                       sizeof(selected_index)) ||
            setsockopt(sender_, IPPROTO_IP, IP_MTU_DISCOVER, &discover,
                       sizeof(discover)) ||
            bind(sender_, reinterpret_cast<sockaddr *>(&source),
                 sizeof(source))) {
          ::close(sender_);
          sender_ = -1;
        }
      }
      if (sender_ >= 0) {
        sender_address_ = address;
        sender_interface_ = iface;
        a["state"] = "ready";
      } else
        a["state"] = "unavailable";
    }
    for (unsigned work = 0; work < 32; ++work) {
      const auto n = sd_journal_next(journal_);
      if (n <= 0)
        break;
      count(c, "generated");
      std::uint64_t entry_time{};
      const auto wall = receipt();
      if (sd_journal_get_realtime_usec(journal_, &entry_time) < 0 ||
          entry_time > wall || wall - entry_time >= 5'000'000ULL) {
        count(c, "expired");
        continue;
      }
      wsprry::logging::UdpEvent e;
      if (!journal_event(journal_, policy_, self_, e)) {
        count(c, "redacted_rejected");
        continue;
      }
      if (e.severity > s["maximum_severity"].get<unsigned>() ||
          (!s["event_codes"].empty() &&
           std::find(s["event_codes"].begin(), s["event_codes"].end(),
                     std::string(e.code.view())) == s["event_codes"].end())) {
        count(c, "filtered");
        continue;
      }
      if (outgoing_.size() == 256) {
        count(c, "overflow");
        continue;
      }
      std::array<char, 480> buffer{};
      bool truncated{};
      const auto packet = wsprry::logging::encode_udp(e, buffer, truncated);
      if (packet.empty()) {
        count(c, "redacted_rejected");
        continue;
      }
      if (truncated)
        count(c, "truncated");
      outgoing_.push_back({std::string(packet), now});
      count(c, "queued");
    }
    while (!outgoing_.empty() &&
           now - outgoing_.front().accepted >= 5'000'000'000ULL) {
      outgoing_.pop_front();
      count(c, "expired");
    }
    for (unsigned work = 0; work < 16 && sender_ >= 0 && !outgoing_.empty();
         ++work) {
      if (!send_bucket_.take(now, s["rate"].get<unsigned>(),
                             s["burst"].get<unsigned>()))
        break;
      auto packet = std::move(outgoing_.front());
      outgoing_.pop_front();
      count(c, "attempted");
      sockaddr_in target{};
      target.sin_family = AF_INET;
      target.sin_port = htons(s["destination_port"].get<std::uint16_t>());
      inet_pton(AF_INET, s["destination_ipv4"].get<std::string>().c_str(),
                &target.sin_addr);
      const auto n = sendto(sender_, packet.packet.data(), packet.packet.size(),
                            MSG_DONTWAIT, reinterpret_cast<sockaddr *>(&target),
                            sizeof(target));
      if (n == static_cast<ssize_t>(packet.packet.size())) {
        count(c, "locally_submitted");
        a["state"] = "ready";
      } else {
        count(c, "locally_failed");
        a["state"] = "submission_failed";
      }
    }
    a["queued"] = outgoing_.size();
  }

public:
  explicit Worker(std::string ini)
      : ini_(std::move(ini)), self_(self_id()), policy_(defaults()) {
    require(getuid() != 0, "UDP worker refuses root execution");
    reset_active();
    const auto descriptors = sd_listen_fds(1);
    if (descriptors == 1)
      activation_ = SD_LISTEN_FDS_START;
    else
      require(descriptors == 0, "udp_socket_inventory");
    if (sd_journal_open(&journal_, SD_JOURNAL_LOCAL_ONLY) >= 0) {
      // Arm the journal notification watches before bounded polling. Without
      // get_fd(), process() cannot discover replacement files after rotation.
      if (sd_journal_add_match(journal_, "_SYSTEMD_UNIT=wsprrypi.service", 0) <
              0 ||
          sd_journal_get_fd(journal_) < 0) {
        sd_journal_close(journal_);
        journal_ = nullptr;
        return;
      }
      sd_journal_set_data_threshold(journal_, 4096);
      sd_journal_seek_tail(journal_);
      sd_journal_previous(journal_);
    } else
      journal_ = nullptr;
  }
  ~Worker() {
    close_sockets();
    if (activation_ >= 0)
      ::close(activation_);
    if (journal_)
      sd_journal_close(journal_);
  }
  void run() {
    while (!stopping) {
      const auto now = monotonic();
      if (now >= reload_at_) {
        reload();
        reload_at_ = now + 1'000'000'000ULL;
      }
      listen();
      receive(now);
      forward(now);
      if (now >= status_at_) {
        Json peers = Json::array();
        for (const auto &peer : peers_)
          peers.push_back(
              {{"slot", peer.slot},
               {"received", std::to_string(peer.received)},
               {"last_receipt_us",
                peer.last ? Json(std::to_string(peer.last)) : Json(nullptr)}});
        active_["collector"]["peers"] = std::move(peers);
        try {
          atomic_file("/run/wsprrypi-log/status.json",
                      Json{{"schema", "host-udp-logging-status/1"},
                           {"collector_id", self_},
                           {"active", active_}}
                          .dump(),
                      0644);
        } catch (...) {
        }
        status_at_ = now + 1'000'000'000ULL;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
};
} // namespace
int main(int argc, char **argv) {
  if (argc != 3 || std::string_view(argv[1]) != "--ini")
    return 2;
  std::signal(SIGTERM, stop);
  std::signal(SIGINT, stop);
  try {
    require(::getuid() != 0 && resource_limits_enforced(),
            "udp_resource_limits_unavailable");
    Worker worker(argv[2]);
    worker.run();
    return 0;
  } catch (...) {
    std::fputs("UDP logging unavailable: unprivileged execution, enforced "
               "resource limits and valid policy are required\n",
               stderr);
    return 1;
  }
}
