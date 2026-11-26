// SPDX-License-Identifier: MIT
#pragma once

#include "bcm2835_qualification_plan.hpp"

#include <functional>
#include <memory>

namespace wsprrypi
{
struct QualificationBank
{
    unsigned bits{};
    bool operator==(const QualificationBank&) const = default;
    QualificationBank withDrive(int code) const { return {(bits & 0x18u) | unsigned(code)}; }
};

struct QualificationObservation
{
    QualificationBank bank;
    unsigned gpio4{};
    unsigned gpio20{};
    bool clock_enabled{};
    bool clock_busy{};
    bool dma_idle{};
    bool pwm_idle{};

    bool idle() const
    {
        return !gpio4 && !gpio20 && !clock_enabled && !clock_busy && dma_idle && pwm_idle;
    }
};

class QualificationHardware
{
public:
    virtual ~QualificationHardware() = default;
    virtual QualificationObservation observe() = 0;
    virtual unsigned prepare(QualificationBank original) = 0;
    virtual void enable(const Bcm2835QualificationPlan& plan, QualificationBank original,
        const std::function<bool()>& still_armed) = 0;
    virtual bool active(const Bcm2835QualificationPlan& plan, QualificationBank original) = 0;
    virtual bool stop(const Bcm2835QualificationPlan& plan,
        QualificationBank original, unsigned worker_bus) = 0;
    virtual bool finish() = 0;
};

std::unique_ptr<QualificationHardware> openBcm2835QualificationHardware();
}
