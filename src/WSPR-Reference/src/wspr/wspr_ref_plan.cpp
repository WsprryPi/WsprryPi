#include "wspr_ref_plan.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace
{
    std::string trim_copy(const std::string &value)
    {
        const auto begin = std::find_if_not(
            value.begin(),
            value.end(),
            [](unsigned char c)
            {
                return std::isspace(c) != 0;
            });

        const auto end = std::find_if_not(
                             value.rbegin(),
                             value.rend(),
                             [](unsigned char c)
                             {
                                 return std::isspace(c) != 0;
                             })
                             .base();

        if (begin >= end)
            return {};

        return std::string(begin, end);
    }

    std::string to_upper_copy(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::toupper(c));
            });

        return value;
    }

    bool is_ascii_alnum_or_slash_or_angle(char c)
    {
        const unsigned char uc = static_cast<unsigned char>(c);

        return std::isalnum(uc) != 0 ||
               c == '/' ||
               c == '<' ||
               c == '>';
    }

    bool contains_only_supported_callsign_chars(const std::string &callsign)
    {
        return std::all_of(
            callsign.begin(),
            callsign.end(),
            [](char c)
            {
                return is_ascii_alnum_or_slash_or_angle(c);
            });
    }

    bool is_explicit_type3_callsign(const std::string &callsign)
    {
        return callsign.size() >= 3 &&
               callsign.front() == '<' &&
               callsign.back() == '>';
    }

    bool is_compound_callsign(const std::string &callsign)
    {
        return callsign.find('/') != std::string::npos;
    }

    bool is_valid_locator_length(const std::string &locator)
    {
        return locator.size() == 4 || locator.size() == 6;
    }

    bool is_plausible_locator(const std::string &locator)
    {
        if (!is_valid_locator_length(locator))
            return false;

        const auto is_upper_alpha = [](char c)
        {
            return c >= 'A' && c <= 'Z';
        };

        const auto is_digit = [](char c)
        {
            return c >= '0' && c <= '9';
        };

        if (!is_upper_alpha(locator[0]) ||
            !is_upper_alpha(locator[1]) ||
            !is_digit(locator[2]) ||
            !is_digit(locator[3]))
        {
            return false;
        }

        if (locator.size() == 6)
        {
            const auto is_lower_alpha = [](char c)
            {
                return c >= 'A' && c <= 'Z';
            };

            if (!is_lower_alpha(locator[4]) ||
                !is_lower_alpha(locator[5]))
            {
                return false;
            }
        }

        return true;
    }

    bool is_likely_type1_callsign(const std::string &callsign)
    {
        if (callsign.empty())
            return false;

        if (callsign.size() > 6)
            return false;

        if (callsign.find('/') != std::string::npos)
            return false;

        if (callsign.find('<') != std::string::npos ||
            callsign.find('>') != std::string::npos)
        {
            return false;
        }

        return std::all_of(
            callsign.begin(),
            callsign.end(),
            [](unsigned char c)
            {
                return std::isalnum(c) != 0;
            });
    }

    wspr::TransmissionPlanResult make_error_result(
        wspr::TransmissionPlanStatus status,
        std::string message,
        std::string rationale,
        const std::string &callsign,
        const std::string &locator,
        int power_dbm,
        wspr::TransmissionPlanPreference preference)
    {
        wspr::TransmissionPlanResult result;
        result.ok = false;
        result.plan = wspr::TransmissionPlanType::Invalid;
        result.status = status;
        result.severity = wspr::TransmissionPlanSeverity::Error;
        result.applied_preference = preference;
        result.normalized_callsign = callsign;
        result.normalized_locator = locator;
        result.power_dbm = power_dbm;
        result.message = std::move(message);
        result.rationale = std::move(rationale);
        return result;
    }

    wspr::TransmissionPlanResult make_success_result(
        wspr::TransmissionPlanType plan,
        const std::string &callsign,
        const std::string &locator,
        int power_dbm,
        wspr::TransmissionPlanPreference preference,
        std::string message)
    {
        wspr::TransmissionPlanResult result;
        result.ok = true;
        result.plan = plan;
        result.status = wspr::TransmissionPlanStatus::Ok;
        result.severity = wspr::TransmissionPlanSeverity::Info;
        result.applied_preference = preference;
        result.normalized_callsign = callsign;
        result.normalized_locator = locator;
        result.power_dbm = power_dbm;
        result.message = std::move(message);
        result.requires_six_char_locator =
            (plan == wspr::TransmissionPlanType::Type3Single ||
             plan == wspr::TransmissionPlanType::Type2Type3Paired);
        return result;
    }
} // namespace

