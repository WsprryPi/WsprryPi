// SPDX-License-Identifier: MIT
// Derived from WsprryPico; Copyright (c) 2026 Lee Bussy. See LICENSE.md and PROVENANCE.json.
#include "detail/json.hpp"

#include <algorithm>
#include <charconv>
#include <limits>

namespace wsprrypi::wtp::json {
namespace {
void whitespace(std::string_view s, std::size_t &p) {
    while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t'))
        ++p;
}
int hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
bool codepoint(std::string_view s, std::size_t &p, unsigned &cp) {
    cp = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (p == s.size() || hex(s[p]) < 0)
            return false;
        cp = cp * 16 + hex(s[p++]);
    }
    return true;
}
void append_utf8(std::string &out, unsigned cp) {
    if (cp < 0x80)
        out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    } else {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 63));
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    }
}
bool string_token(std::string_view s, std::size_t &p, std::string *decoded = nullptr) {
    if (p == s.size() || s[p++] != '"')
        return false;
    while (p < s.size()) {
        auto c = static_cast<unsigned char>(s[p++]);
        if (c == '"')
            return true;
        if (c < 32)
            return false;
        if (c != '\\') {
            if (decoded)
                *decoded += static_cast<char>(c);
            continue;
        }
        if (p == s.size())
            return false;
        char escape = s[p++];
        if (escape == 'u') {
            unsigned cp;
            if (!codepoint(s, p, cp))
                return false;
            if (cp >= 0xd800 && cp <= 0xdbff) {
                if (p + 2 > s.size() || s.substr(p, 2) != "\\u")
                    return false;
                p += 2;
                unsigned low;
                if (!codepoint(s, p, low) || low < 0xdc00 || low > 0xdfff)
                    return false;
                cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
            } else if (cp >= 0xdc00 && cp <= 0xdfff)
                return false;
            if (decoded)
                append_utf8(*decoded, cp);
        } else {
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                c = escape;
                break;
            case 'b':
                c = '\b';
                break;
            case 'f':
                c = '\f';
                break;
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            default:
                return false;
            }
            if (decoded)
                *decoded += static_cast<char>(c);
        }
    }
    return false;
}
bool utf8(std::string_view s) {
    for (std::size_t p = 0; p < s.size();) {
        unsigned c = static_cast<unsigned char>(s[p++]);
        if (c < 0x80)
            continue;
        unsigned remaining, cp, minimum;
        if (c >= 0xc2 && c <= 0xdf) {
            remaining = 1;
            cp = c & 31;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            remaining = 2;
            cp = c & 15;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            remaining = 3;
            cp = c & 7;
            minimum = 0x10000;
        } else
            return false;
        while (remaining--) {
            if (p == s.size())
                return false;
            c = static_cast<unsigned char>(s[p++]);
            if ((c & 0xc0) != 0x80)
                return false;
            cp = (cp << 6) | (c & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return false;
    }
    return true;
}
// Validation stores only key views for the active objects, never a value tree.
bool value(std::string_view s, std::size_t &p, unsigned depth, bool validate) {
    whitespace(s, p);
    if (p == s.size())
        return false;
    const auto start = p;
    char c = s[p];
    if (c == '"')
        return string_token(s, p);
    if (c == '{' || c == '[') {
        if (depth > 16)
            return false;
        ++p;
        whitespace(s, p);
        const char end = c == '{' ? '}' : ']';
        std::vector<std::string_view> keys;
        if (p < s.size() && s[p] == end) {
            ++p;
            return true;
        }
        while (p < s.size()) {
            if (c == '{') {
                const auto key_start = p;
                if (!string_token(s, p))
                    return false;
                if (validate)
                    keys.push_back(s.substr(key_start, p - key_start));
                whitespace(s, p);
                if (p == s.size() || s[p++] != ':')
                    return false;
            }
            if (!value(s, p, depth + 1, validate))
                return false;
            whitespace(s, p);
            if (p == s.size())
                return false;
            if (s[p++] == end) {
                if (validate && c == '{') {
                    auto less = [](auto a, auto b) {
                        return Value{a}.string() < Value{b}.string();
                    };
                    std::sort(keys.begin(), keys.end(), less);
                    for (std::size_t i = 1; i < keys.size(); ++i)
                        if (Value{keys[i - 1]}.string() == Value{keys[i]}.string())
                            return false;
                }
                return true;
            }
            if (s[p - 1] != ',')
                return false;
            whitespace(s, p);
        }
        return false;
    }
    for (auto literal :
         {std::string_view("true"), std::string_view("false"), std::string_view("null")}) {
        if (s.substr(p, literal.size()) == literal) {
            p += literal.size();
            return true;
        }
    }
    if (s[p] == '-')
        ++p;
    if (p == s.size() || s[p] < '0' || s[p] > '9')
        return false;
    if (s[p] == '0')
        ++p;
    else
        while (p < s.size() && s[p] >= '0' && s[p] <= '9')
            ++p;
    std::int32_t number;
    auto result = std::from_chars(s.data() + start, s.data() + p, number);
    return result.ec == std::errc{} && result.ptr == s.data() + p;
}
} // namespace
char Value::type() const { return raw.empty() ? '?' : raw[0]; }
std::string Value::string() const {
    std::string out;
    std::size_t p = 0;
    string_token(raw, p, &out);
    return out;
}
std::int32_t Value::integer() const {
    std::int32_t out = 0;
    std::from_chars(raw.data(), raw.data() + raw.size(), out);
    return out;
}
bool Value::boolean() const { return raw == "true"; }
std::vector<Value> Value::elements(std::size_t limit) const {
    std::vector<Value> out;
    if (type() != '[')
        return out;
    std::size_t p = 1;
    whitespace(raw, p);
    while (p < raw.size() && raw[p] != ']') {
        auto start = p;
        if (!value(raw, p, 1, false))
            return {};
        out.push_back({raw.substr(start, p - start)});
        if (out.size() > limit)
            break;
        whitespace(raw, p);
        if (raw[p] == ']')
            break;
        ++p;
        whitespace(raw, p);
    }
    return out;
}
std::optional<Value> Value::get(std::string_view key) const {
    if (type() != '{')
        return std::nullopt;
    std::size_t p = 1;
    whitespace(raw, p);
    while (p < raw.size() && raw[p] != '}') {
        std::string name;
        if (!string_token(raw, p, &name))
            return std::nullopt;
        whitespace(raw, p);
        ++p;
        whitespace(raw, p);
        auto start = p;
        if (!value(raw, p, 1, false))
            return std::nullopt;
        if (name == key)
            return Value{raw.substr(start, p - start)};
        whitespace(raw, p);
        if (raw[p] == '}')
            break;
        ++p;
        whitespace(raw, p);
    }
    return std::nullopt;
}
std::optional<Value> parse(std::string_view payload) {
    if (payload.empty() || payload.size() > 65536 || !utf8(payload))
        return std::nullopt;
    std::size_t p = 0;
    whitespace(payload, p);
    auto start = p;
    if (p == payload.size() || payload[p] != '{' || !value(payload, p, 1, true))
        return std::nullopt;
    auto end = p;
    whitespace(payload, p);
    if (p != payload.size())
        return std::nullopt;
    return Value{payload.substr(start, end - start)};
}
std::string quote(std::string_view s) {
    std::string out = "\"";
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 32) {
            out += "\\u00";
            out += digits[c >> 4];
            out += digits[c & 15];
        } else
            out += static_cast<char>(c);
    }
    return out + '"';
}
bool decimal(Value v, std::uint64_t &out, bool nonzero) {
    if (v.type() != '"')
        return false;
    auto s = v.string();
    if (s.empty() || (s.size() > 1 && s[0] == '0') || (nonzero && s == "0"))
        return false;
    if (!std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return false;
    auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}
bool identifier(Value v) {
    if (v.type() != '"')
        return false;
    auto s = v.string();
    return s.size() == 32 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
bool fields(Value v, std::initializer_list<std::string_view> required,
            std::initializer_list<std::string_view> optional) {
    if (v.type() != '{')
        return false;
    for (auto key : required)
        if (!v.get(key))
            return false;
    std::size_t p = 1;
    whitespace(v.raw, p);
    while (p < v.raw.size() && v.raw[p] != '}') {
        std::string key;
        if (!string_token(v.raw, p, &key))
            return false;
        if (std::find(required.begin(), required.end(), key) == required.end() &&
            std::find(optional.begin(), optional.end(), key) == optional.end())
            return false;
        whitespace(v.raw, p);
        ++p;
        if (!value(v.raw, p, 1, false))
            return false;
        whitespace(v.raw, p);
        if (v.raw[p] == '}')
            break;
        ++p;
        whitespace(v.raw, p);
    }
    return true;
}
} // namespace wsprrypi::wtp::json
