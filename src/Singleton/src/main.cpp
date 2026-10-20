/**
 * @file main.cpp
 * @brief A test application for Singleton, a C++ header-only class to enforce
 *        a Singleton condition for an application.
 *
 * Licensed under the repository-root LICENSE.md.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 */

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "singleton.hpp"

/**
 * @brief Tests the SingletonProcess class with a given port.
 *
 * Verifies whether the SingletonProcess instance can be created and whether
 * binding to the same port is prevented as expected.
 *
 * @param port The port number to use for testing.
 */
static void test_singleton(uint16_t port)
{
    try
    {
        std::cout << "\nTesting SingletonProcess with port " << port << ".\n";

        SingletonProcess singleton(port);

        if (singleton())
        {
            std::cout << "Singleton instance created successfully on "
                      << port << ".\n";
        }
        else
        {
            std::cerr << "Singleton instance creation failed.\n";
        }

        std::cout << "Testing binding to the same port (should fail).\n";
        try
        {
            SingletonProcess singleton_fail(port);
            if (singleton_fail())
            {
                std::cerr << "Error: Bound to the same port unexpectedly.\n";
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Expected failure: " << e.what() << "\n";
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

/**
 * @brief Tests cross-process singleton enforcement by launching a child copy.
 *
 * The parent acquires the singleton first. It then forks and execs the same
 * program as a child. The child attempts to acquire the singleton and should
 * fail. The parent checks the child's exit status.
 *
 * @param port The port number to use for testing.
 * @param self_path Path to the current executable (argv[0]).
 */
static void test_spawn_second_process(uint16_t port, const char *self_path)
{
    try
    {
        std::cout << "\nTesting cross-process singleton enforcement.\n";

        SingletonProcess singleton(port);
        if (!singleton())
        {
            std::cerr << "Error: Failed to create primary singleton instance.\n";
            return;
        }

        std::cout << "Primary singleton acquired. Spawning child process.\n";

        pid_t pid = fork();
        if (pid < 0)
        {
            std::perror("fork");
            return;
        }

        if (pid == 0)
        {
            char port_arg[16];
            std::snprintf(port_arg, sizeof(port_arg), "%u",
                          static_cast<unsigned>(port));

            execl(self_path, self_path, "--child", port_arg, nullptr);

            std::perror("execl");
            _exit(127);
        }

        int status = 0;
        if (waitpid(pid, &status, 0) < 0)
        {
            std::perror("waitpid");
            return;
        }

        if (!WIFEXITED(status))
        {
            std::cerr << "Error: Child did not exit normally.\n";
            return;
        }

        const int code = WEXITSTATUS(status);
        if (code == 1)
        {
            std::cout << "Child correctly failed to acquire the singleton.\n";
        }
        else
        {
            std::cerr << "Error: Child exit code was " << code << ".\n";
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

/**
 * @brief Simulates a permission error by trying to bind to a restricted port.
 *
 * Attempts to create a SingletonProcess instance on a port that typically
 * requires root privileges (e.g., port 23) to ensure the proper error is raised.
 */
static void simulate_permission_error()
{
    try
    {
        constexpr uint16_t restricted_port = 23; // Telnet, usually restricted
        std::cout << "\nTesting SingletonProcess on a restricted port ("
                  << restricted_port << ").\n";

        SingletonProcess singleton(restricted_port);

        if (singleton())
        {
            std::cerr << "Error: Bound to a restricted port unexpectedly.\n";
        }
    }
    catch (const SingletonException &e)
    {
        std::cerr << "Expected failure (singleton error): " << e.what() << "\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Expected failure (generic error): " << e.what() << "\n";
    }
}

/**
 * @brief Child mode entry point for cross-process testing.
 *
 * Attempts to acquire the singleton on the given port. Returns 1 if the
 * singleton is correctly rejected, or 0 if it unexpectedly succeeds.
 *
 * @param port The port number to use for testing.
 * @return Exit status (1 for expected rejection, 0 for unexpected success).
 */
static int child_mode_try_acquire(uint16_t port)
{
    try
    {
        SingletonProcess singleton(port);
        if (singleton())
            return 0;

        return 1;
    }
    catch (const std::exception &)
    {
        return 1;
    }
}

/**
 * @brief Main function to run SingletonProcess tests.
 *
 * Executes tests for SingletonProcess class functionality, including creating
 * an instance on a user-specified port, spawning a second process to validate
 * cross-process enforcement, and simulating a permission error on a restricted
 * port.
 *
 * @param argc The argument count.
 * @param argv The argument values.
 * @return Exit status (0 for success, non-zero for error).
 */
int main(int argc, char *argv[])
{
    bool child_mode = false;
    uint16_t test_port = 8080; // Default test port

    if (argc > 1 && std::strcmp(argv[1], "--child") == 0)
        child_mode = true;

    if (child_mode)
    {
        if (argc > 2)
        {
            test_port = static_cast<uint16_t>(std::atoi(argv[2]));
            if (test_port == 0)
                test_port = 8080;
        }

        return child_mode_try_acquire(test_port);
    }

    if (argc > 1)
    {
        test_port = static_cast<uint16_t>(std::atoi(argv[1]));
        if (test_port == 0)
        {
            std::cerr << "Invalid port specified. Defaulting to port 8080.\n";
            test_port = 8080;
        }
    }

    std::cout << "===========================\n";
    std::cout << "Testing SingletonProcess.\n";
    std::cout << "===========================\n";

    test_singleton(test_port);
    test_spawn_second_process(test_port, argv[0]);
    simulate_permission_error();

    std::cout << "\nSingletonProcess test completed.\n";
    return 0;
}
