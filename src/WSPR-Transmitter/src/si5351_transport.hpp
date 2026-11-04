#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Transaction-oriented physical transport. Handles, device paths, and OS APIs
// are private to implementations. Exact byte counts are mandatory to callers.
class Si5351Transport {
public:
    virtual ~Si5351Transport() = default;
    virtual void configure(int, std::uint8_t) {} // Compatibility adapters only.
    virtual bool open() = 0;
    virtual bool close() = 0;
    virtual std::ptrdiff_t write(const std::uint8_t*, std::size_t) = 0;
    virtual std::ptrdiff_t read(std::uint8_t*, std::size_t) = 0;
    virtual std::string diagnostic() const = 0;
};
