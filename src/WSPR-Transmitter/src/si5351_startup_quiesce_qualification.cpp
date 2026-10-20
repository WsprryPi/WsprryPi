#include "si5351_startup_quiesce_qualification.hpp"

#include "wspr_transmit.hpp"
#include "wspr_transmit_backend_si5351.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <iomanip>
#include <linux/i2c-dev.h>
#include <sstream>
#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
constexpr const char* kAcknowledgement =
    "--i-understand-this-accesses-live-si5351-hardware";

class InertBridge final : public IControllerBridge
{
public:
    WsprTransmitState backendStateValue() const noexcept override { return WsprTransmitState::DISABLED; }
    void backendSetStateValue(WsprTransmitState) noexcept override {}
    bool backendShouldStop() const noexcept override { return false; }
    void backendSignalStopRequest() noexcept override {}
    void backendRequestStopTxNoJoin() noexcept override {}
    bool backendWaitInterruptableFor(std::chrono::nanoseconds) override { return true; }
    void backendThrowIfStopRequested(const char*) override {}
    void backendReportExecutionProgress(std::size_t) noexcept override {}
    void backendFireTransmitCallback(WsprTransmissionCallbackEvent, WsprTransmitLogLevel,
                                     const std::string&, double) override {}
    bool backendRestartCurrentConfiguration() override { return false; }
};

class LinuxAdapter final : public Si5351Device::I2CAdapter
{
public:
    int openDevice(const std::string& path, int flags) override { return ::open(path.c_str(), flags); }
    int selectSlave(int fd, std::uint8_t address) override { return ::ioctl(fd, I2C_SLAVE, address); }
    ssize_t writeData(int fd, const void* data, std::size_t size) override { return ::write(fd, data, size); }
    ssize_t readData(int fd, void* data, std::size_t size) override { return ::read(fd, data, size); }
    int closeDevice(int fd) override { return ::close(fd); }
};

class AuditingAdapter final : public Si5351Device::I2CAdapter
{
public:
    AuditingAdapter(std::shared_ptr<Si5351Device::I2CAdapter> delegate,
                    std::string path, std::uint8_t address,
                    std::vector<std::string>& trace)
        : delegate_(std::move(delegate)), path_(std::move(path)), address_(address), trace_(trace) {}

    int openDevice(const std::string& path, int flags) override
    {
        if (path != path_ || flags != (O_RDWR | O_CLOEXEC)) return reject("rejected open");
        const int fd = delegate_->openDevice(path, flags);
        if (fd >= 0) { open_fd_ = fd; trace_.push_back("open " + path); }
        return fd;
    }
    int selectSlave(int fd, std::uint8_t address) override
    {
        if (fd != open_fd_ || address != address_) return reject("rejected slave selection");
        trace_.push_back("select 0x" + hex(address));
        return delegate_->selectSlave(fd, address);
    }
    ssize_t writeData(int fd, const void* data, std::size_t size) override
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        if (fd != open_fd_ || (size != 1 && size != 2) || bytes == nullptr || bytes[0] != 0x03 ||
            (size == 2 && bytes[1] != 0xff)) return reject("rejected register write");
        if (size == 2) ++quiesce_writes_;
        trace_.push_back(size == 2 ? "write 03 ff" : "select-register 03");
        return delegate_->writeData(fd, data, size);
    }
    ssize_t readData(int fd, void* data, std::size_t size) override
    {
        if (fd != open_fd_ || size != 1) return reject("rejected register read");
        trace_.push_back("read 03");
        return delegate_->readData(fd, data, size);
    }
    int closeDevice(int fd) override
    {
        if (fd != open_fd_) return reject("rejected close");
        trace_.push_back("close"); open_fd_ = -1;
        return delegate_->closeDevice(fd);
    }
    int quiesce_writes() const noexcept { return quiesce_writes_; }
