/**
 * @file si5351_device.cpp
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

#include "si5351_device.hpp"

#include <cerrno>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
    std::mutex g_i2c_transaction_mutex;
    static constexpr std::uint8_t kOutputEnableRegister = 3;
    static constexpr std::uint8_t kClkControlBaseRegister = 16;
    static constexpr std::uint8_t kOutputDisableAll = 0xff;
    static constexpr std::uint8_t kDriveStrengthMask = 0x03;
    static constexpr std::uint8_t kCrystalLoadRegister = 183;

    static bool crystal_load_register_value(int capacitance_pf, std::uint8_t& value)
    {
        switch (capacitance_pf)
        {
            case 6: value = 0x52; return true;
            case 8: value = 0x92; return true;
            case 10: value = 0xD2; return true;
        }
        return false;
    }

    static bool output_index(
        Si5351Device::Output output,
        std::uint8_t& index)
    {
        switch (output)
        {
            case Si5351Device::Output::CLK0:
                index = 0;
                return true;
            case Si5351Device::Output::CLK1:
                index = 1;
                return true;
            case Si5351Device::Output::CLK2:
                index = 2;
                return true;
        }

        return false;
    }

    static bool drive_strength_bits(
        Si5351Device::DriveStrength strength,
        std::uint8_t& bits)
    {
        switch (strength)
        {
            case Si5351Device::DriveStrength::MA_2:
                bits = 0x00;
                return true;
            case Si5351Device::DriveStrength::MA_4:
                bits = 0x01;
                return true;
            case Si5351Device::DriveStrength::MA_6:
                bits = 0x02;
                return true;
            case Si5351Device::DriveStrength::MA_8:
                bits = 0x03;
                return true;
        }

        return false;
    }

    static std::string system_error_message(
        const std::string& operation,
        const std::string& target)
    {
        std::ostringstream stream;
        stream << operation << " failed for " << target << ": "
               << std::strerror(errno);
        return stream.str();
    }

    class LinuxI2CAdapter final : public Si5351Device::I2CAdapter
    {
    public:
        int openDevice(const std::string& path, int flags) override
        {
            return ::open(path.c_str(), flags);
        }

        int selectSlave(int fd, std::uint8_t address) override
        {
            return ::ioctl(fd, I2C_SLAVE, address);
        }

        ssize_t writeData(int fd, const void* data, std::size_t size) override
        {
            return ::write(fd, data, size);
        }

        ssize_t readData(int fd, void* data, std::size_t size) override
        {
            return ::read(fd, data, size);
        }

        int closeDevice(int fd) override
        {
            return ::close(fd);
        }
    };
}

/**
 * @brief Construct a device wrapper
 *
 * @param config Device access and reference configuration
 */
Si5351Device::Si5351Device(
    const Config& config,
    std::shared_ptr<I2CAdapter> adapter)
    : config_(config),
      adapter_(adapter ? std::move(adapter) : std::make_shared<LinuxI2CAdapter>()),
      fd_(-1),
      last_error_(),
      cache_valid_(256, false),
      cache_values_(256, 0)
{
}

/**
 * @brief Destroy the device wrapper
 */
Si5351Device::~Si5351Device()
{
    close();
}

bool Si5351Device::open()
{
    if (isOpen())
    {
        last_error_.clear();
        return true;
    }

    clearRegisterCache();

    const std::string path = buildI2CDevicePath();
    fd_ = adapter_->openDevice(path, O_RDWR | O_CLOEXEC);
    if (fd_ < 0)
    {
        setLastError(system_error_message("Open", path));
        return false;
    }

    if (adapter_->selectSlave(fd_, config_.i2c_address) < 0)
    {
        setLastError(system_error_message("Select I2C slave", path));
        (void)adapter_->closeDevice(fd_);
        fd_ = -1;
        clearRegisterCache();
        return false;
    }

    last_error_.clear();
    return true;
}

void Si5351Device::close()
{
    if (fd_ >= 0)
    {
        (void)adapter_->closeDevice(fd_);
        fd_ = -1;
    }

    clearRegisterCache();
}

