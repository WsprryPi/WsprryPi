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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>     // close()
#include <netinet/in.h>
#endif
#include <cerrno>       // errno
#include <string>       // std::string, std::to_string()
#include <stdexcept>    // std::runtime_error
#include <system_error> // std::system_error
#include <memory>       // std::unique_ptr


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
 *   SingletonProcess guard(12345);
 *   if (!guard()) { std::exit(EXIT_FAILURE); }
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
        : socket_fd_(invalid_socket), port_(port) {}

    SingletonProcess(const SingletonProcess &)=delete;
    SingletonProcess &operator=(const SingletonProcess &)=delete;

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
        if (socket_fd_ == invalid_socket)
        {
#ifdef _WIN32
            if(!winsock_started_) {
                WSADATA data{};
                if(::WSAStartup(MAKEWORD(2,2),&data)!=0)return false;
                winsock_started_=true;
            }
#endif
            socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
            if (socket_fd_ == invalid_socket)
            {
                return false;
            }

#ifdef _WIN32
            const BOOL exclusive=TRUE;
            if(::setsockopt(socket_fd_,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,
                reinterpret_cast<const char*>(&exclusive),sizeof exclusive)!=0){close_socket(socket_fd_);socket_fd_=invalid_socket;return false;}
#endif
            struct sockaddr_in name{};
            name.sin_family = AF_INET;
            name.sin_port = htons(port_);
            name.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            if (bind(socket_fd_,
                     reinterpret_cast<struct sockaddr *>(&name),
                     sizeof(name)) != 0)
            {
                close_socket(socket_fd_);
                socket_fd_ = invalid_socket;
                return false;
            }
        }

        return socket_fd_ != invalid_socket;
    }

    /**
     * @brief Destructor that releases the lock.
     *
     * @details
     * Closes the socket to free the port, allowing future instances to run.
     */
    ~SingletonProcess()
    {
        if (socket_fd_ != invalid_socket)
        {
            close_socket(socket_fd_);
            socket_fd_ = invalid_socket;
        }
#ifdef _WIN32
        if(winsock_started_)::WSACleanup();
#endif
    }

private:
#ifdef _WIN32
    using Socket=SOCKET;
    static constexpr Socket invalid_socket=INVALID_SOCKET;
    bool winsock_started_{};
    static void close_socket(Socket socket){::closesocket(socket);}
#else
    using Socket=int;
    static constexpr Socket invalid_socket=-1;
    static void close_socket(Socket socket){::close(socket);}
#endif
    Socket socket_fd_; ///< File descriptor for the bound socket, or -1 if unbound.
    uint16_t port_; ///< UDP port number used for singleton enforcement.
};

#endif // SINGLETON_H
