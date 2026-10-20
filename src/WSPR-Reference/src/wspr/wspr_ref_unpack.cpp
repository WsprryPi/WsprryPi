#include "wspr_ref_unpack.hpp"
#include "wspr_constants.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

namespace
{
    constexpr uint32_t rot(uint32_t x, uint32_t k)
    {
        return (x << k) | (x >> (32 - k));
    }

    void mix(uint32_t &a, uint32_t &b, uint32_t &c)
    {
        a -= c;
        a ^= rot(c, 4);
        c += b;
        b -= a;
        b ^= rot(a, 6);
        a += c;
        c -= b;
        c ^= rot(b, 8);
        b += a;
        a -= c;
        a ^= rot(c, 16);
        c += b;
        b -= a;
        b ^= rot(a, 19);
        a += c;
        c -= b;
        c ^= rot(b, 4);
        b += a;
    }

    void final_mix(uint32_t &a, uint32_t &b, uint32_t &c)
    {
        c ^= b;
        c -= rot(b, 14);
        a ^= c;
        a -= rot(c, 11);
        b ^= a;
        b -= rot(a, 25);
        c ^= b;
        c -= rot(b, 16);
        a ^= c;
        a -= rot(c, 4);
        b ^= a;
        b -= rot(a, 14);
        c ^= b;
        c -= rot(b, 24);
    }

    uint32_t nhash(const void *key, int *length0, uint32_t *initval0)
    {
        uint32_t a, b, c;
        std::size_t length = static_cast<std::size_t>(*length0);
        uint32_t initval = *initval0;
        union
        {
            const void *ptr;
            std::size_t i;
        } u;

        a = b = c = 0xdeadbeefU + static_cast<uint32_t>(length) + initval;

        u.ptr = key;
        if ((u.i & 0x3U) == 0)
        {
            const uint32_t *k = static_cast<const uint32_t *>(key);

            while (length > 12)
            {
                a += k[0];
                b += k[1];
                c += k[2];
                mix(a, b, c);
                length -= 12;
                k += 3;
            }

            switch (length)
            {
            case 12: c += k[2]; b += k[1]; a += k[0]; break;
            case 11: c += k[2] & 0x00ffffffU; b += k[1]; a += k[0]; break;
            case 10: c += k[2] & 0x0000ffffU; b += k[1]; a += k[0]; break;
            case 9: c += k[2] & 0x000000ffU; b += k[1]; a += k[0]; break;
            case 8: b += k[1]; a += k[0]; break;
            case 7: b += k[1] & 0x00ffffffU; a += k[0]; break;
            case 6: b += k[1] & 0x0000ffffU; a += k[0]; break;
            case 5: b += k[1] & 0x000000ffU; a += k[0]; break;
            case 4: a += k[0]; break;
            case 3: a += k[0] & 0x00ffffffU; break;
            case 2: a += k[0] & 0x0000ffffU; break;
            case 1: a += k[0] & 0x000000ffU; break;
            case 0: return c;
            }
        }
        else if ((u.i & 0x1U) == 0)
        {
            const uint16_t *k = static_cast<const uint16_t *>(key);
            const uint8_t *k8 = reinterpret_cast<const uint8_t *>(k);

            while (length > 12)
            {
                a += k[0] + (static_cast<uint32_t>(k[1]) << 16);
                b += k[2] + (static_cast<uint32_t>(k[3]) << 16);
                c += k[4] + (static_cast<uint32_t>(k[5]) << 16);
                mix(a, b, c);
                length -= 12;
                k += 6;
            }

            switch (length)
            {
            case 12: c += k[4] + (static_cast<uint32_t>(k[5]) << 16); b += k[2] + (static_cast<uint32_t>(k[3]) << 16); a += k[0] + (static_cast<uint32_t>(k[1]) << 16); break;
            case 11: c += static_cast<uint32_t>(k8[10]) << 16; [[fallthrough]];
            case 10: c += k[4]; b += k[2] + (static_cast<uint32_t>(k[3]) << 16); a += k[0] + (static_cast<uint32_t>(k[1]) << 16); break;
            case 9: c += k8[8]; [[fallthrough]];
            case 8: b += k[2] + (static_cast<uint32_t>(k[3]) << 16); a += k[0] + (static_cast<uint32_t>(k[1]) << 16); break;
            case 7: b += static_cast<uint32_t>(k8[6]) << 16; [[fallthrough]];
            case 6: b += k[2]; a += k[0] + (static_cast<uint32_t>(k[1]) << 16); break;
            case 5: b += k8[4]; [[fallthrough]];
            case 4: a += k[0] + (static_cast<uint32_t>(k[1]) << 16); break;
            case 3: a += static_cast<uint32_t>(k8[2]) << 16; [[fallthrough]];
            case 2: a += k[0]; break;
            case 1: a += k8[0]; break;
            case 0: return c;
            }
        }
        else
        {
            const uint8_t *k = static_cast<const uint8_t *>(key);

            while (length > 12)
            {
                a += k[0];
                a += static_cast<uint32_t>(k[1]) << 8;
                a += static_cast<uint32_t>(k[2]) << 16;
                a += static_cast<uint32_t>(k[3]) << 24;
                b += k[4];
                b += static_cast<uint32_t>(k[5]) << 8;
                b += static_cast<uint32_t>(k[6]) << 16;
                b += static_cast<uint32_t>(k[7]) << 24;
                c += k[8];
                c += static_cast<uint32_t>(k[9]) << 8;
                c += static_cast<uint32_t>(k[10]) << 16;
                c += static_cast<uint32_t>(k[11]) << 24;
                mix(a, b, c);
                length -= 12;
                k += 12;
            }

            switch (length)
            {
            case 12: c += static_cast<uint32_t>(k[11]) << 24; [[fallthrough]];
            case 11: c += static_cast<uint32_t>(k[10]) << 16; [[fallthrough]];
            case 10: c += static_cast<uint32_t>(k[9]) << 8; [[fallthrough]];
            case 9: c += k[8]; [[fallthrough]];
            case 8: b += static_cast<uint32_t>(k[7]) << 24; [[fallthrough]];
            case 7: b += static_cast<uint32_t>(k[6]) << 16; [[fallthrough]];
            case 6: b += static_cast<uint32_t>(k[5]) << 8; [[fallthrough]];
            case 5: b += k[4]; [[fallthrough]];
            case 4: a += static_cast<uint32_t>(k[3]) << 24; [[fallthrough]];
            case 3: a += static_cast<uint32_t>(k[2]) << 16; [[fallthrough]];
            case 2: a += static_cast<uint32_t>(k[1]) << 8; [[fallthrough]];
            case 1: a += k[0]; break;
            case 0: return c;
            }
        }

        final_mix(a, b, c);
        return c;
    }

