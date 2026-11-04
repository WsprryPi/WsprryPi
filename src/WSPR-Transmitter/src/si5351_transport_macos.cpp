#include "si5351_device.hpp"
#include "mcp2221a_macos.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <sstream>

namespace mcp2221a {
namespace {
std::uint32_t number(io_service_t service, CFStringRef key) {
    auto value = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    std::uint32_t result = 0;
    if (value) {
        if (CFGetTypeID(value) == CFNumberGetTypeID()) CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt32Type, &result);
        CFRelease(value);
    }
    return result;
}
std::string text(io_service_t service, CFStringRef key, std::string& error) {
    auto value = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    std::string result;
    if (value) {
        if (CFGetTypeID(value) == CFStringGetTypeID()) {
            auto str = static_cast<CFStringRef>(value);
            std::vector<char> bytes(static_cast<std::size_t>(CFStringGetMaximumSizeForEncoding(CFStringGetLength(str), kCFStringEncodingUTF8)) + 1);
            CFIndex used = 0;
            const auto converted = CFStringGetBytes(str, CFRangeMake(0, CFStringGetLength(str)),
                kCFStringEncodingUTF8, 0, false, reinterpret_cast<std::uint8_t*>(bytes.data()), bytes.size(), &used);
            if (converted != CFStringGetLength(str)) error = "Unable to decode MCP2221A serial metadata.";
            else result.assign(bytes.data(), static_cast<std::size_t>(used));
        } else error = "Invalid MCP2221A serial metadata type.";
        CFRelease(value);
    }
    return result;
}
Endpoint endpoint(io_service_t service, std::string& error) {
    Endpoint item;
    if (number(service, CFSTR(kIOHIDVendorIDKey)) != 0x04d8 || number(service, CFSTR(kIOHIDProductIDKey)) != 0x00dd) return item;
    if (IORegistryEntryGetRegistryEntryID(service, &item.generation) != KERN_SUCCESS) return {};
    item.location = number(service, CFSTR(kIOHIDLocationIDKey));
    const auto serial = text(service, CFSTR(kIOHIDSerialNumberKey), error);
    if (!error.empty()) return {};
    item.identity = identity(serial, item.location);
    item.reports64 = number(service, CFSTR(kIOHIDMaxInputReportSizeKey)) == 64 &&
        number(service, CFSTR(kIOHIDMaxOutputReportSizeKey)) == 64;
    if (item.identity.empty()) error = "MCP2221A stable serial/location metadata is unavailable.";
    return item;
}
class NativeChannel final : public Channel {
public:
    ~NativeChannel() override { close(); }
    bool open(const std::string& selected) override {
        if (close_failed_) { error_ = "Previous HID close failed; construct a new session after operator recovery."; return false; }
        error_.clear(); removed_ = false;
        if (device_) { error_ = "HID session already open."; return false; }
        if (!resolve(discover(), selected, binding_, error_)) return false;
        auto service = IOServiceGetMatchingService(kIOMainPortDefault, IORegistryEntryIDMatching(binding_.generation));
        if (!service) { error_ = "MCP2221A detached before open."; return false; }
        device_ = IOHIDDeviceCreate(kCFAllocatorDefault, service);
        const auto actual = endpoint(service, error_);
        IOObjectRelease(service);
        if (!device_ || !(actual == binding_)) { error_ = "MCP2221A identity changed before open."; release(); return false; }
        const auto result = IOHIDDeviceOpen(device_, kIOHIDOptionsTypeSeizeDevice);
        if (result != kIOReturnSuccess) { error_ = "MCP2221A exclusive access denied or unavailable: " + std::to_string(result); release(); return false; }
        opened_ = true;
        IOHIDDeviceRegisterInputReportCallback(device_, input_.data(), input_.size(), input_callback, this);
        IOHIDDeviceRegisterRemovalCallback(device_, removed_callback, this);
        if (!validate()) { close(); return false; }
        return true;
    }
    std::ptrdiff_t exchange(const Report& request, Report& response, Deadline deadline) override {
        if (!validate()) return -1;
        // Keep every buffer and callback flag session-owned, including error paths.
        request_ = request; response_ = {}; input_count_ = 0; output_done_ = false;
        input_length_ = -1; callback_error_ = false;
        loop_ = CFRunLoopGetCurrent(); CFRetain(loop_);
        IOHIDDeviceScheduleWithRunLoop(device_, loop_, mode());
        // Reject stale queued input before sending another command.
        CFRunLoopRunInMode(mode(), 0.001, false);
        if (input_count_ || removed_) { error_ = "Unexpected queued HID response or removal."; unschedule(); return -1; }
        const auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (timeout_ms <= 0) { error_ = "HID submission deadline expired."; unschedule(); return 0; }
        const auto result = IOHIDDeviceSetReportWithCallback(device_, kIOHIDReportTypeOutput, 0,
            request_.data(), request_.size(), static_cast<CFTimeInterval>(timeout_ms), output_callback, this);
        if (result != kIOReturnSuccess) { error_ = "HID report submission failed: " + std::to_string(result); unschedule(); return -1; }
        while (!removed_ && !callback_error_ && !(output_done_ && input_count_) && std::chrono::steady_clock::now() < deadline)
            CFRunLoopRunInMode(mode(), 0.002, true);
        const bool success = !removed_ && !callback_error_ && output_done_ && input_count_ == 1 && input_length_ == 64;
        if (success) response = response_;
        else error_ = "HID deadline, disconnect, duplicate, or malformed response; session is unusable.";
        // A failed submission/exchange is terminal. Close while callback context
        // and request/input buffers are still alive; never reuse pending I/O.
        unschedule();
        if (!success) { const bool timeout = std::chrono::steady_clock::now() >= deadline; close(); return timeout ? 0 : -1; }
        return 64;
    }
    bool close() override {
        unschedule();
        bool result = !close_failed_;
        if (device_ && opened_) {
            result = IOHIDDeviceClose(device_, kIOHIDOptionsTypeNone) == kIOReturnSuccess;
            if (!result) { close_failed_ = true; error_ += "; IOHIDDeviceClose failed; output state is unconfirmed."; }
        }
        opened_ = false; release(); return result;
    }
    std::string diagnostic() const override { return error_; }
private:
    static CFStringRef mode() { return CFSTR("WsprryPiMcp2221aExchange"); }
    static void input_callback(void* context, IOReturn result, void*, IOHIDReportType type, std::uint32_t report_id, std::uint8_t* report, CFIndex length) {
        auto& self = *static_cast<NativeChannel*>(context);
        ++self.input_count_;
        if (result != kIOReturnSuccess || type != kIOHIDReportTypeInput || report_id != 0 || length != 64 || self.input_count_ != 1) {
            self.callback_error_ = true; return;
        }
        self.input_length_ = length;
        std::copy(report, report + 64, self.response_.begin());
    }
    static void output_callback(void* context, IOReturn result, void*, IOHIDReportType, std::uint32_t, std::uint8_t*, CFIndex) {
        auto& self = *static_cast<NativeChannel*>(context);
        self.output_done_ = true;
        if (result != kIOReturnSuccess) self.callback_error_ = true;
    }
    static void removed_callback(void* context, IOReturn, void*) { static_cast<NativeChannel*>(context)->removed_ = true; }
    bool validate() {
        Endpoint current;
        if (!device_ || !opened_ || removed_ || !resolve(discover(), binding_.identity, current, error_) || !(current == binding_)) {
            error_ = "MCP2221A session binding changed or device is unavailable."; return false;
        }
        return true;
    }
    void unschedule() {
        if (loop_) { if (device_) IOHIDDeviceUnscheduleFromRunLoop(device_, loop_, mode()); CFRelease(loop_); loop_ = nullptr; }
    }
    void release() { if (device_) { CFRelease(device_); device_ = nullptr; } }
    IOHIDDeviceRef device_ = nullptr;
    CFRunLoopRef loop_ = nullptr;
    Endpoint binding_;
    Report request_{}, input_{}, response_{};
    std::string error_;
    CFIndex input_length_ = -1;
    unsigned input_count_ = 0;
    bool opened_ = false, removed_ = false, output_done_ = false, callback_error_ = false, close_failed_ = false;
};
}
Inventory discover() {
    Inventory result;
    io_iterator_t iterator = 0;
    auto matching = IOServiceMatching(kIOHIDDeviceKey);
    if (!matching || IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) != KERN_SUCCESS) {
        result.error = "Unable to read HID registry metadata."; return result;
    }
    while (auto service = IOIteratorNext(iterator)) {
        auto item = endpoint(service, result.error);
        IOObjectRelease(service);
        if (!item.identity.empty()) result.endpoints.push_back(std::move(item));
    }
    IOObjectRelease(iterator);
    if (!result.error.empty()) result.endpoints.clear();
    return result;
}
std::shared_ptr<Channel> native_channel() { return std::make_shared<NativeChannel>(); }
}
std::shared_ptr<Si5351Transport> make_si5351_transport(const Si5351Device::Config& config) {
    return std::make_shared<mcp2221a::Transport>(config.adapter_identity, config.i2c_address, mcp2221a::native_channel());
}