bool Si5351Device::isOpen() const noexcept
{
    return fd_ >= 0;
}

bool Si5351Device::probe()
{
    std::uint8_t value = 0;
    return readRegister(kOutputEnableRegister, value);
}

bool Si5351Device::initialize()
{
    if (!isOpen())
    {
        setLastError("Cannot initialize Si5351 because I2C device is closed.");
        return false;
    }

    clearRegisterCache();

    std::uint8_t crystal_register_value = 0;
    if (config_.reference_source == ReferenceSource::CRYSTAL &&
        !crystal_load_register_value(
            config_.crystal_load_capacitance_pf, crystal_register_value))
    {
        setLastError("Si5351 crystal load capacitance must be 6, 8, or 10 pF.");
        return false;
    }

    if (!disableAllOutputs())
        return false;

    if (config_.reference_source == ReferenceSource::CRYSTAL &&
        !writeRegister(kCrystalLoadRegister, crystal_register_value))
        return false;

    last_error_.clear();
    return true;
}

bool Si5351Device::disableAllOutputs()
{
    return writeRegister(kOutputEnableRegister, kOutputDisableAll);
}

bool Si5351Device::enableOutput(Output output)
{
    std::uint8_t index = 0;
    if (!output_index(output, index))
    {
        setLastError("Cannot enable unknown Si5351 output.");
        return false;
    }

    std::uint8_t value = 0;
    if (!readRegister(kOutputEnableRegister, value))
        return false;

    value &= static_cast<std::uint8_t>(~(1u << index));
    return writeRegister(kOutputEnableRegister, value);
}

bool Si5351Device::disableOutput(Output output)
{
    std::uint8_t index = 0;
    if (!output_index(output, index))
    {
        setLastError("Cannot disable unknown Si5351 output.");
        return false;
    }

    std::uint8_t value = 0;
    if (!readRegister(kOutputEnableRegister, value))
        return false;

    value |= static_cast<std::uint8_t>(1u << index);
    return writeRegister(kOutputEnableRegister, value);
}

bool Si5351Device::setDriveStrength(
    Output output,
    DriveStrength strength)
{
    std::uint8_t index = 0;
    if (!output_index(output, index))
    {
        setLastError("Cannot set drive strength for unknown Si5351 output.");
        return false;
    }

    std::uint8_t bits = 0;
    if (!drive_strength_bits(strength, bits))
    {
        setLastError("Cannot set unknown Si5351 drive strength.");
        return false;
    }

    const std::uint8_t address =
        static_cast<std::uint8_t>(kClkControlBaseRegister + index);

    std::uint8_t value = 0;
    if (!readRegister(address, value))
        return false;

    value = static_cast<std::uint8_t>(
        (value & static_cast<std::uint8_t>(~kDriveStrengthMask)) | bits);
    return writeRegister(address, value);
}

bool Si5351Device::writeRegister(
    std::uint8_t address,
    std::uint8_t value)
{
    std::lock_guard<std::mutex> transaction_lock(g_i2c_transaction_mutex);
    if (!isOpen())
    {
        setLastError("Cannot write Si5351 register because I2C device is "
                     "closed.");
        return false;
    }

    // Only stable clock controls may be elided. Reset and output-enable writes
    // are commands/safety operations and always reach the bus.
    if (config_.optimize_register_writes && config_.enable_register_cache &&
        address >= 16 && address <= 18 && cache_valid_[address] &&
        cache_values_[address] == value)
    {
        last_error_.clear();
        return true;
    }
    const std::uint8_t buffer[2] = {address, value};
    const ssize_t written = adapter_->writeData(fd_, buffer, sizeof(buffer));
    if (written != static_cast<ssize_t>(sizeof(buffer)))
    {
        std::ostringstream stream;
        stream << "Write Si5351 register 0x" << std::hex
               << static_cast<unsigned>(address) << " failed";
        if (written < 0)
            stream << ": " << std::strerror(errno);
        else
            stream << ": Short write.";
        setLastError(stream.str());
        return false;
    }

    updateRegisterCache(address, value);
    last_error_.clear();
    return true;
}