    uint32_t callsign_hash_from_base(const std::string &base_callsign)
    {
        int call_len = static_cast<int>(base_callsign.size());
        uint32_t init_val = 146U;
        return nhash(base_callsign.data(), &call_len, &init_val) & 32767U;
    }

    std::string format_type2_callsign(
        const std::string &base_callsign,
        const std::string &extra)
    {
        if (extra.empty())
            return base_callsign;

        if (extra.back() == '/')
            return extra + base_callsign;

        if (extra.front() == '/')
            return base_callsign + extra;

        return base_callsign + extra;
    }

    bool is_valid_wspr_power(int power_dbm)
    {
        static constexpr int valid_dbm[] = {
            0, 3, 7, 10, 13, 17, 20, 23, 27, 30,
            33, 37, 40, 43, 47, 50, 53, 57, 60};

        for (int p : valid_dbm)
        {
            if (power_dbm == p)
                return true;
        }

        return false;
    }

    bool is_plausible_type1_callsign(const std::string &callsign)
    {
        if (callsign.empty() || callsign.size() > 6)
            return false;

        bool has_digit = false;
        bool has_letter = false;

        for (char c : callsign)
        {
            if (c >= '0' && c <= '9')
                has_digit = true;
            else if (c >= 'A' && c <= 'Z')
                has_letter = true;
            else if (c != ' ')
                return false;
        }

        return has_digit && has_letter;
    }

    std::string unrotate_type3_locator(const std::string &packed_locator)
    {
        if (packed_locator.size() != 6)
            return packed_locator;

        std::string out;
        out.reserve(6);

        out.push_back(packed_locator[5]);
        out.push_back(packed_locator[0]);
        out.push_back(packed_locator[1]);
        out.push_back(packed_locator[2]);
        out.push_back(packed_locator[3]);
        out.push_back(packed_locator[4]);

        return out;
    }

    bool is_type2_overlap_ext_field(uint32_t ext_field)
    {
        return (ext_field >= 27258U && ext_field <= 27267U);
    }
} // namespace

