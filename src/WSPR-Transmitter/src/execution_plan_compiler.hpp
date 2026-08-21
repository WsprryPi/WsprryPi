#pragma once

#include "execution_plan.hpp"
#include "transmission_request.hpp"

namespace wsprrypi
{

class IExecutionPlanCompiler
{
public:
    virtual ~IExecutionPlanCompiler() = default;
    virtual ExecutionPlan compile(const TransmissionRequest& request) const = 0;
};

class ExecutionPlanCompiler : public IExecutionPlanCompiler
{
public:
    ExecutionPlan compile(const TransmissionRequest& request) const override;

private:
    ExecutionPlan compile_wspr(const TransmissionRequest& request,
                               const WsprPayload& payload) const;

    ExecutionPlan compile_qrss(const TransmissionRequest& request,
                               const QrssPayload& payload) const;

    ExecutionPlan compile_fskcw(const TransmissionRequest& request,
                                const FskcwPayload& payload) const;

    ExecutionPlan compile_dfcw(const TransmissionRequest& request,
                               const DfcwPayload& payload) const;

    ExecutionPlan compile_cw(const TransmissionRequest& request,
                             const CwPayload& payload) const;

    ExecutionPlan compile_tone(const TransmissionRequest& request,
                               const TonePayload& payload) const;

    ExecutionPlan compile_standard_feld(
        const TransmissionRequest& request,
        const StandardFeldPayload& payload) const;
};

} // namespace wsprrypi
