#pragma once

#include <cstdint>
#include <string>

namespace wsprrypi {

// Target-side durable generation. The central Pi compares this with its last
// reconciled generation before dispatching saved work to a target Pi.
class WtpRevocationJournal {
public:
    WtpRevocationJournal(std::string path, std::string device_id);
    bool load();
    bool advance();
    std::uint64_t generation() const noexcept { return generation_; }
    const std::string& error() const noexcept { return error_; }

private:
    std::string path_, device_id_, error_;
    std::uint64_t generation_ = 0;
};
} // namespace wsprrypi