namespace wspr
{
    namespace
    {
        char decode_wspr_base37(uint32_t value)
        {
            if (value <= 9)
                return static_cast<char>('0' + value);

            if (value >= 10 && value <= 35)
                return static_cast<char>('A' + (value - 10));

            return ' ';
        }
    } // namespace

    uint32_t WsprRefUnpacker::extract_n(
        const uint8_t *payload_bits,
        std::size_t payload_bit_count) const
    {
        (void)payload_bit_count;

        uint32_t n = 0;

        for (std::size_t i = 0; i < 28; ++i)
        {
            n = (n << 1) | static_cast<uint32_t>(payload_bits[i]);
        }

        return n;
    }

    uint32_t WsprRefUnpacker::extract_m(
        const uint8_t *payload_bits,
        std::size_t payload_bit_count) const
    {
        (void)payload_bit_count;

        uint32_t m = 0;

        for (std::size_t i = 28; i < 50; ++i)
        {
            m = (m << 1) | static_cast<uint32_t>(payload_bits[i]);
        }

        return m;
    }

    void WsprRefUnpacker::unpack_callsign_type1(
        uint32_t n,
        std::string &callsign) const
    {
        char out[7] = {};

        out[5] = static_cast<char>((n % 27U) + 10U + 'A' - 10U);
        n /= 27U;

        out[4] = static_cast<char>((n % 27U) + 10U + 'A' - 10U);
        n /= 27U;

        out[3] = static_cast<char>((n % 27U) + 10U + 'A' - 10U);
        n /= 27U;

        out[2] = decode_wspr_base37(n % 10U);
        n /= 10U;

        out[1] = decode_wspr_base37(n % 36U);
        n /= 36U;

        out[0] = decode_wspr_base37(n);

        for (int i = 3; i <= 5; ++i)
        {
            if (out[i] == '[')
                out[i] = ' ';
        }

        callsign.assign(out, 6);

        while (!callsign.empty() && callsign.front() == ' ')
            callsign.erase(callsign.begin());

        while (!callsign.empty() && callsign.back() == ' ')
            callsign.pop_back();
    }

    bool WsprRefUnpacker::unpack_locator_power_type1(
        uint32_t m,
        std::string &locator,
        int &power_dbm) const
    {
        const uint32_t power_field = (m - 64U) % 128U;
        const uint32_t locator_field = (m - 64U) / 128U;

        power_dbm = static_cast<int>(power_field);

        if (!is_valid_wspr_power(power_dbm))
            return false;

        if (locator_field >= (180U * 180U))
            return false;

        const uint32_t a = locator_field / 180U;
        const uint32_t b = locator_field % 180U;

        const uint32_t d0 = 179U - a;
        const uint32_t d1 = b;

        const uint32_t l0 = d0 / 10U;
        const uint32_t l2 = d0 % 10U;

        const uint32_t l1 = d1 / 10U;
        const uint32_t l3 = d1 % 10U;

        if (l0 > 17U || l1 > 17U || l2 > 9U || l3 > 9U)
            return false;

        locator.clear();
        locator.push_back(static_cast<char>('A' + l0));
        locator.push_back(static_cast<char>('A' + l1));
        locator.push_back(static_cast<char>('0' + l2));
        locator.push_back(static_cast<char>('0' + l3));

        return true;
    }

    bool WsprRefUnpacker::unpack_type1(
        const uint8_t *payload_bits,
        std::size_t payload_bit_count,
        WsprDecodedMessage &message) const
    {
        message = WsprDecodedMessage{};

        if (payload_bits == nullptr)
        {
            message.error = "Null payload_bits.";
            return false;
        }

        if (payload_bit_count != WSPR_PAYLOAD_BIT_COUNT)
        {
            message.error = "payload_bit_count must equal WSPR_PAYLOAD_BIT_COUNT.";
            return false;
        }

        const uint32_t n = extract_n(payload_bits, payload_bit_count);
        const uint32_t m = extract_m(payload_bits, payload_bit_count);

        unpack_callsign_type1(n, message.callsign);

        if (!is_plausible_type1_callsign(message.callsign))
        {
            message.error = "Implausible Type 1 callsign.";
            return false;
        }

        if (!unpack_locator_power_type1(m, message.locator, message.power_dbm))
        {
            message.error = "Failed to unpack locator/power for Type 1.";
            return false;
        }

        message.valid = true;
        message.type = WsprMessageType::Type1;
        return true;
    }

