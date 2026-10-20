#include "test_tone_selector_plan.hpp"
#include "band_lookup.hpp"
TestToneSelectorPlanResult plan_test_tone_selector(HamBand band,const std::vector<WsprFrequencyEntry>& entries,const std::array<BandGPIOConfig,HAM_BAND_COUNT>& gpio){
 TestToneSelectorPlan p; p.band=band; BandLookup lookup; int found=-1; BandGPIOConfig explicit_cfg{};
 for(size_t i=0;i<entries.size();++i){const auto&e=entries[i];if(e.dial_frequency_hz<=0||lookup.lookup_ham_band(e.dial_frequency_hz)!=std::optional<HamBand>(band)||e.selector_gpio==kSelectorGpioUnset)continue;BandGPIOConfig c{e.selector_gpio,true,e.selector_gpio_active_high};if(!is_valid_selector_gpio(c.gpio))return {{},"invalid selector GPIO"};if(found>=0&&(c.gpio!=explicit_cfg.gpio||c.active_high!=explicit_cfg.active_high))return {{},"conflicting same-band selector definitions"};found=i;explicit_cfg=c;}
 if(found>=0){p.enabled=true;p.config=explicit_cfg;p.source=TestToneSelectorSource::FrequencyEntry;p.entry_index=found;return {p,{}};} auto c=gpio[ham_band_index(band)];if(c.enabled){if(!is_valid_selector_gpio(c.gpio))return {{},"invalid band selector GPIO"};p.enabled=true;p.config=c;p.source=TestToneSelectorSource::BandConfiguration;}return {p,{}};
}
