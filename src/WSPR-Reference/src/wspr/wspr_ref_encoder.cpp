#include "wspr_ref_encoder.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

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

    uint32_t nhash_(const void *key, int *length0, uint32_t *initval0)
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
            case 12:
                c += k[2];
                b += k[1];
                a += k[0];
                break;
            case 11:
                c += k[2] & 0x00ffffffU;
                b += k[1];
                a += k[0];
                break;
            case 10:
                c += k[2] & 0x0000ffffU;
                b += k[1];
                a += k[0];
                break;
            case 9:
                c += k[2] & 0x000000ffU;
                b += k[1];
                a += k[0];
                break;
            case 8:
                b += k[1];
                a += k[0];
                break;
            case 7:
                b += k[1] & 0x00ffffffU;
                a += k[0];
                break;
            case 6:
                b += k[1] & 0x0000ffffU;
                a += k[0];
                break;
            case 5:
                b += k[1] & 0x000000ffU;
                a += k[0];
                break;
            case 4:
                a += k[0];
                break;
            case 3:
                a += k[0] & 0x00ffffffU;
                break;
            case 2:
                a += k[0] & 0x0000ffffU;
                break;
            case 1:
                a += k[0] & 0x000000ffU;
                break;
            case 0:
                return c;
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
            case 12:
                c += k[4] + (static_cast<uint32_t>(k[5]) << 16);
                b += k[2] + (static_cast<uint32_t>(k[3]) << 16);
                a += k[0] + (static_cast<uint32_t>(k[1]) << 16);
                break;
            case 11:
                c += static_cast<uint32_t>(k8[10]) << 16;
                [[fallthrough]];
            case 10:
                c += k[4];
                b += k[2] + (static_cast<uint32_t>(k[3]) << 16);
                a += k[0] + (static_cast<uint32_t>(k[1]) << 16);
                break;
            case 9:
                c += k8[8];
                [[fallthrough]];
            case 8:
                b += k[2] + (static_cast<uint32_t>(k[3]) << 16);
                a += k[0] + (static_cast<uint32_t>(k[1]) << 16);
                break;
            case 7:
                b += static_cast<uint32_t>(k8[6]) << 16;
                [[fallthrough]];
            case 6:
                b += k[2];
                a += k[0] + (static_cast<uint32_t>(k[1]) << 16);
                break;
            case 5:
                b += k8[4];
                [[fallthrough]];
            case 4:
                a += k[0] + (static_cast<uint32_t>(k[1]) << 16);
                break;
            case 3:
                a += static_cast<uint32_t>(k8[2]) << 16;
                [[fallthrough]];
            case 2:
                a += k[0];
                break;
            case 1:
                a += k8[0];
                break;
            case 0:
                return c;
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
            case 12:
                c += static_cast<uint32_t>(k[11]) << 24;
                [[fallthrough]];
            case 11:
                c += static_cast<uint32_t>(k[10]) << 16;
                [[fallthrough]];
            case 10:
                c += static_cast<uint32_t>(k[9]) << 8;
                [[fallthrough]];
            case 9:
                c += k[8];
                [[fallthrough]];
            case 8:
                b += static_cast<uint32_t>(k[7]) << 24;
                [[fallthrough]];
            case 7:
                b += static_cast<uint32_t>(k[6]) << 16;
                [[fallthrough]];
            case 6:
                b += static_cast<uint32_t>(k[5]) << 8;
                [[fallthrough]];
            case 5:
                b += k[4];
                [[fallthrough]];
            case 4:
                a += static_cast<uint32_t>(k[3]) << 24;
                [[fallthrough]];
            case 3:
                a += static_cast<uint32_t>(k[2]) << 16;
                [[fallthrough]];
            case 2:
                a += static_cast<uint32_t>(k[1]) << 8;
                [[fallthrough]];
            case 1:
                a += k[0];
                break;
            case 0:
                return c;
            }
        }

        final_mix(a, b, c);
        return c;
    }
} // namespace

namespace wspr
{
    void WsprRefEncoder::wspr_encode(const char *call, const char *loc, int8_t dbm, uint8_t *symbols)
    {
        char call_[13] = {};
        char loc_[7] = {};
        uint8_t dbm_ = static_cast<uint8_t>(dbm);
        std::strcpy(call_, call);
        std::strcpy(loc_, loc);

        wspr_message_prep(call_, loc_, dbm_);

        uint8_t c[11];
        wspr_bit_packing(c);

        uint8_t s[WSPR_SYMBOL_COUNT];
        convolve(c, s, 11, WSPR_BIT_COUNT);
        wspr_interleave(s);
        wspr_merge_sync_vector(s, symbols);
    }

