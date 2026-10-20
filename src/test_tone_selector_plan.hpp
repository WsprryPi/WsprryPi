#pragma once
#include "config_handler.hpp"
#include "band_gpio.hpp"
#include <string>
#include <vector>
enum class TestToneSelectorSource { FrequencyEntry, BandConfiguration, None };
struct TestToneSelectorPlan { HamBand band; bool enabled=false; BandGPIOConfig config{}; TestToneSelectorSource source=TestToneSelectorSource::None; int entry_index=-1; };
struct TestToneSelectorPlanResult { TestToneSelectorPlan plan{}; std::string error; explicit operator bool()const{return error.empty();} };
TestToneSelectorPlanResult plan_test_tone_selector(HamBand, const std::vector<WsprFrequencyEntry>&, const std::array<BandGPIOConfig,HAM_BAND_COUNT>&);
