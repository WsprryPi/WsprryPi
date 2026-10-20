/**
 * @file test.cpp
 * @brief Bounded loopback test for SingletonProcess.
 *
 * Licensed under the repository-root LICENSE.md.
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 */

#include "singleton.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>

namespace
{
std::uint16_t reserve_available_port()
{
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        throw std::runtime_error("unable to create loopback discovery socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(0);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
    {
        ::close(fd);
        throw std::runtime_error("unable to reserve an available loopback port");
    }

    socklen_t length = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) != 0)
    {
        ::close(fd);
        throw std::runtime_error("unable to inspect the reserved loopback port");
    }

    const std::uint16_t port = ntohs(address.sin_port);
    ::close(fd);
    return port;
}
}

int main()
{
    try
    {
        const std::uint16_t port = reserve_available_port();
        {
            SingletonProcess first(port);
            SingletonProcess second(port);
            if (!first() || second() || !first())
            {
                std::cerr << "First/second singleton acquisition contract failed.\n";
                return 1;
            }
        }

        SingletonProcess afterRelease(port);
        if (!afterRelease())
        {
            std::cerr << "Singleton port was not released at scope exit.\n";
            return 1;
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "Singleton test setup failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