    uint8_t WsprRefEncoder::wspr_code(char c) const
    {
        if (std::isdigit(static_cast<unsigned char>(c)))
            return static_cast<uint8_t>(c - 48);
        if (c == ' ')
            return 36;
        if (c >= 'A' && c <= 'Z')
            return static_cast<uint8_t>(c - 55);
        return 36;
    }

    void WsprRefEncoder::wspr_message_prep(char *call, char *loc, int8_t dbm)
    {
        uint8_t i;
        for (i = 0; i < 12 && call[i] != '\0'; i++)
        {
            if (call[i] != '/' && call[i] != '<' && call[i] != '>')
            {
                call[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(call[i])));
                if (!(std::isdigit(static_cast<unsigned char>(call[i])) || std::isupper(static_cast<unsigned char>(call[i]))))
                    call[i] = ' ';
            }
        }
        call[12] = 0;

        std::strncpy(callsign_, call, 12);
        callsign_[12] = '\0';

        if (std::strlen(loc) == 4 || std::strlen(loc) == 6)
        {
            for (i = 0; i <= 1; i++)
            {
                loc[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(loc[i])));
                if (loc[i] < 'A' || loc[i] > 'R')
                    std::strncpy(loc, "AA00AA", 7);
            }
            for (i = 2; i <= 3; i++)
            {
                if (!std::isdigit(static_cast<unsigned char>(loc[i])))
                    std::strncpy(loc, "AA00AA", 7);
            }
        }
        else
        {
            std::strncpy(loc, "AA00AA", 7);
        }

        if (std::strlen(loc) == 6)
        {
            for (i = 4; i <= 5; i++)
            {
                loc[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(loc[i])));
                if (loc[i] < 'A' || loc[i] > 'X')
                    std::strncpy(loc, "AA00AA", 7);
            }
        }

        std::strncpy(locator_, loc, 6);
        locator_[6] = '\0';

        if (dbm > 60)
            dbm = 60;

        constexpr uint8_t valid_dbm_size = 28;
        const int8_t valid_dbm[valid_dbm_size] = {
            -30, -27, -23, -20, -17, -13, -10, -7, -3,
            0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40,
            43, 47, 50, 53, 57, 60};

        for (i = 0; i < valid_dbm_size; i++)
        {
            if (dbm == valid_dbm[i])
                power_ = dbm;
        }
        for (i = 1; i < valid_dbm_size; i++)
        {
            if (dbm < valid_dbm[i] && dbm >= valid_dbm[i - 1])
                power_ = valid_dbm[i - 1];
        }
    }

    void WsprRefEncoder::wspr_bit_packing(uint8_t *c)
    {
        uint32_t n = 0;
        uint32_t m = 0;

        char *slash_avail = std::strchr(callsign_, '/');
        if (callsign_[0] == '<')
        {
            char base_call[13];
            std::memset(base_call, 0, 13);
            uint32_t init_val = 146;
            char *bracket_avail = std::strchr(callsign_, '>');
            int call_len = static_cast<int>(bracket_avail - callsign_ - 1);
            std::strncpy(base_call, callsign_ + 1, static_cast<std::size_t>(call_len));
            uint32_t hash = nhash_(base_call, &call_len, &init_val);
            hash &= 32767;

            char temp_loc = locator_[0];
            locator_[0] = locator_[1];
            locator_[1] = locator_[2];
            locator_[2] = locator_[3];
            locator_[3] = locator_[4];
            locator_[4] = locator_[5];
            locator_[5] = temp_loc;

            n = wspr_code(locator_[0]);
            n = n * 36 + wspr_code(locator_[1]);
            n = n * 10 + wspr_code(locator_[2]);
            n = n * 27 + (wspr_code(locator_[3]) - 10);
            n = n * 27 + (wspr_code(locator_[4]) - 10);
            n = n * 27 + (wspr_code(locator_[5]) - 10);

            m = (hash * 128) - (power_ + 1) + 64;
        }
        else if (slash_avail == nullptr)
        {
            pad_callsign(callsign_);
            n = wspr_code(callsign_[0]);
            n = n * 36 + wspr_code(callsign_[1]);
            n = n * 10 + wspr_code(callsign_[2]);
            n = n * 27 + (wspr_code(callsign_[3]) - 10);
            n = n * 27 + (wspr_code(callsign_[4]) - 10);
            n = n * 27 + (wspr_code(callsign_[5]) - 10);

            m = ((179 - 10 * (locator_[0] - 'A') - (locator_[2] - '0')) * 180) +
                (10 * (locator_[1] - 'A')) + (locator_[3] - '0');
            m = (m * 128) + power_ + 64;
        }
        else
        {
            int slash_pos = static_cast<int>(slash_avail - callsign_);
            uint8_t i;

            if (callsign_[slash_pos + 2] == ' ' || callsign_[slash_pos + 2] == 0)
            {
                char base_call[7];
                std::memset(base_call, 0, 7);
                std::strncpy(base_call, callsign_, static_cast<std::size_t>(slash_pos));
                for (i = 0; i < 7; i++)
                {
                    base_call[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(base_call[i])));
                    if (!(std::isdigit(static_cast<unsigned char>(base_call[i])) || std::isupper(static_cast<unsigned char>(base_call[i]))))
                        base_call[i] = ' ';
                }
                pad_callsign(base_call);

                n = wspr_code(base_call[0]);
                n = n * 36 + wspr_code(base_call[1]);
                n = n * 10 + wspr_code(base_call[2]);
                n = n * 27 + (wspr_code(base_call[3]) - 10);
                n = n * 27 + (wspr_code(base_call[4]) - 10);
                n = n * 27 + (wspr_code(base_call[5]) - 10);

                char x = callsign_[slash_pos + 1];
                if (x >= 48 && x <= 57)
                {
                    x -= 48;
                }
                else if (x >= 65 && x <= 90)
                {
                    x -= 55;
                }
                else
                {
                    x = 38;
                }

                m = 60000 - 32768 + x;
                m = (m * 128) + power_ + 2 + 64;
            }
            else if (callsign_[slash_pos + 3] == ' ' || callsign_[slash_pos + 3] == 0)
            {
                char base_call[7];
                std::memset(base_call, 0, 7);
                std::strncpy(base_call, callsign_, static_cast<std::size_t>(slash_pos));
                for (i = 0; i < 6; i++)
                {
                    base_call[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(base_call[i])));
                    if (!(std::isdigit(static_cast<unsigned char>(base_call[i])) || std::isupper(static_cast<unsigned char>(base_call[i]))))
                        base_call[i] = ' ';
                }
                pad_callsign(base_call);

                n = wspr_code(base_call[0]);
                n = n * 36 + wspr_code(base_call[1]);
                n = n * 10 + wspr_code(base_call[2]);
                n = n * 27 + (wspr_code(base_call[3]) - 10);
                n = n * 27 + (wspr_code(base_call[4]) - 10);
                n = n * 27 + (wspr_code(base_call[5]) - 10);

                m = 10 * (callsign_[slash_pos + 1] - 48) + callsign_[slash_pos + 2] - 48;
                m = 60000 + 26 + m;
                m = (m * 128) + power_ + 2 + 64;
            }
            else
            {
                char prefix[4];
                char base_call[7];
                std::memset(prefix, 0, 4);
                std::memset(base_call, 0, 7);
                ::strncpy(prefix, callsign_, static_cast<std::size_t>(slash_pos));
                std::snprintf(
                    base_call,
                    sizeof(base_call),
                    "%.*s",
                    static_cast<int>(sizeof(base_call) - 1),
                    callsign_ + slash_pos + 1);

                if (prefix[2] == ' ' || prefix[2] == 0)
                {
                    prefix[3] = 0;
                    prefix[2] = prefix[1];
                    prefix[1] = prefix[0];
                    prefix[0] = ' ';
                }

                for (uint8_t j = 0; j < 6; ++j)
                {
                    base_call[j] = static_cast<char>(std::toupper(static_cast<unsigned char>(base_call[j])));
                    if (!(std::isdigit(static_cast<unsigned char>(base_call[j])) || std::isupper(static_cast<unsigned char>(base_call[j]))))
                        base_call[j] = ' ';
                }
                pad_callsign(base_call);

                n = wspr_code(base_call[0]);
                n = n * 36 + wspr_code(base_call[1]);
                n = n * 10 + wspr_code(base_call[2]);
                n = n * 27 + (wspr_code(base_call[3]) - 10);
                n = n * 27 + (wspr_code(base_call[4]) - 10);
                n = n * 27 + (wspr_code(base_call[5]) - 10);

                m = 0;
                for (uint8_t j = 0; j < 3; ++j)
                    m = 37 * m + wspr_code(prefix[j]);

                if (m >= 32768)
                {
                    m -= 32768;
                    m = (m * 128) + power_ + 2 + 64;
                }
                else
                {
                    m = (m * 128) + power_ + 1 + 64;
                }
            }
        }

        c[3] = static_cast<uint8_t>((n & 0x0fU) << 4);
        n = n >> 4;
        c[2] = static_cast<uint8_t>(n & 0xffU);
        n = n >> 8;
        c[1] = static_cast<uint8_t>(n & 0xffU);
        n = n >> 8;
        c[0] = static_cast<uint8_t>(n & 0xffU);

        c[6] = static_cast<uint8_t>((m & 0x03U) << 6);
        m = m >> 2;
        c[5] = static_cast<uint8_t>(m & 0xffU);
        m = m >> 8;
        c[4] = static_cast<uint8_t>(m & 0xffU);
        m = m >> 8;
        c[3] |= static_cast<uint8_t>(m & 0x0fU);
        c[7] = 0;
        c[8] = 0;
        c[9] = 0;
        c[10] = 0;
    }

    void WsprRefEncoder::wspr_interleave(uint8_t *s) const
    {
        uint8_t d[WSPR_BIT_COUNT];
        uint8_t rev, index_temp, i, j, k;

        i = 0;
        for (j = 0; j < 255; j++)
        {
            index_temp = j;
            rev = 0;

            for (k = 0; k < 8; k++)
            {
                if (index_temp & 0x01)
                    rev = rev | (1 << (7 - k));
                index_temp = index_temp >> 1;
            }

            if (rev < WSPR_BIT_COUNT)
            {
                d[rev] = s[i];
                i++;
            }

            if (i >= WSPR_BIT_COUNT)
                break;
        }

        std::memcpy(s, d, WSPR_BIT_COUNT);
    }

    void WsprRefEncoder::wspr_merge_sync_vector(
        const uint8_t *g,
        uint8_t *symbols) const
    {
        for (std::size_t i = 0; i < WSPR_SYMBOL_COUNT; ++i)
            symbols[i] = SYNC_VECTOR[i] + (2 * g[i]);
    }

    void WsprRefEncoder::convolve(const uint8_t *c, uint8_t *s, uint8_t message_size, uint8_t bit_size) const
    {
        uint32_t reg_0 = 0;
        uint32_t reg_1 = 0;
        uint32_t reg_temp = 0;
        uint8_t input_bit, parity_bit;
        uint8_t bit_count = 0;
        uint8_t i, j, k;

        for (i = 0; i < message_size; i++)
        {
            for (j = 0; j < 8; j++)
            {
                input_bit = (((c[i] << j) & 0x80) == 0x80) ? 1 : 0;

                reg_0 = reg_0 << 1;
                reg_1 = reg_1 << 1;
                reg_0 |= static_cast<uint32_t>(input_bit);
                reg_1 |= static_cast<uint32_t>(input_bit);

                reg_temp = reg_0 & 0xf2d05351U;
                parity_bit = 0;
                for (k = 0; k < 32; k++)
                {
                    parity_bit = parity_bit ^ (reg_temp & 0x01U);
                    reg_temp = reg_temp >> 1;
                }
                s[bit_count] = parity_bit;
                bit_count++;

                reg_temp = reg_1 & 0xe4613c47U;
                parity_bit = 0;
                for (k = 0; k < 32; k++)
                {
                    parity_bit = parity_bit ^ (reg_temp & 0x01U);
                    reg_temp = reg_temp >> 1;
                }
                s[bit_count] = parity_bit;
                bit_count++;
                if (bit_count >= bit_size)
                    break;
            }
        }
    }

    void WsprRefEncoder::pad_callsign(char *call) const
    {
        if (std::isdigit(static_cast<unsigned char>(call[1])) &&
            std::isupper(static_cast<unsigned char>(call[2])))
        {
            call[5] = call[4];
            call[4] = call[3];
            call[3] = call[2];
            call[2] = call[1];
            call[1] = call[0];
            call[0] = ' ';
        }
    }

    bool WsprRefEncoder::debug_get_payload_bits(
        const char *call,
        const char *loc,
        int8_t dbm,
        uint8_t *payload_bits) const
    {
        if (call == nullptr || loc == nullptr || payload_bits == nullptr)
            return false;

        char call_[13] = {};
        char loc_[7] = {};
        uint8_t dbm_ = static_cast<uint8_t>(dbm);

        std::strncpy(call_, call, 12);
        call_[12] = '\0';

        std::strncpy(loc_, loc, 6);
        loc_[6] = '\0';

        WsprRefEncoder temp_encoder;
        temp_encoder.wspr_message_prep(call_, loc_, dbm_);

        uint8_t c[11] = {};
        temp_encoder.wspr_bit_packing(c);

        std::size_t out_index = 0;

        for (std::size_t byte_index = 0; byte_index < 11 && out_index < WSPR_PAYLOAD_BIT_COUNT; ++byte_index)
        {
            for (uint8_t bit = 0; bit < 8 && out_index < WSPR_PAYLOAD_BIT_COUNT; ++bit)
            {
                payload_bits[out_index++] =
                    static_cast<uint8_t>(((c[byte_index] << bit) & 0x80U) ? 1U : 0U);
            }
        }

        return (out_index == WSPR_PAYLOAD_BIT_COUNT);
    }
} // namespace wspr
