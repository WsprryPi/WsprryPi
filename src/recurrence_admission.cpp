// SPDX-License-Identifier: MIT
#include "recurrence_admission.hpp"
#include <mutex>
namespace wsprrypi {
namespace {std::mutex mutex;std::function<LegacyRecurrenceAdmission()> provider;}
void set_legacy_recurrence_admission(std::function<LegacyRecurrenceAdmission()> replacement){
    std::lock_guard lock(mutex);provider=std::move(replacement);
}
LegacyRecurrenceAdmission legacy_recurrence_admission() noexcept {
    try{
        std::function<LegacyRecurrenceAdmission()> check;{std::lock_guard lock(mutex);check=provider;}
        return check?check():LegacyRecurrenceAdmission::Open;
    }catch(...){return LegacyRecurrenceAdmission::Held;}
}
bool legacy_recurrence_admitted() noexcept {return legacy_recurrence_admission()==LegacyRecurrenceAdmission::Open;}
}
