#include "wtp_endpoint/system_clock.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <time.h>

#ifdef __linux__
#include <sys/timex.h>
#endif

namespace wsprrypi {
namespace {
std::uint64_t ns(const timespec& value) {
    if (value.tv_sec < 0 || value.tv_nsec < 0 || value.tv_nsec >= 1'000'000'000)
        return 0;
    const auto seconds = static_cast<std::uint64_t>(value.tv_sec);
    if (seconds > (std::numeric_limits<std::uint64_t>::max() -
                   static_cast<std::uint64_t>(value.tv_nsec)) / 1'000'000'000ULL)
        return 0;
    return seconds * 1'000'000'000ULL +
           static_cast<std::uint64_t>(value.tv_nsec);
}
} // namespace

wsprrypico::wtp::ClockSnapshot WtpPiSystemClock::snapshot() const {
    using namespace wsprrypico::wtp;
    ClockSnapshot result;
    timespec before{}, utc{}, after{};
    if (clock_gettime(CLOCK_MONOTONIC, &before) != 0 ||
        clock_gettime(CLOCK_REALTIME, &utc) != 0 ||
        clock_gettime(CLOCK_MONOTONIC, &after) != 0)
        return result;
    const auto first = ns(before), last = ns(after);
    result.utc_now_ns = ns(utc);
    result.monotonic_now_ns = first + (last >= first ? (last - first) / 2 : 0);
    if (!first || !last || last < first || !result.utc_now_ns)
        return result;
#ifdef __linux__
    timex estimate{};
    const int status = adjtimex(&estimate);
    if (status < 0 || status == TIME_ERROR ||
        (estimate.status & (STA_UNSYNC | STA_CLOCKERR)) ||
        estimate.maxerror < 0)
        return result;
    result.state = ClockState::Synchronized;
    result.leap = estimate.status & STA_INS ? LeapState::InsertPending :
                  estimate.status & STA_DEL ? LeapState::DeletePending :
                  LeapState::Normal;
    const auto kernel_error = static_cast<std::uint64_t>(estimate.maxerror);
    if (kernel_error > (std::numeric_limits<std::uint64_t>::max() -
                        (last - first) / 2) / 1000ULL)
        return ClockSnapshot{};
    result.uncertainty_ns = kernel_error * 1000ULL + (last - first) / 2;
    result.sync_age_ns = 0; // Kernel maxerror already accumulates age.
#endif
    return result;
}
} // namespace wsprrypi
