// SPDX-License-Identifier: MIT
// Shared, allocation-free wsprry-udp/1 codec. Keep the Pi copy byte-identical.
// Civil-date arithmetic follows the existing USB logger and Howard Hinnant's
// public-domain civil_from_days: https://howardhinnant.github.io/date_algorithms.html
#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>

namespace wsprry::logging {
inline constexpr std::size_t udp_payload_limit = 480;
template <std::size_t N> struct Text {
    std::array<char, N + 1> bytes{};
    std::size_t size{};
    bool assign(std::string_view s) {
        if (s.size() > N)
            return false;
        std::copy(s.begin(), s.end(), bytes.begin());
        size = s.size();
        bytes[size] = 0;
        return true;
    }
    std::string_view view() const {
        return {bytes.data(), size};
    }
};
struct UdpEvent {
    bool pico = true;
    Text<32> origin, boot, code;
    Text<63> hostname;
    Text<192> message;
    std::uint64_t sequence{}, monotonic_ns{}, dropped{};
    std::optional<std::uint64_t> utc_ns;
    unsigned severity = 6;
    bool relayed = false;
    Text<32> relay_id;
    std::uint64_t relay_receipt_us{};
};
inline bool hex_id(std::string_view s) {
    return s.size() == 32 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
inline bool decimal(std::string_view s, std::uint64_t& n) {
    if (s.empty() || s.size() > 20 || (s.size() > 1 && s[0] == '0') ||
        !std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return false;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}
inline bool token(std::string_view s, std::size_t maximum) {
    return !s.empty() && s.size() <= maximum &&
           std::all_of(s.begin(), s.end(), [](unsigned char c) {
               return c >= 33 && c <= 126 && c != '"' && c != '\\';
           });
}
inline bool hostname(std::string_view s) {
    return token(s, 63) && std::all_of(s.begin(), s.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '.' || c == '-';
           });
}
inline bool valid(const UdpEvent& e) {
    return hex_id(e.origin.view()) && hex_id(e.boot.view()) && e.sequence && e.severity <= 7 &&
           token(e.code.view(), 32) && hostname(e.hostname.view()) &&
           std::all_of(e.message.view().begin(), e.message.view().end(),
                       [](unsigned char c) { return c >= 32 && c <= 126; }) &&
           (!e.relayed ||
            (e.pico && hex_id(e.relay_id.view()) && e.relay_id.view() != e.origin.view()));
}
// uint64 nanoseconds span 1970..2554; six digits are RFC5424's precision ceiling.
inline std::array<char, 32> timestamp(std::optional<std::uint64_t> utc) {
    std::array<char, 32> result{};
    if (!utc) {
        result[0] = '-';
        return result;
    }
    const auto seconds = *utc / 1'000'000'000ULL;
    const auto z = static_cast<std::int64_t>(seconds / 86400) + 719468;
    const auto era = z / 146097, doe = z - era * 146097;
    const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    auto year = yoe + era * 400;
    const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const auto mp = (5 * doy + 2) / 153;
    const auto day = doy - (153 * mp + 2) / 5 + 1;
    const auto month = mp + (mp < 10 ? 3 : -9);
    year += month <= 2;
    std::snprintf(result.data(), result.size(), "%04u-%02u-%02uT%02u:%02u:%02u.%06uZ",
                  static_cast<unsigned>(year), static_cast<unsigned>(month),
                  static_cast<unsigned>(day), static_cast<unsigned>(seconds / 3600 % 24),
                  static_cast<unsigned>(seconds / 60 % 60), static_cast<unsigned>(seconds % 60),
                  static_cast<unsigned>(*utc / 1000 % 1'000'000));
    return result;
}
namespace udp_detail {
struct Writer {
    std::span<char> out;
    std::size_t used{};
    bool ok = true;
    void put(std::string_view s) {
        if (!ok || s.size() > out.size() - used) {
            ok = false;
            return;
        }
        std::copy(s.begin(), s.end(), out.begin() + used);
        used += s.size();
    }
    void number(std::uint64_t n) {
        std::array<char, 20> b{};
        auto r = std::to_chars(b.data(), b.data() + b.size(), n);
        put({b.data(), static_cast<std::size_t>(r.ptr - b.data())});
    }
    void quoted(std::string_view s) {
        put("\"");
        for (char c : s) {
            if (c == '"' || c == '\\')
                put("\\");
            put({&c, 1});
        }
        put("\"");
    }
    void counter(std::uint64_t n) {
        put("\"");
        number(n);
        put("\"");
    }
};
struct Reader {
    std::string_view in;
    bool take(std::string_view s) {
        if (!in.starts_with(s))
            return false;
        in.remove_prefix(s.size());
        return true;
    }
    template <std::size_t N> bool text(Text<N>& out) {
        out = {};
        if (!take("\""))
            return false;
        while (!in.empty()) {
            auto c = in.front();
            in.remove_prefix(1);
            if (c == '"') {
                out.bytes[out.size] = 0;
                return true;
            }
            if (c == '\\') {
                if (in.empty() || (in.front() != '\\' && in.front() != '"'))
                    return false;
                c = in.front();
                in.remove_prefix(1);
            }
            if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126 ||
                out.size == N)
                return false;
            out.bytes[out.size++] = c;
        }
        return false;
    }
    bool counter(std::uint64_t& n) {
        Text<20> s;
        return text(s) && decimal(s.view(), n);
    }
    std::string_view word() {
        const auto end = in.find(' ');
        if (end == std::string_view::npos || end == 0)
            return {};
        const auto s = in.substr(0, end);
        in.remove_prefix(end + 1);
        return s;
    }
};
} // namespace udp_detail
// Returns a view into caller-owned storage. Only the message may be shortened.
inline std::string_view encode_udp(const UdpEvent& e, std::span<char> output, bool& truncated) {
    truncated = false;
    if (!valid(e) || output.size() < udp_payload_limit)
        return {};
    const auto emit = [&](std::size_t length) -> std::size_t {
        udp_detail::Writer w{output.first(udp_payload_limit)};
        w.put("<");
        w.number(184 + e.severity);
        w.put(">1 ");
        w.put(timestamp(e.utc_ns).data());
        w.put(" ");
        w.put(e.hostname.view());
        w.put(e.pico ? " WsprryPico - " : " WsprryPi - ");
        w.put(e.code.view());
        w.put(" - [\"wsprry-udp/1\",");
        w.quoted(e.pico ? "pico" : "pi");
        w.put(",");
        w.quoted(e.origin.view());
        w.put(",");
        w.quoted(e.boot.view());
        w.put(",");
        w.counter(e.sequence);
        w.put(",");
        if (e.utc_ns)
            w.counter(*e.utc_ns);
        else
            w.put("null");
        w.put(",");
        w.counter(e.monotonic_ns);
        w.put(",");
        w.counter(e.dropped);
        w.put(",");
        w.quoted(e.message.view().substr(0, length));
        w.put(",");
        if (e.relayed) {
            w.put("[");
            w.quoted(e.relay_id.view());
            w.put(",");
            w.counter(e.relay_receipt_us);
            w.put(",\"usb\"]");
        } else
            w.put("null");
        w.put("]");
        return w.ok ? w.used : 0;
    };
    const auto mandatory = emit(0);
    if (!mandatory)
        return {};
    std::size_t length = 0, budget = udp_payload_limit - mandatory;
    for (char c : e.message.view()) {
        const std::size_t cost = (c == '"' || c == '\\') ? 2 : 1;
        if (cost > budget)
            break;
        budget -= cost;
        ++length;
    }
    truncated = length != e.message.size;
    const auto size = emit(length);
    return size ? std::string_view{output.data(), size} : std::string_view{};
}
// Failure never mutates the caller's record. Unknown versions fail closed.
inline bool decode_udp(std::string_view input, UdpEvent& output, std::string_view collector = {}) {
    if (input.empty() || input.size() > udp_payload_limit ||
        !std::all_of(input.begin(), input.end(),
                     [](unsigned char c) { return c >= 32 && c <= 126; }))
        return false;
    UdpEvent e;
    udp_detail::Reader r{input};
    if (!r.take("<"))
        return false;
    const auto end = r.in.find('>');
    if (end == std::string_view::npos)
        return false;
    std::uint64_t pri{};
    if (!decimal(r.in.substr(0, end), pri) || pri < 184 || pri > 191)
        return false;
    e.severity = static_cast<unsigned>(pri - 184);
    r.in.remove_prefix(end);
    if (!r.take(">1 "))
        return false;
    const auto time = r.word();
    if (time.empty() || !e.hostname.assign(r.word()))
        return false;
    const auto app = r.word();
    if (app != "WsprryPico" && app != "WsprryPi")
        return false;
    e.pico = app == "WsprryPico";
    if (!r.take("- ") || !e.code.assign(r.word()) || !r.take("- [\"wsprry-udp/1\","))
        return false;
    if (!r.take(e.pico ? "\"pico\"," : "\"pi\","))
        return false;
    if (!r.text(e.origin) || !r.take(",") || !r.text(e.boot) || !r.take(",") ||
        !r.counter(e.sequence) || !r.take(","))
        return false;
    if (!r.take("null")) {
        std::uint64_t n{};
        if (!r.counter(n))
            return false;
        e.utc_ns = n;
    }
    if (!r.take(",") || !r.counter(e.monotonic_ns) || !r.take(",") || !r.counter(e.dropped) ||
        !r.take(",") || !r.text(e.message) || !r.take(","))
        return false;
    if (!r.take("null")) {
        e.relayed = true;
        if (!r.take("[") || !r.text(e.relay_id) || !r.take(",") || !r.counter(e.relay_receipt_us) ||
            !r.take(",\"usb\"]"))
            return false;
    }
    if (!r.take("]") || !r.in.empty() || !valid(e) || time != timestamp(e.utc_ns).data() ||
        (!collector.empty() &&
         (collector == e.origin.view() || (e.relayed && collector == e.relay_id.view()))))
        return false;
    output = e;
    return true;
}
} // namespace wsprry::logging
