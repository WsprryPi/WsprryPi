#include "wspr/wspr_ref_decoder.hpp"
#include "wspr/wspr_ref_api.hpp"
#include "wspr/wspr_ref_unpack.hpp"
#include "wspr/wspr_constants.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

namespace
{
    using json = nlohmann::json;

    void print_payload_bits(const uint8_t *payload_bits)
    {
        std::cout << "Recovered payload bits:\n";
        for (std::size_t i = 0; i < wspr::WSPR_PAYLOAD_BIT_COUNT; ++i)
            std::cout << static_cast<unsigned>(payload_bits[i]);
        std::cout << "\n";
    }

    void print_type1_message(const wspr::WsprDecodedMessage &message)
    {
        std::cout << "\nDecoded Type 1 message:\n";
        std::cout << "  Callsign: " << message.callsign << "\n";
        std::cout << "  Locator:  " << message.locator << "\n";
        std::cout << "  Power:    " << message.power_dbm << "\n";
    }

    void print_type2_message(const wspr::WsprDecodedMessage &message)
    {
        std::cout << "\nDecoded Type 2 partial message:\n";
        std::cout << "  Callsign: " << message.callsign << "\n";
        std::cout << "  Extra:    " << message.extra << "\n";
        std::cout << "  Power:    " << message.power_dbm << "\n";
        std::cout << "  Partial:  "
                  << (message.is_partial ? "true" : "false") << "\n";

        if (message.has_ambiguity)
        {
            std::cout << "  Ambiguous with: "
                      << message.alternate_extra << "\n";
        }
    }

    void print_type3_message(const wspr::WsprDecodedMessage &message)
    {
        std::cout << "\nDecoded Type 3 partial message:\n";
        std::cout << "  Callsign: " << message.callsign << "\n";
        std::cout << "  Hash:     " << message.callsign_hash << "\n";
        std::cout << "  Locator:  " << message.locator << "\n";
        std::cout << "  Power:    " << message.power_dbm << "\n";
        std::cout << "  Partial:  "
                  << (message.is_partial ? "true" : "false") << "\n";
    }

    void print_quiet_message(const wspr::WsprDecodedMessage &message)
    {
        switch (message.type)
        {
        case wspr::WsprMessageType::Type1:
            std::cout
                << "TYPE1 "
                << message.callsign << " "
                << message.locator << " "
                << message.power_dbm << "\n";
            break;

        case wspr::WsprMessageType::Type2:
            std::cout
                << "TYPE2 "
                << message.callsign << " "
                << message.power_dbm;

            if (message.has_ambiguity)
                std::cout << " ALT " << message.alternate_extra;

            std::cout << "\n";
            break;

        case wspr::WsprMessageType::Type3:
            std::cout
                << "TYPE3 "
                << message.callsign << " "
                << message.callsign_hash << " "
                << message.locator << " "
                << message.power_dbm << "\n";
            break;

        default:
            std::cout << "UNKNOWN\n";
            break;
        }
    }

    std::string type_to_string(wspr::WsprMessageType type)
    {
        switch (type)
        {
        case wspr::WsprMessageType::Type1:
            return "TYPE1";
        case wspr::WsprMessageType::Type2:
            return "TYPE2";
        case wspr::WsprMessageType::Type3:
            return "TYPE3";
        default:
            return "UNKNOWN";
        }
    }

    json message_to_json(const wspr::WsprDecodedMessage &message)
    {
        json j;

        j["type"] = type_to_string(message.type);
        j["callsign"] = message.callsign;
        j["power_dbm"] = message.power_dbm;
        j["is_partial"] = message.is_partial;
        j["has_hash"] = message.has_hash;
        j["has_ambiguity"] = message.has_ambiguity;

        if (!message.locator.empty())
            j["locator"] = message.locator;

        if (!message.extra.empty())
            j["extra"] = message.extra;

        if (message.has_hash)
            j["hash"] = message.callsign_hash;

        if (message.has_ambiguity)
            j["alternate_extra"] = message.alternate_extra;

        return j;
    }
} // namespace

int main(int argc, char **argv)
{
    bool quiet = false;
    bool json_mode = false;
    int argi = 1;

    while (argi < argc)
    {
        const std::string arg = argv[argi];

        if (arg == "--quiet")
        {
            quiet = true;
            ++argi;
            continue;
        }

        if (arg == "--json")
        {
            json_mode = true;
            ++argi;
            continue;
        }

        break;
    }

    if (quiet && json_mode)
    {
        std::cerr << "Use either --quiet or --json, not both.\n";
        return 1;
    }

    if ((argc - argi) != 1)
    {
        std::cerr
            << "Usage: wspr-decode [--quiet|--json] <162-symbol-string>\n";
        return 1;
    }

    wspr::WsprRefDecoder decoder;
    uint8_t g_bits[wspr::WSPR_BIT_COUNT] = {};
    uint8_t deinterleaved_bits[wspr::WSPR_BIT_COUNT] = {};
    uint8_t payload_bits[wspr::WSPR_PAYLOAD_BIT_COUNT] = {};

    std::string error;

    if (!decoder.symbols_to_bits(argv[argi], g_bits, error))
    {
        std::cerr << "Decode error: " << error << "\n";
        return 1;
    }

    decoder.deinterleave_bits(g_bits, deinterleaved_bits);

    if (!decoder.decode_payload_bits_from_symbols(argv[argi], payload_bits, error))
    {
        std::cerr << "Payload decode error: " << error << "\n";
        return 1;
    }

    wspr::WsprDecodedMessage message;

    if (wspr::decode_symbols(argv[argi], message, error))
    {
        if (json_mode)
        {
            std::cout << message_to_json(message).dump(2) << "\n";
            return 0;
        }

        if (quiet)
        {
            print_quiet_message(message);
            return 0;
        }

        std::cout << "Recovered g bits:\n";
        for (std::size_t i = 0; i < wspr::WSPR_BIT_COUNT; ++i)
            std::cout << static_cast<unsigned>(g_bits[i]);
        std::cout << "\n";

        std::cout << "Deinterleaved bits:\n";
        for (std::size_t i = 0; i < wspr::WSPR_BIT_COUNT; ++i)
            std::cout << static_cast<unsigned>(deinterleaved_bits[i]);
        std::cout << "\n";

        print_payload_bits(payload_bits);
        switch (message.type)
        {
        case wspr::WsprMessageType::Type1:
            print_type1_message(message);
            break;
        case wspr::WsprMessageType::Type2:
            print_type2_message(message);
            break;
        case wspr::WsprMessageType::Type3:
            print_type3_message(message);
            break;
        default:
            break;
        }
        return 0;
    }

    if (json_mode)
    {
        json j;
        j["type"] = "UNKNOWN";
        j["error"] = "No message type unpack succeeded.";
        std::cout << j.dump(2) << "\n";
        return 0;
    }

    if (!quiet)
        std::cout << "\nNo message type unpack succeeded.\n";

    return 0;
}
