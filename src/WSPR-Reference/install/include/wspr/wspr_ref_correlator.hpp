#ifndef WSPR_REF_CORRELATOR_HPP
#define WSPR_REF_CORRELATOR_HPP

/// \file wspr_ref_correlator.hpp
/// \brief Public correlator for pairing compatible decoded WSPR messages.

#include "wspr_ref_unpack.hpp"

#include <vector>

namespace wspr
{
    /// \brief Correlates recently added decoded messages into a resolved result.
    class WsprRefCorrelator
    {
    public:
        /// \brief Add a decoded message to the correlator state.
        void add_message(const WsprDecodedMessage &message);

        /// \brief Try to resolve the most recent compatible message pair.
        /// \param resolved_message Receives the correlated message on success.
        /// \return True if a compatible pair was found and resolved.
        bool try_resolve_last(WsprDecodedMessage &resolved_message) const;

        /// \brief Remove all retained decoded messages.
        void clear();

    private:
        bool can_pair(
            const WsprDecodedMessage &a,
            const WsprDecodedMessage &b) const;

        bool resolve_pair(
            const WsprDecodedMessage &a,
            const WsprDecodedMessage &b,
            WsprDecodedMessage &resolved_message) const;

        std::string combine_callsign_and_extra(
            const std::string &callsign,
            const std::string &extra) const;

        std::vector<WsprDecodedMessage> recent_messages_;
    };
} // namespace wspr

#endif // WSPR_REF_CORRELATOR_HPP
