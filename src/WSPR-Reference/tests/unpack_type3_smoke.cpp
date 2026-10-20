#include "wspr/wspr_ref_unpack.hpp"
#include "wspr/wspr_constants.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>

int main()
{
    wspr::WsprRefUnpacker unpacker;

    uint8_t payload[wspr::WSPR_PAYLOAD_BIT_COUNT] = {};

    for (std::size_t i = 0; i < wspr::WSPR_PAYLOAD_BIT_COUNT; ++i)
        payload[i] = static_cast<uint8_t>((i + 1) % 2);

    wspr::WsprDecodedMessage msg;

    if (!unpacker.unpack_type3(payload, wspr::WSPR_PAYLOAD_BIT_COUNT, msg))
    {
        std::cerr << "Type3 unpack failed: " << msg.error << "\n";
        return 1;
    }

    std::cout << "Type3 decode (smoke):\n";
    std::cout << "  Callsign: " << msg.callsign << "\n";
    std::cout << "  Hash:     " << msg.callsign_hash << "\n";
    std::cout << "  Locator:  " << msg.locator << "\n";
    std::cout << "  Power:    " << msg.power_dbm << "\n";
    std::cout << "  Partial:  " << (msg.is_partial ? "true" : "false") << "\n";

    return 0;
}