namespace wspr
{
    TransmissionPlanResult plan_transmission(
        const std::string &callsign,
        const std::string &locator,
        int power_dbm,
        TransmissionPlanPreference preference)
    {
        const std::string normalized_callsign =
            to_upper_copy(trim_copy(callsign));
        const std::string normalized_locator =
            to_upper_copy(trim_copy(locator));

        if (normalized_callsign.empty())
        {
            return make_error_result(
                TransmissionPlanStatus::InvalidCallsign,
                "Callsign must not be empty.",
                "Provide a callsign before requesting a transmission plan.",
                normalized_callsign,
                normalized_locator,
                power_dbm,
                preference);
        }

        if (!contains_only_supported_callsign_chars(normalized_callsign))
        {
            return make_error_result(
                TransmissionPlanStatus::InvalidCallsign,
                "Callsign contains unsupported characters.",
                "Only letters, digits, '/', '<', and '>' are supported.",
                normalized_callsign,
                normalized_locator,
                power_dbm,
                preference);
        }

        if (normalized_locator.empty() || !is_valid_locator_length(normalized_locator))
        {
            return make_error_result(
                TransmissionPlanStatus::InvalidLocator,
                "Locator must be 4 or 6 characters long.",
                "Use a 4-character locator for Type 1 and Type 2, or a 6-character locator for Type 3.",
                normalized_callsign,
                normalized_locator,
                power_dbm,
                preference);
        }

        if (!is_plausible_locator(normalized_locator))
        {
            return make_error_result(
                TransmissionPlanStatus::InvalidLocator,
                "Locator format is not valid.",
                "Expected 4-character or 6-character Maidenhead-style locator text.",
                normalized_callsign,
                normalized_locator,
                power_dbm,
                preference);
        }

        if (power_dbm < 0 || power_dbm > 60)
        {
            return make_error_result(
                TransmissionPlanStatus::InvalidPower,
                "Power must be within the supported range.",
                "Provide a power value between 0 and 60 dBm.",
                normalized_callsign,
                normalized_locator,
                power_dbm,
                preference);
        }

        if (is_explicit_type3_callsign(normalized_callsign))
        {
            if (normalized_locator.size() != 6)
            {
                auto result = make_error_result(
                    TransmissionPlanStatus::Type3RequiresSixCharLocator,
                    "Explicit Type 3 callsigns require a 6-character locator.",
                    "Use <CALLSIGN> only with a 6-character locator such as EM18IG.",
                    normalized_callsign,
                    normalized_locator,
                    power_dbm,
                    preference);
                result.requires_six_char_locator = true;
                return result;
            }

            if (preference == TransmissionPlanPreference::RequirePaired)
            {
                return make_error_result(
                    TransmissionPlanStatus::PairedTransmissionUnavailable,
                    "RequirePaired cannot be satisfied for explicit Type 3 callsigns.",
                    "Paired planning is only available for compound callsigns with a 6-character locator in the current design.",
                    normalized_callsign,
                    normalized_locator,
                    power_dbm,
                    preference);
            }

            auto result = make_success_result(
                TransmissionPlanType::Type3Single,
                normalized_callsign,
                normalized_locator,
                power_dbm,
                preference,
                "Planned explicit Type 3 transmission.");
            result.type2_callsign.clear();
            result.type2_locator.clear();
            result.type3_callsign.clear();
            result.type3_locator.clear();
            return result;
        }

        if (is_compound_callsign(normalized_callsign))
        {
            if (normalized_locator.size() == 6 &&
                (preference == TransmissionPlanPreference::Auto ||
                 preference == TransmissionPlanPreference::PreferPaired ||
                 preference == TransmissionPlanPreference::RequirePaired))
            {
                auto result = make_success_result(
                    TransmissionPlanType::Type2Type3Paired,
                    normalized_callsign,
                    normalized_locator,
                    power_dbm,
                    preference,
                    "Planned paired Type 2 and Type 3 transmission.");

                result.requires_correlation = true;
                result.pairing_requested = true;
                result.pairing_required =
                    (preference == TransmissionPlanPreference::RequirePaired);
                result.requires_six_char_locator = true;

                result.type2_callsign = normalized_callsign;
                result.type2_locator = normalized_locator.substr(0, 4);

                result.type3_callsign = "<" + normalized_callsign + ">";
                result.type3_locator = normalized_locator;

                result.primary_extra.clear();
                result.alternate_extra.clear();

                return result;
            }

            if (normalized_locator.size() != 4)
            {
                if (preference == TransmissionPlanPreference::RequirePaired)
                {
                    return make_error_result(
                        TransmissionPlanStatus::PairedTransmissionUnavailable,
                        "RequirePaired cannot be satisfied without a 6-character locator for a compound callsign.",
                        "Paired planning is only available for compound callsigns with a 6-character locator in the current design.",
                        normalized_callsign,
                        normalized_locator,
                        power_dbm,
                        preference);
                }

                return make_error_result(
                    TransmissionPlanStatus::InvalidLocator,
                    "Compound callsigns require either a 4-character locator for single Type 2 or a 6-character locator for paired planning.",
                    "Use a 4-character locator for Type 2 single-message transmission, or use a 6-character locator with PreferPaired or RequirePaired.",
                    normalized_callsign,
                    normalized_locator,
                    power_dbm,
                    preference);
            }

            auto result = make_success_result(
                TransmissionPlanType::Type2Single,
                normalized_callsign,
                normalized_locator,
                power_dbm,
                preference,
                "Planned Type 2 single-message transmission.");
            result.type2_callsign.clear();
            result.type2_locator.clear();
            result.type3_callsign.clear();
            result.type3_locator.clear();

            if (preference == TransmissionPlanPreference::PreferPaired ||
                preference == TransmissionPlanPreference::RequirePaired)
            {
                result.pairing_requested = true;
                result.severity = TransmissionPlanSeverity::Warning;
                result.rationale =
                    "Paired transmission was requested, but a 6-character locator is required to produce a paired Type 2 and Type 3 plan.";
            }

            if (preference == TransmissionPlanPreference::RequirePaired)
            {
                return make_error_result(
                    TransmissionPlanStatus::PairedTransmissionUnavailable,
                    "RequirePaired cannot be satisfied for a compound callsign with a 4-character locator.",
                    "Paired planning is only available for compound callsigns with a 6-character locator in the current design.",
                    normalized_callsign,
                    normalized_locator,
                    power_dbm,
                    preference);
            }

            return result;
        }

        if (normalized_locator.size() == 6)
        {
            if (preference == TransmissionPlanPreference::RequirePaired)
            {
                return make_error_result(
                    TransmissionPlanStatus::PairedTransmissionUnavailable,
                    "RequirePaired cannot be satisfied for a non-compound callsign with a 6-character locator.",
                    "Paired planning is only available for compound callsigns with a 6-character locator in the current design.",
                    normalized_callsign,
                    normalized_locator,
                    power_dbm,
                    preference);
            }

            auto result = make_error_result(
                TransmissionPlanStatus::BareLongCallsignRequiresExplicitType3,
                "Bare long callsigns must use explicit Type 3 form.",
                "Use <CALLSIGN> with a 6-character locator for Type 3 planning.",
                normalized_callsign,
                normalized_locator,
                power_dbm,
                preference);
            result.requires_six_char_locator = true;
            return result;
        }

        if (!is_likely_type1_callsign(normalized_callsign))
        {
            auto result = make_error_result(
                TransmissionPlanStatus::BareLongCallsignRequiresExplicitType3,
                "Callsign does not fit standard Type 1 planning.",
                "Use explicit Type 3 form such as <LONGCALL> for extended identities.",
                normalized_callsign,
                normalized_locator,
                power_dbm,
                preference);
            result.requires_six_char_locator = true;
            return result;
        }

        if (normalized_locator.size() != 4)
        {
            return make_error_result(
                TransmissionPlanStatus::Type1RequiresFourCharLocator,
                "Type 1 planning requires a 4-character locator.",
                "Use a standard 4-character locator for Type 1 transmission planning.",
                normalized_callsign,
                normalized_locator,
                power_dbm,
                preference);
        }

        if (preference == TransmissionPlanPreference::RequirePaired)
        {
            auto result = make_error_result(
                TransmissionPlanStatus::PairedTransmissionUnavailable,
                "RequirePaired cannot be satisfied for a standard Type 1 callsign.",
                "Paired planning is only available for compound callsigns with a 6-character locator in the current design.",
                normalized_callsign,
                normalized_locator,
                power_dbm,
                preference);
            result.pairing_requested = true;
            result.pairing_required = false;
            result.requires_six_char_locator = true;
            return result;
        }

        auto result = make_success_result(
            TransmissionPlanType::Type1Single,
            normalized_callsign,
            normalized_locator,
            power_dbm,
            preference,
            "Planned Type 1 single-message transmission.");
        result.type2_callsign.clear();
        result.type2_locator.clear();
        result.type3_callsign.clear();
        result.type3_locator.clear();

        if (preference == TransmissionPlanPreference::PreferPaired ||
            preference == TransmissionPlanPreference::RequirePaired)
        {
            result.pairing_requested = true;
            result.severity = TransmissionPlanSeverity::Warning;
            result.rationale =
                "Paired transmission was requested, but this first-pass planner currently selects Type 1 single-message mode for standard callsigns.";
        }

        return result;
    }

} // namespace wspr
