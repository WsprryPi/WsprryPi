#pragma once

#include "rpi_standard_feld_execution.hpp"

namespace wsprrypi
{
// Hardware-independent owner of the immutable Standard Feld progress identity.
// Plan identity is installed once; reports supply only their generation, event
// index, and compiler-produced raster record.
class RpiStandardFeldProgressBridge final
{
public:
    bool prepare(const ExecutionPlan& plan, std::uint64_t generation);
    bool report(std::uint64_t generation, std::size_t event_index,
                const RfEvent::RasterProgress& progress);
    bool finalize(std::uint64_t generation,
                  const RpiStandardFeldExecutionResult& result);
    void clear();
    RpiStandardFeldProgressSnapshot snapshot() const;
#ifdef STANDARD_FELD_PROGRESS_TEST_HOOKS
    std::size_t completed_capacity_for_test() const;
#endif

private:
    RpiStandardFeldProgressStore store_{};
};
} // namespace wsprrypi
