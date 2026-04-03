#ifndef FREQUENCY_SEMANTICS_HPP
#define FREQUENCY_SEMANTICS_HPP

enum class FrequencyPath
{
    DirectRf,
    WsprDial
};

// Semantic boundary helper:
// - External WSPR configuration uses dial frequency.
// - RF generation uses actual transmit frequency.
// - Direct-RF paths bypass the WSPR audio offset.
inline double resolve_actual_rf_frequency_hz(
    double dial_hz,
    double wspr_audio_offset_hz,
    FrequencyPath path)
{
    if (path == FrequencyPath::WsprDial && dial_hz != 0.0)
    {
        return dial_hz + wspr_audio_offset_hz;
    }

    return dial_hz;
}

#endif
