// SPDX-License-Identifier: MIT
#pragma once

#include "WSPR-Transmitter/src/bcm2835_qualification_hardware.hpp"
#include "json.hpp"

#include <chrono>
#include <initializer_list>
#include <string_view>

namespace gpio_qualification
{
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using wsprrypi::QualificationBank;
using wsprrypi::QualificationHardware;
using wsprrypi::QualificationObservation;
using wsprrypi::Bcm2835QualificationPlan;

constexpr std::string_view contract = "wsprrypi.gpio-qualification-owner.v1";
constexpr std::string_view profile = "bcm2835";
constexpr int cutoff_ms = 50'000;
constexpr int group_ms = 750'000;
constexpr int run_ms = 6'145'000;
constexpr unsigned max_message = 65'536;

void require(bool condition, const char* reason);
void closed(const Json& value, std::initializer_list<const char*> fields);
std::string token(const Json& value);
int integer(const Json& value, int lower, int upper);
Json parse(std::string_view bytes);
std::string sha256(std::string_view bytes);
std::string nonce();
std::int64_t nowMs();
Json point(unsigned index);
Json admission(const Json& source, const Json& point);
Json bankJson(QualificationBank bank);
Json nativeJson(const QualificationObservation& state, int pin, bool owned);
Json merge(Json left, const Json& right);
Json envelope(std::string_view bytes, const Json& request, const Json& receipt);

class Worker
{
public:
    Worker(QualificationHardware& hardware, QualificationBank original, int pin, int drive,
        std::int64_t group_deadline, int supervisor_socket);
    ~Worker();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    bool ready();
    Json call(const Json& request);
    bool exited() const;
    bool retire();
    bool finishedCleanly() const { return exited() && clean_; }
    unsigned bus() const { return bus_; }
    int pid() const { return pid_; }
    unsigned port() const { return port_; }

private:
    int channel_{-1};
    mutable int pid_{-1};
    mutable bool clean_{};
    unsigned bus_{};
    unsigned port_{};
};
}