    bool WsprRefUnpacker::decode_type2_power_and_offset(
        uint32_t low_bits,
        int &power_dbm,
        uint32_t &offset) const
    {
        static constexpr int valid_dbm[] = {
            0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37,
            40, 43, 47, 50, 53, 57, 60};

        for (int p : valid_dbm)
        {
            if (low_bits == static_cast<uint32_t>(p + 1))
            {
                power_dbm = p;
                offset = 1;
                return true;
            }

            if (low_bits == static_cast<uint32_t>(p + 2))
            {
                power_dbm = p;
                offset = 2;
                return true;
            }
        }

        return false;
    }

    std::string WsprRefUnpacker::decode_type2_suffix_one_char(
        uint32_t value) const
    {
        if (value <= 9U)
            return "/" + std::string(1, static_cast<char>('0' + value));

        if (value >= 10U && value <= 35U)
            return "/" + std::string(1, static_cast<char>('A' + (value - 10U)));

        if (value == 38U)
            return "/?";

        return {};
    }

    std::string WsprRefUnpacker::decode_type2_suffix_two_digit(
        uint32_t value) const
    {
        const uint32_t number = value;

        if (number > 99U)
            return {};

        std::string out = "/";
        out.push_back(static_cast<char>('0' + (number / 10U)));
        out.push_back(static_cast<char>('0' + (number % 10U)));
        return out;
    }

    std::string WsprRefUnpacker::decode_type2_prefix(
        uint32_t value) const
    {
        auto decode37 = [](uint32_t v) -> char
        {
            if (v <= 9U)
                return static_cast<char>('0' + v);

            if (v >= 10U && v <= 35U)
                return static_cast<char>('A' + (v - 10U));

            return ' ';
        };

        char prefix[4] = {};
        prefix[2] = decode37(value % 37U);
        value /= 37U;
        prefix[1] = decode37(value % 37U);
        value /= 37U;
        prefix[0] = decode37(value % 37U);
        prefix[3] = '\0';

        std::string out(prefix, 3);

        while (!out.empty() && out.front() == ' ')
            out.erase(out.begin());

        while (!out.empty() && out.back() == ' ')
            out.pop_back();

        if (out.empty())
            return {};

        return out + "/";
    }

    bool WsprRefUnpacker::unpack_type2(
        const uint8_t *payload_bits,
        std::size_t payload_bit_count,
        WsprDecodedMessage &message) const
    {
        message = WsprDecodedMessage{};

        if (payload_bits == nullptr)
        {
            message.error = "Null payload_bits.";
            return false;
        }

        if (payload_bit_count != WSPR_PAYLOAD_BIT_COUNT)
        {
            message.error = "Invalid payload size.";
            return false;
        }

        const uint32_t n = extract_n(payload_bits, payload_bit_count);

        const uint32_t m = extract_m(payload_bits, payload_bit_count);

        if (m < 64U)
        {
            message.error = "Type 2 payload is below minimum range.";
            return false;
        }

        const uint32_t raw = m - 64U;
        const uint32_t low_bits = raw % 128U;
        const uint32_t ext_field = raw / 128U;

        uint32_t offset = 0;

        if (!decode_type2_power_and_offset(low_bits, message.power_dbm, offset))
        {
            message.error = "Failed to decode Type 2 power field.";
            return false;
        }

        std::string extra;
        std::string alternate_extra;
        bool has_ambiguity = false;

        // Two-digit numeric suffix: /00 .. /99
        if (ext_field >= 27258U && ext_field <= 27357U)
        {
            if (offset != 2U)
            {
                message.error = "Invalid offset for Type 2 two-digit suffix.";
                return false;
            }

            extra = decode_type2_suffix_two_digit(ext_field - 27258U);

            if (is_type2_overlap_ext_field(ext_field))
            {
                const std::string overlap_one_char =
                    decode_type2_suffix_one_char(ext_field - 27232U);

                if (!overlap_one_char.empty())
                {
                    has_ambiguity = true;
                    alternate_extra = extra;
                    extra = overlap_one_char;
                }
            }
        }
        // One-character suffix: /0 .. /9, /A .. /Z, etc.
        else if (ext_field >= 27232U && ext_field <= 27270U)
        {
            if (offset != 2U)
            {
                message.error = "Invalid offset for Type 2 one-character suffix.";
                return false;
            }

            extra = decode_type2_suffix_one_char(ext_field - 27232U);
        }
        // Prefix: W0/, DL/, etc.
        else if (ext_field <= 32767U)
        {
            uint32_t prefix_value = ext_field;

            if (offset == 2U)
                prefix_value += 32768U;
            else if (offset != 1U)
            {
                message.error = "Invalid offset for Type 2 prefix.";
                return false;
            }

            extra = decode_type2_prefix(prefix_value);
        }
        else
        {
            message.error = "Unrecognized Type 2 extension field.";
            return false;
        }

        if (extra.empty())
        {
            message.error = "Failed to decode Type 2 extension.";
            return false;
        }

        std::string base_callsign;
        unpack_callsign_type1(n, base_callsign);

        message.callsign = format_type2_callsign("<hashed>", extra);
        message.extra = extra;
        message.callsign_hash = callsign_hash_from_base(base_callsign);
        message.has_hash = true;
        message.valid = true;
        message.type = WsprMessageType::Type2;
        message.is_partial = true;
        message.has_ambiguity = has_ambiguity;
        message.alternate_extra = alternate_extra;

        return true;
    }

