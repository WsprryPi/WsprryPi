#include "transmission_controller.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
class Compiler final : public wsprrypi::IExecutionPlanCompiler {
public: wsprrypi::ExecutionPlan plan;
wsprrypi::ExecutionPlan compile(const wsprrypi::TransmissionRequest& request) const override { auto result=plan; result.request_id=request.id; return result; }
};
class Backend final : public wsprrypi::ITransmissionBackend {
public:
 wsprrypi::BackendCapabilities caps; wsprrypi::ExecutionResult execution{true,false,false,{}};
 wsprrypi::CleanupResult cleanup_result{true,{}}; wsprrypi::BackendCompileResult configure_result{true,{},{}}; wsprrypi::StartupQuiesceResult quiesce_result{true,{}}; int configure_calls{0}; int cleanup_calls{0}; int quiesce_calls{0};
 wsprrypi::BackendInfo info() const override { return {wsprrypi::BackendKind::RPI_CLOCK_GPIO,"double","test"}; }
 wsprrypi::BackendCapabilities capabilities() const override { return caps; }
 wsprrypi::BackendCompileResult configure(const wsprrypi::ExecutionPlan&,const wsprrypi::BackendExecutionInputs&) override { ++configure_calls; return configure_result; }
 wsprrypi::ExecutionResult execute(const wsprrypi::ExecutionPlan&) override { return execution; }
 wsprrypi::StartupQuiesceResult quiesceForStartup() override { ++quiesce_calls; return quiesce_result; }
 void stop() noexcept override {}
 wsprrypi::CleanupResult cleanup() noexcept override { ++cleanup_calls; return cleanup_result; }
};
}
int main() try {
 Compiler compiler; compiler.plan.mode=wsprrypi::TransmissionMode::WSPR;
 compiler.plan.backend=wsprrypi::BackendKind::RPI_CLOCK_GPIO;
 compiler.plan.reference_frequency_hz=144490500.0;
 compiler.plan.summary.min_frequency_hz=144490500.0; compiler.plan.summary.max_frequency_hz=144490504.4;
 compiler.plan.events.push_back({{},std::chrono::seconds{1},wsprrypi::RfEventType::RF_ON,144490500.0,true});
 Backend backend; backend.caps.output_class=wsprrypi::BackendOutputClass::NON_RF_SIMULATION;
 backend.caps.supported_modes=wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::WSPR);
 wsprrypi::TransmissionController controller(compiler,backend); wsprrypi::TransmissionRequest request; request.id.value=400;
 expect(controller.quiesceForStartup().ok && backend.quiesce_calls==1,"startup quiesce success must propagate");
 backend.quiesce_result={false,"injected startup quiesce failure"}; const auto quiesce_failure=controller.quiesceForStartup();
 expect(!quiesce_failure.ok && quiesce_failure.error=="injected startup quiesce failure" && backend.quiesce_calls==2,"startup quiesce failure must propagate unchanged");
 expect(controller.prepare(request).ok,"simulation must bypass physical GPIO band policy");
 expect(controller.prepared_plan()->request_id.value==request.id.value,"prepared plan must retain request identity");
 expect(controller.prepared_plan()->id.value==1,"first prepared plan must receive deterministic identity");
 expect(controller.prepared_plan()->id.value!=controller.prepared_plan()->request_id.value,"plan and request identities must remain distinct");
 auto result=controller.execute_prepared();
 expect(result.ok && result.cleanup_attempted && result.cleanup.ok && backend.cleanup_calls==1,"successful execution must clean up once");
 expect(controller.prepare(request).ok,"repeat prepare");
 expect(controller.prepared_plan()->id.value==2,"repeated prepare must receive a distinct plan identity");
 expect(controller.prepared_plan()->request_id.value==request.id.value,"repeat prepare must retain request identity");
 backend.cleanup_result={false,"injected cleanup failure"}; result=controller.execute_prepared();
 expect(!result.ok && result.faulted && result.cleanup_attempted,"cleanup failure must fail lifecycle");
 expect(result.error.find("injected cleanup failure")!=std::string::npos,"cleanup detail missing");
 backend.execution={false,false,true,"execution failed first"}; result=controller.execute_prepared();
 expect(result.error.find("execution failed first")!=std::string::npos && result.error.find("injected cleanup failure")!=std::string::npos,"execution failure erased");
 backend.execution={false,true,false,"cancelled first"}; result=controller.execute_prepared();
 expect(result.stopped && result.faulted && result.error.find("cancelled first")!=std::string::npos && result.error.find("injected cleanup failure")!=std::string::npos,"cancellation and cleanup failure must both remain observable");
 Backend configure_failure; configure_failure.caps=backend.caps; configure_failure.configure_result={false,{},"configure failed first"}; configure_failure.cleanup_result={false,"configure cleanup failed"};
 wsprrypi::TransmissionController failed_prepare(compiler,configure_failure); const auto prepare_result=failed_prepare.prepare(request);
 expect(!prepare_result.ok && configure_failure.cleanup_calls==1,"configure failure must clean up");
 expect(prepare_result.error.find("configure failed first")!=std::string::npos && prepare_result.error.find("configure cleanup failed")!=std::string::npos,"configure failure cleanup must preserve both errors");
 Backend unsupported; unsupported.caps.output_class=wsprrypi::BackendOutputClass::NON_RF_SIMULATION;
 unsupported.caps.supported_modes=wsprrypi::transmission_mode_bit(wsprrypi::TransmissionMode::TONE);
 wsprrypi::TransmissionController rejected(compiler,unsupported);
 expect(!rejected.prepare(request).ok && unsupported.configure_calls==0,"capability mismatch reached backend");
 std::cout<<"transmission controller contract tests passed\n"; return EXIT_SUCCESS;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return EXIT_FAILURE; }
