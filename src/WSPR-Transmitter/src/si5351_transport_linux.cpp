#include "si5351_device.hpp"
#include "linux_i2c_identity.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <unistd.h>
namespace {
std::string system_error_message(const std::string& op, const std::string& target) {
    return op + " failed for " + target + ": " + std::strerror(errno);
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
        std::string diagnostic() const override { return Si5351I2CAdapter::diagnostic() + " " + diagnostic_; }
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
std::shared_ptr<Si5351Transport> make_si5351_transport(const Si5351Device::Config& config) {
    auto adapter = std::make_shared<LinuxI2CAdapter>(config);
    adapter->configure(config.i2c_bus, config.i2c_address);
    return adapter;
}
