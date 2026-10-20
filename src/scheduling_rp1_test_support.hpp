#pragma once

#include "json.hpp"

#include <functional>
#include <string>

using Rp1DevelopmentReconcileInvokerForTest =
    std::function<nlohmann::json(const std::string &)>;
void set_rp1_development_reconcile_invoker_for_test(
    Rp1DevelopmentReconcileInvokerForTest invoker);
void reset_rp1_development_reconcile_invoker_for_test() noexcept;
