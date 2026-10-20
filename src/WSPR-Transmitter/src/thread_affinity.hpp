#pragma once

enum class ThreadAffinityStatus
{
    Applied,
    Unsupported,
    Failed,
};

struct ThreadAffinityResult
{
    ThreadAffinityStatus status = ThreadAffinityStatus::Unsupported;
    int error_number = 0;
};

bool thread_affinity_supported() noexcept;
ThreadAffinityResult pin_current_thread_to_cpu(int cpu) noexcept;
