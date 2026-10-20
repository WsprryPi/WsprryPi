#pragma once

#include <cstring>

inline bool qualification_gpio_test_mode_requested(const char *value) noexcept
{
    return value != nullptr && std::strcmp(value, "1") == 0;
}
