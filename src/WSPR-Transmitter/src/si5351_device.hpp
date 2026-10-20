/**
 * @file si5351_device.hpp
 * @brief Low-level Linux I2C access helper for Si5351 devices.
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

#ifndef SI5351_DEVICE_HPP
#define SI5351_DEVICE_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <sys/types.h>
#include <vector>

/**
 * @brief Low-level Si5351 register access helper
 *
 * Owns the direct device-facing operations needed by the Si5351 backend.
 * This class is intentionally narrow in scope. It should handle:
 *
 * - Opening and closing the I2C device
 * - Basic probe/reset/initialization
 * - Register reads and writes
 * - Output enable and disable control
 * - Optional local register cache support
 *
 * It should not decide transmission policy, symbol timing, or WSPR mode
 * behavior. Those belong in the planner and backend layers.
 */
class Si5351Device
{
public:
    /** Injectable Linux I2C operations for source-level hardware tests. */
    class I2CAdapter
    {
    public:
        virtual ~I2CAdapter() = default;
        virtual int openDevice(const std::string& path, int flags) = 0;
        virtual int selectSlave(int fd, std::uint8_t address) = 0;
        virtual ssize_t writeData(int fd, const void* data, std::size_t size) = 0;
        virtual ssize_t readData(int fd, void* data, std::size_t size) = 0;
        virtual int closeDevice(int fd) = 0;
    };

    /**
     * @brief Supported output clocks
     */
    enum class Output
    {
        CLK0 = 0,
        CLK1 = 1,
        CLK2 = 2
    };

    /**
     * @brief Reference source selection
     *
     * The initial implementation should target an external reference path
     * immediately, but the enum leaves room for future expansion.
     */
    enum class ReferenceSource
    {
        EXTERNAL_TCXO,
        CRYSTAL
    };

    /**
     * @brief Drive strength options for a clock output
     */
    enum class DriveStrength
    {
        MA_2,
        MA_4,
        MA_6,
        MA_8
    };

    /**
     * @brief Device configuration
     */
    struct Config
    {
        int i2c_bus = 1;
        std::uint8_t i2c_address = 0x60;
        std::uint32_t reference_hz = 27000000;
        ReferenceSource reference_source =
            ReferenceSource::EXTERNAL_TCXO;
        int crystal_load_capacitance_pf = 10;
        bool enable_register_cache = true;
        // Maintainer opt-in until broader bus/hardware qualification.
        bool optimize_register_writes = false;
    };

    /**
     * @brief Single register write
     */
    struct RegisterWrite
    {
        std::uint8_t address = 0;
        std::uint8_t value = 0;
    };

    /**
     * @brief Construct a device wrapper
     *
     * @param config Device access and reference configuration
     */
    explicit Si5351Device(
        const Config& config,
        std::shared_ptr<I2CAdapter> adapter = {});

    /**
     * @brief Destroy the device wrapper
     */
    ~Si5351Device();

    Si5351Device(const Si5351Device&) = delete;
    Si5351Device& operator=(const Si5351Device&) = delete;

    Si5351Device(Si5351Device&&) = delete;
    Si5351Device& operator=(Si5351Device&&) = delete;

    /**
     * @brief Open and prepare the I2C device
     *
     * @return True if the device was opened successfully
     */
    bool open();

    /**
     * @brief Close the I2C device if open
     */
    void close();

    /**
     * @brief Check whether the device is currently open
     *
     * @return True if open
     */
    bool isOpen() const noexcept;

    /**
     * @brief Probe the Si5351 by attempting a simple register read
     *
     * @return True if the device appears responsive
     */
    bool probe();

    /**
     * @brief Apply a basic reset/init sequence
     *
     * This should perform only backend-neutral device initialization and
     * should not yet program mode-specific transmit frequencies.
     *
     * @return True on success
     */
    bool initialize();

    /**
     * @brief Disable all outputs
     *
     * @return True on success
     */
    bool disableAllOutputs();

    /**
     * @brief Enable a single output
     *
     * @param output Output to enable
     * @return True on success
     */
    bool enableOutput(Output output);

    /**
     * @brief Disable a single output
     *
     * @param output Output to disable
     * @return True on success
     */
    bool disableOutput(Output output);

    /**
     * @brief Set output drive strength
     *
     * @param output Output to update
     * @param strength Requested drive strength
     * @return True on success
     */
    bool setDriveStrength(Output output, DriveStrength strength);

    /**
     * @brief Write a single register
     *
     * @param address Register address
     * @param value Register value
     * @return True on success
     */
    bool writeRegister(std::uint8_t address, std::uint8_t value);

    /**
     * @brief Read a single register
     *
     * @param address Register address
     * @param value Filled with the register value on success
     * @return True on success
     */
    bool readRegister(std::uint8_t address, std::uint8_t& value);

    /**
     * @brief Write a batch of registers
     *
     * The caller is responsible for supplying the writes in a safe order.
     *
     * @param writes Register writes to apply
     * @return True on success
     */
    bool writeRegisters(const std::vector<RegisterWrite>& writes);

    // Return the next safe transaction boundary; never cross parameter blocks.
    static std::size_t writeGroupEnd(const std::vector<RegisterWrite>& writes,
        std::size_t begin);

    /**
     * @brief Return the device configuration
     *
     * @return Active configuration
     */
    const Config& getConfig() const noexcept;

    /**
     * @brief Clear any local register cache state
     */
    void clearRegisterCache();

    /**
     * @brief Return the last error string
     *
     * @return Human-readable error text
     */
    const std::string& getLastError() const noexcept;

private:
    /**
     * @brief Build the Linux I2C device path for the configured bus
     *
     * @return Device path such as /dev/i2c-1
     */
    std::string buildI2CDevicePath() const;

    /**
     * @brief Update the cached register value if caching is enabled
     *
     * @param address Register address
     * @param value Register value
     */
    void updateRegisterCache(std::uint8_t address, std::uint8_t value);

    /**
     * @brief Record an error message
     *
     * @param message Error text
     */
    void setLastError(const std::string& message);

    Config config_;
    std::shared_ptr<I2CAdapter> adapter_;
    int fd_;
    std::string last_error_;
    std::vector<bool> cache_valid_;
    std::vector<std::uint8_t> cache_values_;
};

#endif
