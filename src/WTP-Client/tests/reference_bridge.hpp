// SPDX-License-Identifier: MIT
// Separate translation-unit boundary keeps the client and reference server's
// similarly named headers independent. This interface contains no WTP types.
#pragma once
#include <cstdint>
#include <memory>
#include <span>
class ReferenceEndpoint {
  public:
    virtual ~ReferenceEndpoint() = default;
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual void advance(std::uint64_t now_ms) = 0;
    virtual bool closed() const = 0;
    virtual std::size_t receive(std::span<const std::uint8_t> bytes) = 0;
    virtual std::size_t read(std::span<std::uint8_t> bytes) = 0;
    virtual unsigned executions() const = 0;
    virtual bool output_active() const = 0;
};
std::unique_ptr<ReferenceEndpoint> reference_endpoint();
