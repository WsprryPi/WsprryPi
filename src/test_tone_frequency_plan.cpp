#include "test_tone_frequency_plan.hpp"
#include "band_lookup.hpp"
#include <cmath>
#include <limits>
TestToneFrequencyPlanResult plan_explicit_test_tone_frequency(const ParsedTestToneRequest&r,double offset,const std::string& profile,const WsprBandPreferences& preferences){
 auto fail=[](const char*s){return TestToneFrequencyPlanResult{{},s};}; BandLookup l;
 if(r.source!=TestToneRequestSource::WsprBand&&r.source!=TestToneRequestSource::CustomRf)return fail("legacy request is not handled by explicit planner");
 if(r.source==TestToneRequestSource::CustomRf){if(!r.frequency_hz||!l.lookup_ham_band(static_cast<double>(*r.frequency_hz)))return fail("custom RF is outside supported amateur bands"); auto b=*l.lookup_ham_band(static_cast<double>(*r.frequency_hz));return {{TestToneFrequencyPlan{r.source,ham_band_to_string(b),{}, {},*r.frequency_hz,WsprBandResolutionSource::BuiltInPreset,{}}}, {}};}
 const double uint64_exclusive_upper_bound=std::ldexp(1.0,std::numeric_limits<std::uint64_t>::digits);
 if(!std::isfinite(offset)||offset<0||std::trunc(offset)!=offset||offset>=uint64_exclusive_upper_bound)return fail("audio offset must be a non-negative integral Hz value");
 const auto offset_hz=static_cast<std::uint64_t>(offset);
 const auto resolved=l.resolve_wspr_band_frequency(r.band,profile,preferences);if(resolved){if(offset_hz>std::numeric_limits<std::uint64_t>::max()-resolved->dial_frequency_hz)return fail("audio offset overflows RF frequency");return {TestToneFrequencyPlan{r.source,resolved->band,resolved->dial_frequency_hz,offset_hz,resolved->dial_frequency_hz+offset_hz,resolved->source,resolved->preset}, {}};}
 if(r.band=="22m")return fail("22m is not in the amateur correlation catalog");
 return fail("band must be canonical");
}
