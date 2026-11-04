#pragma once
#include "si5351_transport.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <utility>

// Explicit compatibility adapter for existing Linux qualification programs and
// injected fixtures. Native transports implement Si5351Transport directly.
class Si5351I2CAdapter : public Si5351Transport {
public:
    virtual int openDevice(const std::string&, int) = 0;
    virtual int queryCapabilities(int, unsigned long&) = 0;
    virtual int selectSlave(int, std::uint8_t) = 0;
    virtual ssize_t writeData(int, const void*, std::size_t) = 0;
    virtual ssize_t readData(int, void*, std::size_t) = 0;
    virtual int closeDevice(int) = 0;
    std::string diagnostic() const override { return error_; }
    void configure(int bus, std::uint8_t address) override { bus_ = bus; address_ = address; }
    bool open() final {
        error_.clear();
        fd_ = openDevice("/dev/i2c-" + std::to_string(bus_), O_RDWR | O_CLOEXEC);
        if (fd_ < 0) { error_ = std::string("Open I2C device failed: ") + std::strerror(errno); return false; }
        unsigned long capabilities = 0;
        if (queryCapabilities(fd_, capabilities) < 0 || !(capabilities & 1UL)) {
            error_ = "Query I2C capabilities failed or required I2C_FUNC_I2C capability is absent.";
            if (!close()) error_ += "; cleanup failed; output state is unconfirmed.";
            return false;
        }
        if (selectSlave(fd_, address_) < 0) {
            error_ = "Select I2C slave failed.";
            if (!close()) error_ += "; cleanup failed; output state is unconfirmed.";
            return false;
        }
        return true;
    }
    bool close() final {
        const int fd = std::exchange(fd_, -1);
        if (fd < 0 || closeDevice(fd) == 0) return true;
        error_ += std::string("; Close I2C device failed: ") + std::strerror(errno) + "; output state is unconfirmed.";
        return false;
    }
    std::ptrdiff_t write(const std::uint8_t* data, std::size_t size) final { return writeData(fd_, data, size); }
    std::ptrdiff_t read(std::uint8_t* data, std::size_t size) final { return readData(fd_, data, size); }
private:
    int bus_ = 1, fd_ = -1;
    std::uint8_t address_ = 0x60;
    std::string error_;
};
