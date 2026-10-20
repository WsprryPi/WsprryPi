#ifndef WSPR_REF_PLAN_HPP
#define WSPR_REF_PLAN_HPP

#include <string>
#include <string_view>

namespace wspr
{
    /**
     * @brief User preference for transmission planning
     *
     * This enum allows the user to express their preference for single or paired
     * transmission modes, which the planner will use to select the most suitable
     * plan that matches their intent.
     */
    enum class TransmissionPlanPreference
    {
        Auto = 0,
        RequireSingle,
        PreferSingle,
        PreferPaired,
        RequirePaired
    };

    /**
     * @brief Supported transmission planning outcomes
     *
     * This enum describes the transmission shape required to represent the
     * user's intended identity and metadata correctly.
     */
    enum class TransmissionPlanType
    {
        Invalid = 0,
        Type1Single,
        Type2Single,
        Type3Single,
        Type2Type3Paired
    };

    /**
     * @brief Machine-readable planning status codes
     *
     * These values are intended for daemon logic, UI mapping, and logging.
     * They should remain stable once exposed across process boundaries.
     */
    enum class TransmissionPlanStatus
    {
        Ok = 0,

        InvalidCallsign,
        InvalidLocator,
        InvalidPower,

        Type1RequiresFourCharLocator,
        Type3RequiresSixCharLocator,

        BareLongCallsignRequiresExplicitType3,
        UnsupportedCompoundCallsign,
        CompoundCallsignRequiresCorrelation,
        PairedTransmissionUnavailable,

        AmbiguousType2Overlap,

        InternalError
    };

    /**
     * @brief Severity associated with a planning result
     *
     * This allows the caller to distinguish between informational results,
     * warnings that still permit transmission, and hard errors.
     */
    enum class TransmissionPlanSeverity
    {
        Info = 0,
        Warning,
        Error
    };

    /**
     * @brief Structured result returned by transmission planning
     *
     * This object is designed to be stable enough for use across a daemon/UI
     * boundary while still being convenient inside library code.
     */
    struct TransmissionPlanResult
    {
        /**
         * @brief True when the input is valid and a usable plan was produced
         */
        bool ok = false;

        /**
         * @brief Transmission plan selected by the planner
         */
        TransmissionPlanType plan = TransmissionPlanType::Invalid;

        /**
         * @brief Detailed machine-readable status code
         */
        TransmissionPlanStatus status = TransmissionPlanStatus::InternalError;

        /**
         * @brief Severity for UI and daemon handling
         */
        TransmissionPlanSeverity severity = TransmissionPlanSeverity::Error;

        /**
         * @brief User preference that influenced the selected plan
         */
        TransmissionPlanPreference applied_preference = TransmissionPlanPreference::Auto;

        /**
         * @brief True when paired Type 2 and Type 3 transmission is required
         */
        bool requires_correlation = false;

        /**
         * @brief True when the selected mode requires a 6-character locator
         */
        bool requires_six_char_locator = false;

        /**
         * @brief True when the planned Type 2 interpretation has overlap ambiguity
         */
        bool has_ambiguity = false;

        /**
         * @brief Callsign after normalization and validation
         */
        std::string normalized_callsign;

        /**
         * @brief Locator after normalization and validation
         */
        std::string normalized_locator;

        /**
         * @brief Requested transmit power in dBm
         */
        int power_dbm = 0;

        /**
         * @brief Type 2 callsign after normalization and validation
         */
        std::string type2_callsign;

        /**
         * @brief Type 2 locator when the plan involves a Type 2 transmission
         */
        std::string type2_locator;

        /**
         * @brief Type 3 callsign after normalization and validation
         */
        std::string type3_callsign;

        /**
         * @brief Type 3 locator when the plan involves a Type 3 transmission
         */
        std::string type3_locator;

        /**
         * @brief Primary decoded or planned Type 2 extra field when relevant
         */
        std::string primary_extra;

        /**
         * @brief Alternate Type 2 extra field for overlap-region ambiguity
         */
        std::string alternate_extra;

        /**
         * @brief Human-readable explanation suitable for logs or UI display
         */
        std::string message;

        /**
         * @brief Optional short rationale for debugging or UI detail text
         */
        std::string rationale;

        /**
         * @brief True when the user requested pairing, even if it was not strictly required
         */
        bool pairing_requested = false;

