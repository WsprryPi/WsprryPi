#ifndef WSPR_REF_UNPACK_HPP
#define WSPR_REF_UNPACK_HPP

/// \file wspr_ref_unpack.hpp
/// \brief Public message model and payload unpacking helpers.

#include <cstddef>
#include <cstdint>
#include <string>

namespace wspr
{
    /// \brief Supported WSPR message classifications.
    enum class WsprMessageType
    {
        Unknown = 0,
        Type1,
        Type2,
        Type3
    };

    /// \brief Structured decoded representation of a WSPR message.
    struct WsprDecodedMessage
    {
        /// \brief True when the decoded message contents are valid.
        bool valid = false;
        /// \brief Recognized WSPR message type.
        WsprMessageType type = WsprMessageType::Unknown;

        /// \brief Callsign or placeholder callsign text.
        std::string callsign;
        /// \brief Locator for Type 1 or Type 3 messages when available.
        std::string locator;
        /// \brief Decoded transmit power in dBm.
        int power_dbm = 0;

        /// \brief Type 2 suffix or prefix fragment when present.
        std::string extra;

        /// \brief Decoded callsign hash value when available.
        uint32_t callsign_hash = 0;
        /// \brief True when \ref callsign_hash contains a meaningful value.
        bool has_hash = false;

        /// \brief True when the decoded message is intentionally partial.
        bool is_partial = false;

        /// \brief True when a Type 2 overlap decode has a preserved alternate interpretation.
        bool has_ambiguity = false;
        /// \brief Alternate Type 2 suffix preserved for ambiguous overlap cases.
        std::string alternate_extra;

        /// \brief Error text associated with the decoded message, if any.
        std::string error;
    };

    /// \brief Unpacks recovered payload bits into supported WSPR message forms.
    class WsprRefUnpacker
    {
    public:
        /// \brief Attempt to unpack a Type 1 message from payload bits.
        /// \param payload_bits Input payload bits.
        /// \param payload_bit_count Number of bits available in \p payload_bits.
        /// \param message Receives the decoded message on success.
        /// \return True if the payload matches a valid Type 1 message.
        bool unpack_type1(
            const uint8_t *payload_bits,
            std::size_t payload_bit_count,
            WsprDecodedMessage &message) const;

        /// \brief Attempt to unpack a Type 2 message from payload bits.
        /// \param payload_bits Input payload bits.
        /// \param payload_bit_count Number of bits available in \p payload_bits.
        /// \param message Receives the decoded message on success.
        /// \return True if the payload matches a valid Type 2 message.
        bool unpack_type2(
            const uint8_t *payload_bits,
            std::size_t payload_bit_count,
            WsprDecodedMessage &message) const;

        /// \brief Attempt to unpack a Type 3 message from payload bits.
        /// \param payload_bits Input payload bits.
        /// \param payload_bit_count Number of bits available in \p payload_bits.
        /// \param message Receives the decoded message on success.
        /// \return True if the payload matches a valid Type 3 message.
        bool unpack_type3(
            const uint8_t *payload_bits,
            std::size_t payload_bit_count,
            WsprDecodedMessage &message) const;

    private:
        bool decode_type2_power_and_offset(
            uint32_t low_bits,
            int &power_dbm,
            uint32_t &offset) const;

        std::string decode_type2_suffix_one_char(
            uint32_t value) const;

        std::string decode_type2_suffix_two_digit(
            uint32_t value) const;

        std::string decode_type2_prefix(
            uint32_t value) const;

        uint32_t extract_n(
            const uint8_t *payload_bits,
            std::size_t payload_bit_count) const;

        uint32_t extract_m(
            const uint8_t *payload_bits,
            std::size_t payload_bit_count) const;

        void unpack_callsign_type1(
            uint32_t n,
            std::string &callsign) const;

        bool unpack_locator_power_type1(
            uint32_t m,
            std::string &locator,
            int &power_dbm) const;

        uint32_t extract_type3_locator_value(
            const uint8_t *payload_bits,
            std::size_t payload_bit_count) const;

        uint32_t extract_type3_hash_value(
            const uint8_t *payload_bits,
            std::size_t payload_bit_count) const;

        bool unpack_locator_type3(
            uint32_t locator_value,
            std::string &locator) const;
    };
} // namespace wspr

#endif // WSPR_REF_UNPACK_HPP