bool Si5351Device::readRegister(
    std::uint8_t address,
    std::uint8_t& value)
{
    std::lock_guard<std::mutex> transaction_lock(g_i2c_transaction_mutex);
    if (!isOpen())
    {
        setLastError("Cannot read Si5351 register because I2C device is "
                     "closed.");
        return false;
    }

    const ssize_t address_written =
        adapter_->writeData(fd_, &address, sizeof(address));
    if (address_written != static_cast<ssize_t>(sizeof(address)))
    {
        std::ostringstream stream;
        stream << "Select Si5351 register 0x" << std::hex
               << static_cast<unsigned>(address) << " failed";
        if (address_written < 0)
            stream << ": " << std::strerror(errno);
        else
            stream << ": Short write.";
        setLastError(stream.str());
        return false;
    }

    const ssize_t bytes_read = adapter_->readData(fd_, &value, sizeof(value));
    if (bytes_read != static_cast<ssize_t>(sizeof(value)))
    {
        std::ostringstream stream;
        stream << "Read Si5351 register 0x" << std::hex
               << static_cast<unsigned>(address) << " failed";
        if (bytes_read < 0)
            stream << ": " << std::strerror(errno);
        else
            stream << ": Short read.";
        setLastError(stream.str());
        return false;
    }

    updateRegisterCache(address, value);
    last_error_.clear();
    return true;
}

std::size_t Si5351Device::writeGroupEnd(
    const std::vector<RegisterWrite>& writes, std::size_t begin)
{
    if (begin >= writes.size()) return writes.size();
    const auto address = writes[begin].address;
    // PLLA/B and MultiSynth0/1/2 each have eight contiguous parameter bytes.
    if (address < 26 || address > 65) return begin + 1;
    const unsigned block_end = 26 + ((address - 26) / 8 + 1) * 8;
    std::size_t end = begin + 1;
    while (end < writes.size() && writes[end].address == address + end - begin &&
        writes[end].address < block_end) ++end;
    return end;
}

bool Si5351Device::writeRegisters(const std::vector<RegisterWrite>& writes)
{
    for (std::size_t begin = 0; begin < writes.size();)
    {
        const std::size_t end = config_.optimize_register_writes
            ? writeGroupEnd(writes, begin) : begin + 1;
        if (end == begin + 1)
        {
            if (!writeRegister(writes[begin].address, writes[begin].value)) return false;
        }
        else
        {
            std::lock_guard<std::mutex> transaction_lock(g_i2c_transaction_mutex);
            if (!isOpen()) { setLastError("Cannot write Si5351 block: device closed."); return false; }
            std::uint8_t buffer[9] = {writes[begin].address};
            for (std::size_t i = begin; i < end; ++i) buffer[i - begin + 1] = writes[i].value;
            const std::size_t size = end - begin + 1;
            const ssize_t written = adapter_->writeData(fd_, buffer, size);
            if (written != static_cast<ssize_t>(size))
            {
                // Some bytes may have reached silicon. Never retry or trust cache.
                setLastError("Si5351 parameter block write failed or was short.");
                return false;
            }
            for (std::size_t i = begin; i < end; ++i)
                updateRegisterCache(writes[i].address, writes[i].value);
        }
        begin = end;
    }
    last_error_.clear();
    return true;
}

const Si5351Device::Config& Si5351Device::getConfig() const noexcept
{
    return config_;
}

void Si5351Device::clearRegisterCache()
{
    for (std::size_t i = 0; i < cache_valid_.size(); ++i)
    {
        cache_valid_[i] = false;
        cache_values_[i] = 0;
    }
}

const std::string& Si5351Device::getLastError() const noexcept
{
    return last_error_;
}

std::string Si5351Device::buildI2CDevicePath() const
{
    return "/dev/i2c-" + std::to_string(config_.i2c_bus);
}

void Si5351Device::updateRegisterCache(
    std::uint8_t address,
    std::uint8_t value)
{
    if (!config_.enable_register_cache)
        return;

    cache_valid_[address] = true;
    cache_values_[address] = value;
}

void Si5351Device::setLastError(const std::string& message)
{
    clearRegisterCache();
    last_error_ = message;
}
