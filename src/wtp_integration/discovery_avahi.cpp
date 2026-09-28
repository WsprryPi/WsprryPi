// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "discovery.hpp"
#include <chrono>
#include <cstdint>
#include <cstdlib>

#if defined(__linux__) && defined(WSPRRYPI_HAVE_AVAHI)
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/strlst.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <thread>

namespace wsprrypi {
namespace {
std::uint64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}
class AvahiBrowser {
  std::atomic_bool stopping_{false}, started_{false};
  std::thread worker_;
  AvahiSimplePoll *poll_{};
  AvahiClient *client_{};
  AvahiServiceBrowser *browser_{};
  std::map<WtpServiceKey, AvahiServiceResolver *> resolvers_;
  static void client_event(AvahiClient *client, AvahiClientState state, void *self) {
    auto *browser = static_cast<AvahiBrowser *>(self);
    if (state == AVAHI_CLIENT_FAILURE) {
      wtp_discovery().available(false, avahi_strerror(avahi_client_errno(client)));
      avahi_simple_poll_quit(browser->poll_);
    }
  }
  static void resolved(AvahiServiceResolver *resolver, AvahiIfIndex interface_index,
      AvahiProtocol protocol, AvahiResolverEvent event, const char *name,
      const char *, const char *domain, const char *host, const AvahiAddress *,
      std::uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags, void *self) {
    auto *browser = static_cast<AvahiBrowser *>(self);
    const WtpServiceKey key{interface_index, protocol, name ? name : "", domain ? domain : ""};
    if (event == AVAHI_RESOLVER_FOUND && !(flags & AVAHI_LOOKUP_RESULT_WIDE_AREA)) {
      std::vector<std::string> entries;
      for (auto *item = txt; item && entries.size() <= 32; item = avahi_string_list_get_next(item)) {
        const auto *bytes = avahi_string_list_get_text(item);
        const auto size = avahi_string_list_get_size(item);
        if (bytes && size <= 255) entries.emplace_back(reinterpret_cast<const char *>(bytes), size);
        else { entries.clear(); break; }
      }
      // Avahi prepends parsed TXT entries; restore the on-wire order for txtvers validation.
      std::reverse(entries.begin(), entries.end());
      wtp_discovery().resolved(key, host ? host : "", port, entries, now_ms());
    } else {
      wtp_discovery().failed(key, "Avahi resolver failed", now_ms());
    }
    const auto it = browser->resolvers_.find(key);
    if (it != browser->resolvers_.end() && it->second == resolver) browser->resolvers_.erase(it);
    avahi_service_resolver_free(resolver);
  }
  static void service_event(AvahiServiceBrowser *service_browser,
      AvahiIfIndex interface_index, AvahiProtocol protocol, AvahiBrowserEvent event,
      const char *name, const char *type, const char *domain,
      AvahiLookupResultFlags flags, void *self) {
    auto *browser = static_cast<AvahiBrowser *>(self);
    if (event == AVAHI_BROWSER_FAILURE) {
      wtp_discovery().available(false, avahi_strerror(avahi_client_errno(browser->client_)));
      avahi_simple_poll_quit(browser->poll_);
      return;
    }
    if (event != AVAHI_BROWSER_NEW && event != AVAHI_BROWSER_REMOVE) return;
    if (flags & AVAHI_LOOKUP_RESULT_WIDE_AREA) return;
    const WtpServiceKey key{interface_index, protocol, name ? name : "", domain ? domain : ""};
    if (const auto it = browser->resolvers_.find(key); it != browser->resolvers_.end()) {
      avahi_service_resolver_free(it->second);
      browser->resolvers_.erase(it);
    }
    if (event == AVAHI_BROWSER_REMOVE) {
      wtp_discovery().removed(key, now_ms());
      return;
    }
    wtp_discovery().found(key, now_ms());
    if (browser->resolvers_.size() >= 64) {
      wtp_discovery().failed(key, "Resolver capacity reached", now_ms());
      return;
    }
    auto *resolver = avahi_service_resolver_new(browser->client_, interface_index,
        protocol, name, type, domain, AVAHI_PROTO_UNSPEC,
        static_cast<AvahiLookupFlags>(0), resolved, self);
    if (resolver) browser->resolvers_[key] = resolver;
    else wtp_discovery().failed(key, "Could not start Avahi resolver", now_ms());
    (void)service_browser;
  }
  void run() {
    while (!stopping_) {
      poll_ = avahi_simple_poll_new();
      if (poll_) {
        int error = 0;
        client_ = avahi_client_new(avahi_simple_poll_get(poll_),
            static_cast<AvahiClientFlags>(0), client_event, this, &error);
        if (client_) browser_ = avahi_service_browser_new(client_, AVAHI_IF_UNSPEC,
            AVAHI_PROTO_UNSPEC, "_wtp._tcp", "local",
            static_cast<AvahiLookupFlags>(0), service_event, this);
        if (browser_) {
          wtp_discovery().available(true);
          while (!stopping_ && avahi_simple_poll_iterate(poll_, 200) == 0) {}
        } else wtp_discovery().available(false,
            client_ ? "Avahi service browser unavailable" : avahi_strerror(error));
      } else wtp_discovery().available(false, "Avahi poll unavailable");
      for (auto &[key, resolver] : resolvers_) avahi_service_resolver_free(resolver);
      resolvers_.clear();
      if (browser_) avahi_service_browser_free(browser_);
      if (client_) avahi_client_free(client_);
      if (poll_) avahi_simple_poll_free(poll_);
      browser_ = nullptr; client_ = nullptr; poll_ = nullptr;
      wtp_discovery().restart(now_ms());
      wtp_discovery().available(false, "Avahi unavailable; retrying");
      for (int i = 0; i < 20 && !stopping_; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
public:
  ~AvahiBrowser() { stopping_ = true; if (worker_.joinable()) worker_.join(); }
  void start() {
    if (started_.exchange(true)) return;
    worker_ = std::thread([this] { run(); });
  }
};
} // namespace
void start_wtp_discovery() {
  if (const char *disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
      disabled && std::string(disabled) == "1") {
    wtp_discovery().available(false, "Discovery disabled for hardware-free validation");
    return;
  }
  static AvahiBrowser browser; browser.start();
}
} // namespace wsprrypi
#else
namespace wsprrypi {
void start_wtp_discovery() {
  wtp_discovery().available(false, "Avahi client support is unavailable on this host");
}
} // namespace wsprrypi
#endif
