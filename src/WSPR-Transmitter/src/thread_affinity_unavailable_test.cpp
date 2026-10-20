#include "thread_affinity.hpp"

#include <cassert>

int main()
{
    assert(!thread_affinity_supported());
    const ThreadAffinityResult result = pin_current_thread_to_cpu(0);
    assert(result.status == ThreadAffinityStatus::Unsupported);
    assert(result.error_number == 0);
}
