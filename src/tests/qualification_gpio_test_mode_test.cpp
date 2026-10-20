#include "qualification_gpio_test_mode.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    void require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    require(!qualification_gpio_test_mode_requested(nullptr),
            "an absent qualification GPIO test mode value must be disabled");

    for (const char *value : {"", "0", "true", "yes", "invalid", "01", "1 ", " 1"})
    {
        require(!qualification_gpio_test_mode_requested(value),
                std::string("qualification GPIO test mode must reject '") + value + "'");
    }

    require(qualification_gpio_test_mode_requested("1"),
            "only exact value 1 must enable qualification GPIO test mode");

    std::cout << "qualification GPIO test mode value semantics passed" << std::endl;
    return EXIT_SUCCESS;
}
