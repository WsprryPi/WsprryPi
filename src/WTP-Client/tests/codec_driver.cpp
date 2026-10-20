// SPDX-License-Identifier: MIT
// Hardware-free test adapter: stdin hex payloads, one result per line.
#include "wtp/codec.hpp"
#include "wtp/frame_parser.hpp"
#include <iostream>
#include <string>
using namespace wsprrypi::wtp;
int main(int argc, char **argv) {
    const bool frame_mode = argc == 2 && std::string_view(argv[1]) == "--encode-frame";
    if (argc != 1 && !frame_mode)
        return 2;
    auto print_hex = [](const auto &bytes) {
        constexpr char chars[] = "0123456789abcdef";
        for (unsigned char c : bytes)
            std::cout << chars[c >> 4] << chars[c & 15];
        std::cout << '\n';
    };
    std::string line;
    while (std::getline(std::cin, line)) {
        std::string bytes;
        auto hex = [](char c) {
            return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        };
        if (line.size() % 2)
            return 2;
        for (std::size_t i = 0; i < line.size(); i += 2) {
            if (hex(line[i]) < 0 || hex(line[i + 1]) < 0)
                return 2;
            bytes += static_cast<char>((hex(line[i]) << 4) | hex(line[i + 1]));
        }
        if (frame_mode) {
            print_hex(
                encode_frame({reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()}));
            continue;
        }
        auto result = decode(bytes);
        if (!result) {
            std::cout << "invalid\n";
            continue;
        }
        if (auto r = std::get_if<Request>(&*result.message)) {
            auto encoded = encode_request(*r);
            if (!encoded)
                return 3;
            std::cout << "request ";
            print_hex(*encoded.payload);
        } else if (auto r = std::get_if<Response>(&*result.message)) {
            std::cout << (r->ok() ? "response" : "error") << '\n';
        } else
            std::cout << "event\n";
    }
}
