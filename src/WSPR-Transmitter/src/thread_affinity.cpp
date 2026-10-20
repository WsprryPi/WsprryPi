#include "thread_affinity.hpp"

#if defined(__linux__) && !defined(WSPRRYPI_FORCE_UNAVAILABLE_AFFINITY_FOR_TEST)

#include <pthread.h>
#include <sched.h>

bool thread_affinity_supported() noexcept
{
    return true;
}

ThreadAffinityResult pin_current_thread_to_cpu(int cpu) noexcept
{
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(cpu, &cpus);

    const int result = pthread_setaffinity_np(
        pthread_self(),
        sizeof(cpus),
        &cpus);
    if (result != 0)
    {
        return {ThreadAffinityStatus::Failed, result};
    }
    return {ThreadAffinityStatus::Applied, 0};
}

#elif defined(__APPLE__) || defined(WSPRRYPI_FORCE_UNAVAILABLE_AFFINITY_FOR_TEST)

bool thread_affinity_supported() noexcept
{
    return false;
}

ThreadAffinityResult pin_current_thread_to_cpu(int) noexcept
{
    return {ThreadAffinityStatus::Unsupported, 0};
}

#else
#error "Transmit-thread CPU affinity is supported only on Linux; other platforms require an explicit unavailable implementation"
#endif
