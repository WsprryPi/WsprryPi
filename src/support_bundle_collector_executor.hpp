#pragma once
#include "support_bundle_job_manager.hpp"
#include <chrono>
#include <mutex>
class SupportBundleCollectorExecutor final : public SupportBundleJobExecutor {
public:
    SupportBundleCollectorExecutor(std::string executable, std::chrono::milliseconds timeout = std::chrono::minutes(10), std::chrono::milliseconds grace = std::chrono::seconds(2), std::string project_version = {});
    SupportBundleExecutionResult run(const SupportBundleExecutionContext &context) override;
    void request_stop() noexcept override;
private:
    std::string executable_;
    std::chrono::milliseconds timeout_;
    std::chrono::milliseconds grace_;
    std::string project_version_;
    std::mutex mutex_;
    bool stop_ = false;
};
