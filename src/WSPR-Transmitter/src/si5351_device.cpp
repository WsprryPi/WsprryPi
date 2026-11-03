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
#include "linux_i2c_identity.hpp"

#include <cerrno>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
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
        explicit LinuxI2CAdapter(const Si5351Device::Config& config) : config_(config) {}
        int openDevice(const std::string& path, int flags) override
        {
            diagnostic_.clear();
            auto selected_path = path;
            if (!config_.adapter_identity.empty()) {
                const auto resolved = linux_i2c_identity::resolve(
                    linux_i2c_identity::discover(), config_.adapter_identity, config_.i2c_bus);
                if (!resolved.error.empty()) { diagnostic_ = resolved.error; errno = ENODEV; return -1; }
                binding_ = resolved.adapter;
                selected_path = binding_.node.string();
            }
            const int fd = ::open(selected_path.c_str(), flags);
            if (fd < 0) { diagnostic_ = system_error_message("Open", selected_path); return -1; }
            if (config_.adapter_identity.empty()) {
                const auto resolved = linux_i2c_identity::resolve(
                    linux_i2c_identity::discover(), {}, config_.i2c_bus);
                if (!resolved.error.empty()) { diagnostic_ = resolved.error; errno = ENODEV; return rejectOpenedDevice(fd); }
                binding_ = resolved.adapter;
            }
            if (!validate(fd)) return rejectOpenedDevice(fd);
            diagnostic_ = "linux-i2c adapter " + (config_.adapter_identity.empty()
                ? std::string("explicit bus ") + std::to_string(config_.i2c_bus) : config_.adapter_identity) +
                " resolved to " + selected_path;
            return fd;
        }
        int queryCapabilities(int fd, unsigned long& capabilities) override
        { return validate(fd) ? ::ioctl(fd, I2C_FUNCS, &capabilities) : -1; }
        int selectSlave(int fd, std::uint8_t address) override
        { return validate(fd) ? ::ioctl(fd, I2C_SLAVE, address) : -1; }
        ssize_t writeData(int fd, const void* data, std::size_t size) override
        { return validate(fd) ? ::write(fd, data, size) : -1; }
        ssize_t readData(int fd, void* data, std::size_t size) override
        { return validate(fd) ? ::read(fd, data, size) : -1; }
        int closeDevice(int fd) override { return ::close(fd); }
        std::string diagnostic() const override { return diagnostic_; }
    private:
        int rejectOpenedDevice(int fd) {
            const int primary_error = errno;
            if (::close(fd) != 0)
                diagnostic_ += "; " + system_error_message("Close rejected I2C device", binding_.node.string()) +
                    "; cleanup failed; output state is unconfirmed.";
            errno = primary_error;
            return -1;
        }
        bool validate(int fd) {
            const auto current = linux_i2c_identity::resolve(linux_i2c_identity::discover(),
                config_.adapter_identity, config_.adapter_identity.empty() ? config_.i2c_bus : binding_.bus);
            struct stat status{};
            if (!current.error.empty() || !(current.adapter == binding_) ||
                ::fstat(fd, &status) != 0 || !S_ISCHR(status.st_mode) ||
                major(status.st_rdev) != 89 || minor(status.st_rdev) != static_cast<unsigned>(binding_.bus)) {
                diagnostic_ = "linux-i2c adapter identity changed or device was removed; output state is unconfirmed.";
                errno = ENODEV;
                return false;
            }
            return true;
        }
        Si5351Device::Config config_;
        linux_i2c_identity::Adapter binding_;
        std::string diagnostic_;
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
      adapter_(adapter ? std::move(adapter) : std::make_shared<LinuxI2CAdapter>(config)),
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
    const std::string target = config_.adapter_identity.empty() ? path : config_.adapter_identity;
    fd_ = adapter_->openDevice(path, O_RDWR | O_CLOEXEC);
    if (fd_ < 0)
    {
        setLastError(system_error_message("Open", target));
        return false;
    }

    unsigned long capabilities = 0;
    const int capability_result = adapter_->queryCapabilities(fd_, capabilities);
    if (capability_result < 0 || !(capabilities & I2C_FUNC_I2C))
    {
        setLastError(capability_result < 0
            ? system_error_message("Query I2C capabilities", target)
            : "linux-i2c adapter lacks required I2C_FUNC_I2C capability.");
        close();
        return false;
    }

    if (adapter_->selectSlave(fd_, config_.i2c_address) < 0)
    {
        setLastError(system_error_message("Select I2C slave", target));
        close();
        return false;
    }

    last_error_.clear();
    return true;
}

bool Si5351Device::close()
{
    const int fd = std::exchange(fd_, -1);
    clearRegisterCache();
    if (fd >= 0 && adapter_->closeDevice(fd) != 0) {
        last_error_ += (last_error_.empty() ? "" : "; ") +
            system_error_message("Close I2C device", config_.adapter_identity.empty() ? buildI2CDevicePath() : config_.adapter_identity) +
            "; cleanup failed; output state is unconfirmed.";
        return false;
    }
    return true;
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
    const auto detail = adapter_->diagnostic();
    if (!detail.empty()) last_error_ += " " + detail;
}
