#pragma once
#include "transmission_backend.hpp"
#include <atomic>
#include <string>
#include <vector>
namespace wsprrypi {
struct SimulatedBackendConfig { bool virtual_time{true}; std::string trace_path; bool fail_configure{false}; long fail_event{-1}; long cancel_event{-1}; bool fail_cleanup{false}; bool fail_startup_quiesce{false}; };
class SimulatedTransmitBackend final : public ITransmissionBackend {
public:
 SimulatedTransmitBackend(IExecutionContext&,SimulatedBackendConfig={}); BackendInfo info()const override; BackendCapabilities capabilities()const override;
 BackendCompileResult configure(const ExecutionPlan&,const BackendExecutionInputs&)override; ExecutionResult execute(const ExecutionPlan&)override;
 StartupQuiesceResult quiesceForStartup()override; void stop()noexcept override; CleanupResult cleanup()noexcept override; const std::string& traceJson()const noexcept{return json_;}
private:
 struct Item{std::string kind;long index;long long ns;double hz;bool rf;std::string detail;}; void add(std::string,long,std::chrono::nanoseconds,double,bool,std::string={});void render();
 IExecutionContext& context_;SimulatedBackendConfig config_;std::atomic<bool> stopped_{false};bool configured_{false};bool cleanup_armed_{false};bool cleanup_recorded_{false};PlanId plan_id_{};RequestId request_id_{};TransmissionMode plan_mode_{TransmissionMode::WSPR};std::vector<Item> items_;std::string json_;
}; }
