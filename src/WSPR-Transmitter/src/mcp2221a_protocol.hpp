#pragma once
#include "si5351_transport.hpp"
#include "usb_adapter_identity.hpp"
#include <vector>
#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <sstream>

namespace mcp2221a {
using Report = std::array<std::uint8_t, 64>;
using Deadline = std::chrono::steady_clock::time_point;
struct Endpoint {
    std::string identity;
    std::uint64_t generation = 0;
    std::uint32_t location = 0;
    bool reports64 = true;
    bool operator==(const Endpoint&) const = default;
};
struct Inventory { std::vector<Endpoint> endpoints; std::string error; };
inline std::string identity(const std::string& serial, std::uint32_t location) {
    if (!serial.empty()) return "usb:04d8:00dd:serial:" + usb_adapter_identity::encode(serial);
    if (!location) return {};
    const char* digits = "0123456789abcdef";
    std::string token = "macos-location-";
    for (int shift = 28; shift >= 0; shift -= 4) token += digits[(location >> shift) & 15];
    return "usb:04d8:00dd:path:" + usb_adapter_identity::encode(token);
}
inline bool resolve(const Inventory& inventory, const std::string& selected, Endpoint& result, std::string& error) {
    error = inventory.error;
    if (!error.empty()) return false;
    if (selected.empty() || !usb_adapter_identity::valid(selected) || selected.rfind("usb:04d8:00dd:", 0) != 0) {
        error = "mcp2221a-hid requires an explicit MCP2221A stable identity."; return false;
    }
    unsigned count = 0;
    for (const auto& item : inventory.endpoints) if (item.identity == selected) { result = item; ++count; }
    if (count != 1) { error = count ? "Ambiguous MCP2221A identity." : "Selected MCP2221A is unavailable."; return false; }
    if (!result.reports64) { error = "MCP2221A requires exactly 64-byte HID input/output reports."; return false; }
    if (!result.generation) { error = "MCP2221A registry binding is unavailable."; return false; }
    return true;
}
// Channel owns OS discovery, exact session binding, and deadline-limited HID I/O.
// exchange returns the actual input length, or -1 with a retained diagnostic.
class Channel {
public:
    virtual ~Channel() = default;
    virtual bool open(const std::string&) = 0;
    virtual std::ptrdiff_t exchange(const Report&, Report&, Deadline) = 0;
    virtual bool close() = 0;
    virtual std::string diagnostic() const = 0;
    // The real channel uses the host monotonic clock. Scripted channels can
    // advance polling and exchange latency together without scheduler sleeps.
    virtual Deadline now() const { return std::chrono::steady_clock::now(); }
    virtual void waitPoll() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
};
enum class State { Closed, Idle, Busy, Completed, Nack, Timeout, Partial, Uncertain };

class Transport final : public Si5351Transport {
public:
    Transport(std::string identity, std::uint8_t address, std::shared_ptr<Channel> channel)
        : identity_(std::move(identity)), address_(address), channel_(std::move(channel)) {}
    ~Transport() override { close(); }
    bool open() override {
        std::lock_guard lock(mutex_);
        if (faulted_) return false;
        if (opened_) return state_ == State::Idle || state_ == State::Completed;
        error_.clear();
        if (address_ < 0x08 || address_ > 0x77) return fail(State::Uncertain, "Invalid 7-bit I2C address.");
        if (!channel_->open(identity_)) return fail(State::Uncertain, channel_->diagnostic());
        opened_ = true;
        auto deadline = expires();
        Report request{}, response{}; request[0] = 0x10;
        if (!command(request, response, deadline) || response[8] != 0) {
            if (error_.empty()) fail(State::Uncertain, "Initial MCP2221A engine is not idle; no idle cancellation or reset attempted.");
            closeChannel(); return false;
        }
        request[3] = 0x20; request[4] = 117; // 12 MHz/(117+3) = 100 kHz.
        if (!command(request, response, deadline) || response[3] != 0x20 || response[4] != 117 || response[8] != 0) {
            if (error_.empty()) fail(State::Uncertain, "MCP2221A rejected 100 kHz configuration.");
            closeChannel(); return false;
        }
        state_ = State::Idle;
        error_ = "mcp2221a-hid selected " + identity_;
        return true;
    }
    bool close() override {
        std::lock_guard lock(mutex_);
        return closeChannel(); // Never cancel idle or an unowned operation.
    }
    std::ptrdiff_t write(const std::uint8_t* data, std::size_t size) override {
        std::lock_guard lock(mutex_);
        if (!admit() || !data || size == 0 || size > 9) return invalid();
        error_.clear();
        const auto deadline = expires();
        Report request{}, response{}; request[0] = 0x90; request[1] = static_cast<std::uint8_t>(size);
        request[3] = static_cast<std::uint8_t>(address_ << 1);
        std::copy(data, data + size, request.begin() + 4);
        state_ = State::Busy;
        if (!command(request, response, deadline)) return -1;
        owned_ = true;
        if (!complete(size, deadline)) return -1;
        return static_cast<std::ptrdiff_t>(size);
    }
    std::ptrdiff_t read(std::uint8_t* data, std::size_t size) override {
        std::lock_guard lock(mutex_);
        if (!admit() || !data || size != 1) return invalid();
        error_.clear();
        const auto deadline = expires();
        Report request{}, response{}; request[0] = 0x91; request[1] = 1;
        request[3] = static_cast<std::uint8_t>((address_ << 1) | 1);
        state_ = State::Busy;
        if (!command(request, response, deadline)) return -1;
        owned_ = true;
        request = {}; request[0] = 0x40;
        for (unsigned poll = 0; poll < 100; ++poll) {
            if (!command(request, response, deadline)) return -1;
            if (response[2] == 0x25) { fail(State::Nack, "MCP2221A address NACK."); return -1; }
            if (response[2] == 0x55) {
                if (response[3] != 1) { fail(State::Partial, "MCP2221A read length mismatch."); return -1; }
                const auto value = response[4];
                if (!complete(1, deadline)) return -1;
                *data = value; return 1;
            }
            if (response[3] != 0 || (response[2] != 0 && response[2] != 0x54)) {
                fail(State::Partial, "MCP2221A partial or invalid read response."); return -1;
            }
            channel_->waitPoll();
        }
        fail(State::Timeout, "MCP2221A read deadline expired."); return -1;
    }
    // Transfer cancellation is separate from Si5351 clock/output inhibition.
    // Calling this while idle must not send a cancellation report.
    bool cancelActive() {
        std::lock_guard lock(mutex_);
        return cancelOwned();
    }
    std::string diagnostic() const override { std::lock_guard lock(mutex_); return error_; }
    State state() const { std::lock_guard lock(mutex_); return state_; }
private:
    Deadline expires() const { return channel_->now() + std::chrono::milliseconds(500); }
    bool admit() const { return opened_ && (state_ == State::Idle || state_ == State::Completed); }
    std::ptrdiff_t invalid() {
        if (error_.empty() || admit()) fail(State::Uncertain, "Invalid or unavailable MCP2221A transaction.");
        return -1;
    }
    static std::string hex(std::uint8_t value) {
        std::ostringstream text; text << "0x" << std::hex << static_cast<unsigned>(value); return text.str();
    }
    bool fail(State state, const std::string& message) {
        faulted_ = true; state_ = state; error_ = "mcp2221a-hid " + identity_ + ": " + message + "; output state is unconfirmed."; return false;
    }
    bool command(const Report& request, Report& response, Deadline deadline) {
        if (channel_->now() >= deadline) return fail(State::Timeout, "MCP2221A transaction deadline expired.");
        const auto length = channel_->exchange(request, response, deadline);
        synchronized_ = length == 64 && response[0] == request[0];
        if (!synchronized_ && (request[0] == 0x90 || request[0] == 0x91)) uncertain_transfer_ = true;
        if (length != 64) return fail(length == 0 ? State::Timeout : State::Uncertain,
            "MCP2221A HID exchange failed or returned a short report. " + channel_->diagnostic());
        if (response[0] != request[0]) return fail(State::Uncertain, "MCP2221A response command mismatch.");
        if (response[1] != 0) return fail(response[1] == 1 ? State::Busy : State::Uncertain,
            "MCP2221A command refused; no transaction replay.");
        return true;
    }
    bool complete(std::size_t size, Deadline deadline) {
        Report request{}, response{}; request[0] = 0x10;
        for (unsigned poll = 0; poll < 100; ++poll) {
            if (!command(request, response, deadline)) return false;
            const auto engine = response[8];
            if (engine == 0x25 || (response[20] & 0x40)) return fail(State::Nack, "MCP2221A I2C NACK.");
            if (engine == 0x12 || engine == 0x23 || engine == 0x44 || engine == 0x62)
                return fail(State::Timeout, "MCP2221A I2C engine timeout: " + hex(engine) + ".");
            if (engine == 0) {
                if (static_cast<std::size_t>(response[9] | (response[10] << 8)) != size ||
                    static_cast<std::size_t>(response[11] | (response[12] << 8)) != size)
                    return fail(State::Partial, "MCP2221A transfer count mismatch.");
                owned_ = false; state_ = State::Completed; return true;
            }
            switch (engine) {
                case 0x10: case 0x11: case 0x20: case 0x21: case 0x22:
                case 0x30: case 0x31: case 0x40: case 0x41: case 0x42:
                case 0x43: case 0x45: case 0x50: case 0x51: case 0x52:
                case 0x53: case 0x54: case 0x60: case 0x61: break;
                default: return fail(State::Uncertain, "Unknown MCP2221A engine state.");
            }
            channel_->waitPoll();
        }
        return fail(State::Timeout, "MCP2221A completion deadline expired.");
    }
    bool cancelOwned() {
        if (uncertain_transfer_) return false; // Admission itself may have been lost.
        if (!owned_) return true;
        if (!synchronized_) return false; // No echo-safe exchange after a lost response.
        const auto primary = error_;
        const auto deadline = expires();
        Report request{}, response{}; request[0] = 0x10;
        bool ok = command(request, response, deadline);
        if (ok && response[8] != 0) {
            request[2] = 0x10;
            ok = command(request, response, deadline) && (response[2] == 0x10 || response[2] == 0x11);
            request[2] = 0;
            if (ok) {
                ok = false;
                for (unsigned poll = 0; poll < 50; ++poll) {
                    if (!command(request, response, deadline)) break;
                    if (response[8] == 0) { ok = true; break; }
                    if (response[8] == 0x62) break;
                    channel_->waitPoll();
                }
            }
        }
        if (ok) owned_ = false;
        error_ = primary + (ok ? "; adapter transfer stopped; clock output remains unconfirmed."
            : "; adapter cancellation failed or engine did not become idle (" + hex(response[8]) + "); output state is unconfirmed.");
        // Never restore a failed session to Idle/Completed after cancellation.
        if (!ok) state_ = State::Uncertain;
        return ok;
    }
    bool closeChannel() {
        if (!opened_) return close_result_;
        const bool cancelled = cancelOwned();
        opened_ = false;
        const bool closed = channel_->close();
        const bool result = cancelled && closed;
        close_result_ = result;
        if (!result) { faulted_ = true; error_ += "; " + channel_->diagnostic() + "; cleanup failed; output state is unconfirmed."; state_ = State::Uncertain; }
        else state_ = State::Closed;
        return result;
    }
    std::string identity_, error_;
    std::uint8_t address_;
    std::shared_ptr<Channel> channel_;
    mutable std::mutex mutex_;
    State state_ = State::Closed;
    bool opened_ = false, owned_ = false, synchronized_ = true, faulted_ = false, uncertain_transfer_ = false, close_result_ = true;
};
} // namespace mcp2221a