private:
    int reject(const char* reason) { trace_.push_back(reason); errno = EPERM; return -1; }
    static std::string hex(std::uint8_t value) { std::ostringstream s; s << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(value); return s.str(); }
    std::shared_ptr<Si5351Device::I2CAdapter> delegate_; std::string path_; std::uint8_t address_; std::vector<std::string>& trace_; int open_fd_{-1}; int quiesce_writes_{0};
};

bool read_register_three(const si5351_startup_quiesce_qualification::Options& options,
                         const std::shared_ptr<Si5351Device::I2CAdapter>& adapter,
                         std::uint8_t& value, std::string& error)
{
    Si5351Device::Config config; config.i2c_bus = options.bus; config.i2c_address = options.address;
    Si5351Device device(config, adapter);
    if (!device.open() || !device.readRegister(3, value)) { error = device.getLastError(); device.close(); return false; }
    device.close(); return true;
}
}

namespace si5351_startup_quiesce_qualification
{
bool parse_options(int argc, char** argv, Options& options, std::string& error)
{
    if (argc != 8 || std::string(argv[1]) != "--device" || std::string(argv[3]) != "--address" ||
        std::string(argv[5]) != "--count" || std::string(argv[7]) != kAcknowledgement) { error = "Refusing hardware access: require exactly --device /dev/i2c-N --address 0x60 --count 2 " + std::string(kAcknowledgement) + "."; return false; }
    const std::string prefix = "/dev/i2c-"; options.device_path = argv[2];
    if (options.device_path.rfind(prefix, 0) != 0 || options.device_path.size() == prefix.size()) { error = "Device must be exactly /dev/i2c-N."; return false; }
    try { std::size_t used = 0; options.bus = std::stoi(options.device_path.substr(prefix.size()), &used); if (used != options.device_path.size() - prefix.size() || options.bus < 0) throw std::invalid_argument("bus"); unsigned long address = std::stoul(argv[4], &used, 0); if (used != std::string(argv[4]).size() || address > 0x7f) throw std::invalid_argument("address"); options.address = static_cast<std::uint8_t>(address); if (std::string(argv[6]) != "2") throw std::invalid_argument("count"); }
    catch (const std::exception&) { error = "Invalid device, address, or count; count must be exactly 2."; return false; }
    return true;
}

Result run(const Options& options, std::shared_ptr<Si5351Device::I2CAdapter> system_adapter)
{
    Result result; auto audited = std::make_shared<AuditingAdapter>(std::move(system_adapter), options.device_path, options.address, result.trace);
    if (!read_register_three(options, audited, result.before, result.error)) return result;
    InertBridge bridge; WsprSi5351Backend::Config config; config.device.i2c_bus = options.bus; config.device.i2c_address = options.address; config.device_adapter = audited; config.dry_run = false;
    WsprSi5351Backend backend(bridge, config);
    const auto first = backend.quiesceForStartup(); result.first_quiesce_ok = first.ok; result.first_quiesce_error = first.error; if (!first.ok) { result.error = first.error; return result; }
    if (!read_register_three(options, audited, result.after_first, result.error) || result.after_first != 0xff) { if (result.error.empty()) result.error = "Register 3 was not 0xFF after first quiesce."; return result; }
    const auto second = backend.quiesceForStartup(); result.second_quiesce_ok = second.ok; result.second_quiesce_error = second.error; if (!second.ok) { result.error = second.error; return result; }
    if (!read_register_three(options, audited, result.after_second, result.error) || result.after_second != 0xff) { if (result.error.empty()) result.error = "Register 3 was not 0xFF after second quiesce."; return result; }
    if (audited->quiesce_writes() != 2) { result.error = "Qualification did not perform exactly two register 3 = 0xFF writes."; return result; }
    result.ok = true; return result;
}

std::shared_ptr<Si5351Device::I2CAdapter> make_system_adapter() { return std::make_shared<LinuxAdapter>(); }
} // namespace si5351_startup_quiesce_qualification
