#include "wspr_ref_correlator.hpp"

namespace wspr
{
    namespace
    {
        std::string resolved_callsign_token(std::string callsign)
        {
            constexpr const char *kHashedToken = "<hashed>";
            constexpr const char *kResolvedToken = "<callsign>";

            const std::size_t pos = callsign.find(kHashedToken);
            if (pos != std::string::npos)
            {
                callsign.replace(
                    pos,
                    std::char_traits<char>::length(kHashedToken),
                    kResolvedToken);
            }

            return callsign;
        }
    } // namespace

    void WsprRefCorrelator::add_message(const WsprDecodedMessage &message)
    {
        recent_messages_.push_back(message);

        constexpr std::size_t max_recent = 16;
        if (recent_messages_.size() > max_recent)
            recent_messages_.erase(recent_messages_.begin());
    }

    bool WsprRefCorrelator::try_resolve_last(
        WsprDecodedMessage &resolved_message) const
    {
        resolved_message = WsprDecodedMessage{};

        if (recent_messages_.size() < 2)
            return false;

        const WsprDecodedMessage &newest = recent_messages_.back();

        for (std::size_t i = recent_messages_.size() - 1; i-- > 0;)
        {
            const WsprDecodedMessage &candidate = recent_messages_[i];

            if (can_pair(newest, candidate) &&
                resolve_pair(newest, candidate, resolved_message))
            {
                return true;
            }
        }

        return false;
    }

    void WsprRefCorrelator::clear()
    {
        recent_messages_.clear();
    }

    bool WsprRefCorrelator::can_pair(
        const WsprDecodedMessage &a,
        const WsprDecodedMessage &b) const
    {
        const bool a_type2 = (a.type == WsprMessageType::Type2);
        const bool a_type3 = (a.type == WsprMessageType::Type3);
        const bool b_type2 = (b.type == WsprMessageType::Type2);
        const bool b_type3 = (b.type == WsprMessageType::Type3);

        if (!((a_type2 && b_type3) || (a_type3 && b_type2)))
            return false;

        if (!a.valid || !b.valid)
            return false;

        if (a.power_dbm != b.power_dbm)
            return false;

        if (a.has_hash && b.has_hash && a.callsign_hash != b.callsign_hash)
            return false;

        return true;
    }

    bool WsprRefCorrelator::resolve_pair(
        const WsprDecodedMessage &a,
        const WsprDecodedMessage &b,
        WsprDecodedMessage &resolved_message) const
    {
        const WsprDecodedMessage *type2 = nullptr;
        const WsprDecodedMessage *type3 = nullptr;

        if (a.type == WsprMessageType::Type2 &&
            b.type == WsprMessageType::Type3)
        {
            type2 = &a;
            type3 = &b;
        }
        else if (a.type == WsprMessageType::Type3 &&
                 b.type == WsprMessageType::Type2)
        {
            type2 = &b;
            type3 = &a;
        }
        else
        {
            return false;
        }

        resolved_message = WsprDecodedMessage{};
        resolved_message.valid = true;
        resolved_message.type = WsprMessageType::Type2;
        resolved_message.power_dbm = type2->power_dbm;
        resolved_message.locator = type3->locator;
        resolved_message.callsign_hash = type3->callsign_hash;
        resolved_message.has_hash = type3->has_hash;
        resolved_message.is_partial = false;

        if (!type2->callsign.empty() && type2->callsign != "<hashed>")
        {
            resolved_message.callsign =
                resolved_callsign_token(type2->callsign);
        }
        else
        {
            const std::string base_callsign =
                !type2->callsign.empty() ? type2->callsign : "<callsign>";

            resolved_message.callsign =
                resolved_callsign_token(
                    combine_callsign_and_extra(base_callsign, type2->extra));
        }

        resolved_message.extra = type2->extra;

        resolved_message.has_ambiguity = false;
        resolved_message.alternate_extra.clear();

        return true;
    }

    std::string WsprRefCorrelator::combine_callsign_and_extra(
        const std::string &callsign,
        const std::string &extra) const
    {
        if (extra.empty())
            return callsign;

        if (!extra.empty() && extra.back() == '/')
            return extra + callsign;

        if (!extra.empty() && extra.front() == '/')
            return callsign + extra;

        return callsign + extra;
    }
} // namespace wspr
