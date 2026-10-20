/**
 * @file singleton.hpp
 * @brief Header-only C++ class to enforce singleton behavior in an application.
 *
 * @details
 * Prevents multiple instances of an application from running concurrently
 * by binding to a reserved loopback UDP port. If the bind succeeds, the
 * application is considered the "singleton" instance.
 *
 * Licensed under the repository-root LICENSE.md.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 */

#ifndef SINGLETON_H
#define SINGLETON_H

#include <unistd.h>     // close()
#include <cerrno>       // errno
#include <string>       // std::string, std::to_string()
#include <stdexcept>    // std::runtime_error
#include <system_error> // std::system_error
#include <memory>       // std::unique_ptr

#include <netinet/in.h> // sockaddr_in, INADDR_LOOPBACK, htons(), htonl()
#include <string.h>     // strerror

/**
 * @class SingletonException
 * @brief Custom exception thrown by SingletonProcess on fatal setup failure.
 *
 * @details
 * This exception class inherits from `std::runtime_error` and is used to
 * report initialization errors in the `SingletonProcess` class.
 */
class SingletonException : public std::runtime_error
{
public:
    /**
     * @brief Constructs a SingletonException with an error message.
     * @param message Description of the error.
     */
    explicit SingletonException(const std::string &message)
        : std::runtime_error(message) {}
};

/**
 * @class SingletonProcess
 * @brief Enforces singleton process behavior using a UDP port lock.
 *
 * @details
 * Attempts to bind to a loopback UDP port. If another process has already
 * bound to that port, the bind fails, indicating an instance is already running.
 * This class is useful in ensuring that only one copy of an application runs.
 *
 * @note This class is intended to be used as a one-liner check:
 * @code
 *   if (!SingletonProcess(12345)()) { std::exit(EXIT_FAILURE); }
 * @endcode
 */
class SingletonProcess
{
public:
    /**
     * @brief Constructs a SingletonProcess with a specified lock port.
     * @param port The loopback UDP port used to enforce singleton behavior.
     */
    explicit SingletonProcess(uint16_t port)
        : socket_fd_(-1), port_(port) {}

    /**
     * @brief Attempts to acquire the singleton lock by binding to the port.
     *
     * @details
     * Binds a UDP socket to the loopback interface on the specified port.
     * If the bind succeeds, this instance becomes the singleton owner.
     * If it fails (e.g., port in use), another instance is running.
     *
     * @return true if this is the singleton instance.
     * @return false if another instance is already running.
     */
    [[nodiscard]] bool operator()()
    {
        // Only attempt binding once per instance
        if (socket_fd_ == -1)
        {
            socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
            if (socket_fd_ < 0)
            {
                return false;
            }

            struct sockaddr_in name{};
            name.sin_family = AF_INET;
            name.sin_port = htons(port_);
            name.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            if (bind(socket_fd_,
                     reinterpret_cast<struct sockaddr *>(&name),
                     sizeof(name)) != 0)
            {
                close(socket_fd_);
                socket_fd_ = -1;
                return false;
            }
        }

        return socket_fd_ != -1;
    }

    /**
     * @brief Destructor that releases the lock.
     *
     * @details
     * Closes the socket to free the port, allowing future instances to run.
     */
    ~SingletonProcess()
    {
        if (socket_fd_ != -1)
        {
            close(socket_fd_);
            socket_fd_ = -1;
        }
    }

private:
    int socket_fd_; ///< File descriptor for the bound socket, or -1 if unbound.
    uint16_t port_; ///< UDP port number used for singleton enforcement.
};

#endif // SINGLETON_H
