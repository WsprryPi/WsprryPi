/**
 * @file mailbox.hpp
 * @brief C++20 `Mailbox` class to interface with the Broadcom GPU mailbox.
 *
 * This project is licensed under the MIT License. See the repository root
 * LICENSE.md for more information.
 *
 * Copyright © 2025 - 2026 Lee C. Bussy (@LBussy). All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _MAILBOX_HPP
#define _MAILBOX_HPP
#pragma once

// C++ Standard Library
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

#include "mailbox_revision.hpp"

// POSIX/system headers
#include <linux/ioctl.h> // for IOCTL_MBOX_PROPERTY

class Mailbox
{
public:
    /**
     * @brief Default-constructs a Mailbox instance.
     *
     * The mailbox device is not opened by the constructor;
     * call open() to open the underlying `/dev/vcio` interface.
     */
    Mailbox();

    /**
     * @brief Destructs the Mailbox instance.
     *
     * Closes the mailbox device if it is currently open, ensuring
     * that resources are released properly.
     */
    ~Mailbox() noexcept;

    /**
     * @brief Opens the mailbox device.
     *
     * Attempts to open the mailbox device file specified by DEVICE_FILE_NAME.
     * On failure to open the file, throws a system_error with the errno
     * and a descriptive message.  Idempotent, multiple open() are NOPs.
     *
     * @throws std::system_error  If the underlying open() call fails.
     */
    void open();

    /**
     * @brief Closes the mailbox device.
     *
     * If the mailbox file descriptor is valid, attempts to close it.
     *
     * This function is noexcept so it can be safely called from the destructor.
     */
    void close() noexcept;

    /**
     * @brief Get the underlying mailbox file descriptor.
     *
     * @return The file descriptor obtained via `open()`, or -1 if the mailbox
     *         is closed.
     */
    [[nodiscard]] int getFD() const noexcept { return fd_; }

    [[nodiscard]] uint32_t memAlloc(uint32_t size, uint32_t align);
    uint32_t memFree(uint32_t handle);
    [[nodiscard]] std::uintptr_t memLock(uint32_t handle);
    uint32_t memUnlock(uint32_t handle);

    /**
     * @brief Map a physical address range into user-space memory.
     *
     * Opens `/dev/mem`, aligns the requested `base` address to the system page
     * size, and mmaps a region of length `size` bytes. The returned pointer is
     * offset by the original `base % PAGE_SIZE` so that it points directly at
     * the requested address.
     *
     * Note: The internal mapping length is `size + offset` so that the returned
     * pointer always has at least `size` bytes of valid space.
     *
     * @param base Physical address to map; will be aligned down to a PAGE_SIZE
     *             boundary.
     * @param size Number of bytes to map starting at `base`.
     * @return Pointer to mapped region, adjusted by the page offset.
     * @throws std::system_error if opening `/dev/mem` or mmap fails.
     */
    [[nodiscard]] volatile uint8_t *mapMem(std::uintptr_t base, std::size_t size);

    /**
     * @brief Unmap a previously mapped address region.
     *
     * Calculates the original mapping base by removing the page offset from the
     * pointer returned by mapMem(), then calls munmap() to release the mapping.
     *
     * Note: This unmaps `size + offset` to match mapMem().
     *
     * @param addr Pointer returned by mapMem().
     * @param size Size passed to mapMem().
     * @throws std::system_error if munmap fails.
     */
    void unMapMem(volatile uint8_t *addr, std::size_t size);

    /**
     * @brief Determine the SoC peripheral base address from the device tree.
     *
     * Attempts to parse `/proc/device-tree/soc/ranges` in common 32-bit and
     * 64-bit cell formats. Returns the physical peripheral base suitable for
     * mapping via `/dev/mem`.
     *
     * If parsing fails, falls back to the legacy address 0x2000'0000.
     *
     * @return Physical peripheral base address.
     */
    [[nodiscard]] static uint32_t discoverPeripheralBase();

    [[nodiscard]] static constexpr std::uintptr_t
    busToPhysical(std::uintptr_t x) noexcept
    {
        return x & ~BUS_FLAG_MASK;
    }

    [[nodiscard]] static constexpr std::uintptr_t
    offsetFromBase(std::uintptr_t x) noexcept
    {
        return x - PERIPH_BUS_BASE;
    }

    static constexpr std::uintptr_t BUS_FLAG_MASK = 0xC0000000ULL;
    static constexpr std::uintptr_t PERIPH_BUS_BASE = 0x7E000000ULL;
    static constexpr std::size_t PAGE_SIZE = 4 * 1024;
    static constexpr std::size_t BLOCK_SIZE = 4 * 1024;

private:
    static inline constexpr int MAJOR_NUM_A = 249;
    static inline constexpr int MAJOR_NUM_B = 100;
    static inline constexpr int IOCTL_MBOX_PROPERTY =
        _IOWR(MAJOR_NUM_B, 0, char *);

    static inline constexpr char DEVICE_FILE_NAME[] = "/dev/vcio";
    static inline constexpr char MEM_FILE_NAME[] = "/dev/mem";

    int fd_ = -1;
    mailbox_detail::RevisionResolver revision_resolver_;

    [[nodiscard]] uint32_t get_mem_flag();

    static std::optional<uint32_t>
    read_dt_range_helper(const char *path, std::size_t offset);
};

extern Mailbox mailbox;

#endif // _MAILBOX_HPP