    uint32_t WsprRefUnpacker::extract_type3_locator_value(
        const uint8_t *payload_bits,
        std::size_t payload_bit_count) const
    {
        (void)payload_bit_count;

        uint32_t n = 0;

        for (std::size_t i = 0; i < 28; ++i)
            n = (n << 1) | static_cast<uint32_t>(payload_bits[i]);

        return n;
    }

    uint32_t WsprRefUnpacker::extract_type3_hash_value(
        const uint8_t *payload_bits,
        std::size_t payload_bit_count) const
    {
        (void)payload_bit_count;

        uint32_t m = 0;

        for (std::size_t i = 28; i < 50; ++i)
            m = (m << 1) | static_cast<uint32_t>(payload_bits[i]);

        return m;
    }

    bool WsprRefUnpacker::unpack_locator_type3(
        uint32_t locator_value,
        std::string &locator) const
    {
        char out[7] = {};

        const uint32_t c5 = locator_value % 27U;
        locator_value /= 27U;

        const uint32_t c4 = locator_value % 27U;
        locator_value /= 27U;

        const uint32_t c3 = locator_value % 27U;
        locator_value /= 27U;

        const uint32_t c2 = locator_value % 10U;
        locator_value /= 10U;

        const uint32_t c1 = locator_value % 36U;
        locator_value /= 36U;

        const uint32_t c0 = locator_value;

        if (c0 > 35U || c1 > 35U || c2 > 9U || c3 > 26U || c4 > 26U || c5 > 26U)
            return false;

        auto decode36 = [](uint32_t v) -> char
        {
            if (v <= 9U)
                return static_cast<char>('0' + v);

            if (v <= 35U)
                return static_cast<char>('A' + (v - 10U));

            return ' ';
        };

        out[0] = decode36(c0);
        out[1] = decode36(c1);
        out[2] = static_cast<char>('0' + c2);
        out[3] = static_cast<char>('A' + c3);
        out[4] = static_cast<char>('A' + c4);
        out[5] = static_cast<char>('A' + c5);
        out[6] = '\0';

        locator.assign(out, 6);
        return true;
    }

    bool WsprRefUnpacker::unpack_type3(
        const uint8_t *payload_bits,
        std::size_t payload_bit_count,
        WsprDecodedMessage &message) const
    {
        message = WsprDecodedMessage{};

        if (payload_bits == nullptr)
        {
            message.error = "Null payload_bits.";
            return false;
        }

        if (payload_bit_count != WSPR_PAYLOAD_BIT_COUNT)
        {
            message.error = "payload_bit_count must equal WSPR_PAYLOAD_BIT_COUNT.";
            return false;
        }

        const uint32_t locator_value =
            extract_type3_locator_value(payload_bits, payload_bit_count);

        const uint32_t m =
            extract_type3_hash_value(payload_bits, payload_bit_count);

        const uint32_t hash = m / 128U;
        const uint32_t packed_power = m % 128U;

        if (packed_power > 63U)
        {
            message.error = "Invalid Type 3 packed power field.";
            return false;
        }

        if (!unpack_locator_type3(locator_value, message.locator))
        {
            message.error = "Failed to unpack Type 3 locator.";
            return false;
        }

        message.locator = unrotate_type3_locator(message.locator);

        message.power_dbm = 63 - static_cast<int>(packed_power);

        if (!is_valid_wspr_power(message.power_dbm))
        {
            message.error = "Invalid Type 3 power value.";
            return false;
        }

        message.callsign = "<hashed>";
        message.callsign_hash = hash;
        message.has_hash = true;
        message.valid = true;
        message.type = WsprMessageType::Type3;
        message.is_partial = true;

        return true;
    }
} // namespace wspr
