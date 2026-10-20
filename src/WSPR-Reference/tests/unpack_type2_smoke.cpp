#include "wspr/wspr_ref_unpack.hpp"
#include "wspr/wspr_constants.hpp"

#include <iostream>

int main()
{
    wspr::WsprRefUnpacker unpacker;

    uint8_t payload[wspr::WSPR_PAYLOAD_BIT_COUNT] = {};

    // fake payload just to exercise code path
    for (std::size_t i = 0; i < wspr::WSPR_PAYLOAD_BIT_COUNT; ++i)
        payload[i] = (i % 2);

    wspr::WsprDecodedMessage msg;

    if (!unpacker.unpack_type2(payload, wspr::WSPR_PAYLOAD_BIT_COUNT, msg))
    {
        std::cerr << "Type2 unpack failed\n";
        return 1;
    }

    std::cout << "Type2 decode (smoke):\n";
    std::cout << "  Callsign: " << msg.callsign << "\n";
    std::cout << "  Extra:    " << msg.extra << "\n";
    std::cout << "  Power:    " << msg.power_dbm << "\n";

    return 0;
}
