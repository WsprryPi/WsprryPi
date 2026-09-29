#include "wtp_endpoint/dns_sd_publisher.hpp"

#include <cstdlib>

#if defined(__linux__) && defined(WSPRRYPI_HAVE_AVAHI)
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/strlst.h>
#include <net/if.h>
#include <chrono>
#include <thread>

namespace wsprrypi {
namespace {
struct Publication {
    WtpPiDnsSdPublisher* owner;
    AvahiSimplePoll* poll = nullptr;
    AvahiClient* client = nullptr;
    AvahiEntryGroup* group = nullptr;
    AvahiIfIndex interface_index = AVAHI_IF_UNSPEC;
    std::string name;
    std::string target, address;
    std::uint16_t port = 0;
    std::atomic<bool>* published = nullptr;
};

void group_event(AvahiEntryGroup*, AvahiEntryGroupState state, void* data) {
    auto* publication = static_cast<Publication*>(data);
    publication->published->store(state == AVAHI_ENTRY_GROUP_ESTABLISHED);
    if (state == AVAHI_ENTRY_GROUP_COLLISION ||
        state == AVAHI_ENTRY_GROUP_FAILURE)
        avahi_simple_poll_quit(publication->poll);
}

void client_event(AvahiClient* client, AvahiClientState state, void* data) {
    auto* publication = static_cast<Publication*>(data);
    if (state == AVAHI_CLIENT_FAILURE) {
        publication->published->store(false);
        avahi_simple_poll_quit(publication->poll);
        return;
    }
    if (state == AVAHI_CLIENT_S_COLLISION || state == AVAHI_CLIENT_S_REGISTERING) {
        publication->published->store(false);
        if (publication->group) avahi_entry_group_reset(publication->group);
        return;
    }
    if (state != AVAHI_CLIENT_S_RUNNING) return;
    if (!publication->group)
        publication->group = avahi_entry_group_new(client, group_event, data);
    if (!publication->group) {
        avahi_simple_poll_quit(publication->poll);
        return;
    }
    if (!avahi_entry_group_is_empty(publication->group)) return;
    AvahiAddress address{};
    if (!avahi_address_parse(publication->address.c_str(), AVAHI_PROTO_INET, &address) ||
        avahi_entry_group_add_address(publication->group, publication->interface_index,
            AVAHI_PROTO_INET, AVAHI_PUBLISH_NO_REVERSE,
            publication->target.c_str(), &address) < 0) {
        avahi_simple_poll_quit(publication->poll);
        return;
    }
    // Avahi prepends list entries; this insertion order makes txtvers the
    // first DNS TXT string when serialized.
    AvahiStringList* txt = avahi_string_list_add(nullptr, "txtvers=1");
    txt = avahi_string_list_add(txt, "binding=plain");
    const int added = avahi_entry_group_add_service_strlst(
        publication->group, publication->interface_index, AVAHI_PROTO_INET,
        static_cast<AvahiPublishFlags>(0), publication->name.c_str(),
        "_wtp._tcp", "local", publication->target.c_str(), publication->port, txt);
    avahi_string_list_free(txt);
    if (added < 0 || avahi_entry_group_commit(publication->group) < 0)
        avahi_simple_poll_quit(publication->poll);
}
} // namespace

WtpPiDnsSdPublisher::~WtpPiDnsSdPublisher() { stop(); }

bool WtpPiDnsSdPublisher::start(const std::string& interface_name,
                               const std::string& instance_name,
                               std::uint16_t port, const std::string& target,
                               const std::string& address) {
    if (worker_.joinable() || interface_name.empty() || instance_name.empty() ||
        port == 0 || if_nametoindex(interface_name.c_str()) == 0)
        return false;
    if (const char* disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
        disabled && std::string(disabled) == "1")
        return false;
    interface_name_ = interface_name;
    instance_name_ = instance_name;
    target_ = target;
    address_ = address;
    port_ = port;
    stopping_ = false;
    published_ = false;
    try { worker_ = std::thread(&WtpPiDnsSdPublisher::run, this); }
    catch (...) { return false; }
    return true;
}

void WtpPiDnsSdPublisher::run() noexcept {
    while (!stopping_) {
        Publication publication;
        publication.owner = this;
        publication.name = instance_name_;
        publication.target = target_;
        publication.address = address_;
        publication.port = port_;
        publication.published = &published_;
        publication.interface_index = static_cast<AvahiIfIndex>(
            if_nametoindex(interface_name_.c_str()));
        publication.poll = avahi_simple_poll_new();
        if (publication.poll) {
            int error = 0;
            publication.client = avahi_client_new(
                avahi_simple_poll_get(publication.poll),
                static_cast<AvahiClientFlags>(0), client_event, &publication, &error);
            if (publication.client)
                while (!stopping_ &&
                       avahi_simple_poll_iterate(publication.poll, 200) == 0) {}
        }
        published_ = false;
        if (publication.group) avahi_entry_group_free(publication.group);
        if (publication.client) avahi_client_free(publication.client);
        if (publication.poll) avahi_simple_poll_free(publication.poll);
        for (int i = 0; i < 20 && !stopping_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void WtpPiDnsSdPublisher::stop() noexcept {
    stopping_ = true;
    if (worker_.joinable()) worker_.join();
    published_ = false;
}
} // namespace wsprrypi
#else
namespace wsprrypi {
WtpPiDnsSdPublisher::~WtpPiDnsSdPublisher() { stop(); }
bool WtpPiDnsSdPublisher::start(const std::string&, const std::string&,
                               std::uint16_t, const std::string&,
                               const std::string&) { return false; }
void WtpPiDnsSdPublisher::run() noexcept {}
void WtpPiDnsSdPublisher::stop() noexcept { published_ = false; }
} // namespace wsprrypi
#endif