        /**
         * @brief True when the planner determined that pairing is required to represent the user's intent
         */
        bool pairing_required = false;
    };

    /**
     * @brief Convert a transmission plan type to stable text
     *
     * @param value Enum value to convert
     * @return Stable string name for logging, JSON, or UI mapping
     */
    [[nodiscard]] constexpr std::string_view
    to_string(TransmissionPlanType value) noexcept
    {
        switch (value)
        {
        case TransmissionPlanType::Invalid:
            return "Invalid";
        case TransmissionPlanType::Type1Single:
            return "Type1Single";
        case TransmissionPlanType::Type2Single:
            return "Type2Single";
        case TransmissionPlanType::Type3Single:
            return "Type3Single";
        case TransmissionPlanType::Type2Type3Paired:
            return "Type2Type3Paired";
        }

        return "Invalid";
    }

    /**
     * @brief Convert a transmission plan status to stable text
     *
     * @param value Enum value to convert
     * @return Stable string name for logging, JSON, or UI mapping
     */
    [[nodiscard]] constexpr std::string_view
    to_string(TransmissionPlanStatus value) noexcept
    {
        switch (value)
        {
        case TransmissionPlanStatus::Ok:
            return "Ok";
        case TransmissionPlanStatus::InvalidCallsign:
            return "InvalidCallsign";
        case TransmissionPlanStatus::InvalidLocator:
            return "InvalidLocator";
        case TransmissionPlanStatus::InvalidPower:
            return "InvalidPower";
        case TransmissionPlanStatus::Type1RequiresFourCharLocator:
            return "Type1RequiresFourCharLocator";
        case TransmissionPlanStatus::Type3RequiresSixCharLocator:
            return "Type3RequiresSixCharLocator";
        case TransmissionPlanStatus::BareLongCallsignRequiresExplicitType3:
            return "BareLongCallsignRequiresExplicitType3";
        case TransmissionPlanStatus::UnsupportedCompoundCallsign:
            return "UnsupportedCompoundCallsign";
        case TransmissionPlanStatus::CompoundCallsignRequiresCorrelation:
            return "CompoundCallsignRequiresCorrelation";
        case TransmissionPlanStatus::AmbiguousType2Overlap:
            return "AmbiguousType2Overlap";
        case TransmissionPlanStatus::PairedTransmissionUnavailable:
            return "PairedTransmissionUnavailable";
        case TransmissionPlanStatus::InternalError:
            return "InternalError";
        }

        return "InternalError";
    }

    /**
     * @brief Convert a transmission plan severity to stable text
     *
     * @param value Enum value to convert
     * @return Stable string name for logging, JSON, or UI mapping
     */
    [[nodiscard]] constexpr std::string_view
    to_string(TransmissionPlanSeverity value) noexcept
    {
        switch (value)
        {
        case TransmissionPlanSeverity::Info:
            return "Info";
        case TransmissionPlanSeverity::Warning:
            return "Warning";
        case TransmissionPlanSeverity::Error:
            return "Error";
        }

        return "Error";
    }

    /**
     * @brief Convert a transmission plan preference to stable text
     *
     * @param value Enum value to convert
     * @return Stable string name for logging, JSON, or UI mapping
     */
    [[nodiscard]] constexpr std::string_view
    to_string(TransmissionPlanPreference value) noexcept
    {
        switch (value)
        {
        case TransmissionPlanPreference::Auto:
            return "Auto";
        case TransmissionPlanPreference::RequireSingle:
            return "RequireSingle";
        case TransmissionPlanPreference::PreferSingle:
            return "PreferSingle";
        case TransmissionPlanPreference::PreferPaired:
            return "PreferPaired";
        case TransmissionPlanPreference::RequirePaired:
            return "RequirePaired";
        }

        return "Auto";
    }

    /**
     * @brief Plan the transmission strategy for a callsign, locator, and power.
     *
     * @param callsign Requested callsign text.
     * @param locator Requested locator text.
     * @param power_dbm Requested power in dBm.
     * @param preference User preference for single or paired transmission.
     * @return Structured planning result describing the selected transmission mode.
     */
    [[nodiscard]] TransmissionPlanResult plan_transmission(
        const std::string &callsign,
        const std::string &locator,
        int power_dbm,
        TransmissionPlanPreference preference =
            TransmissionPlanPreference::Auto);

} // namespace wspr

#endif // WSPR_REF_PLAN_HPP
