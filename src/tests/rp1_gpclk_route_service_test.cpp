#include "../rp1_gpclk_route_service.hpp"
#include "../WSPR-Transmitter/src/rp1_gpclk_development_policy.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

nlohmann::json state(const std::string &configured = "gpio4",
                     const std::string &active = "gpio4") {
  return {
      {"configuredRoute", configured},
      {"activeRoute", active},
      {"bootId", "0fc31228-4c38-4f05-8053-afb9f04fba52"},
      {"bootOwnership", "current"},
      {"pendingTransaction", nullptr},
      {"historicalJournals",
       {{{"schemaVersion", 1},
         {"status", "complete"},
         {"preservation", "in-place-byte-exact"}}}},
      {"safety",
       {{"endpointOwned", true},
        {"endpointOpen", false},
        {"outputInhibited", false},
        {"operationalReady", true},
        {"services",
         {{"wsprrypi.service", "active"},
          {"soapyremote-server.service", "inactive"}}}}}};
}

nlohmann::json response(const std::string &operation, const std::string &status,
                        const nlohmann::json &value) {
  return {{"contract", "rp1-gpclk-route-manager"},
          {"operation", operation},
          {"status", status},
          {"state", value}};
}

wsprrypi::Rp1GpclkApplicationIdleState idle() { return {}; }

wsprrypi::Rp1GpclkDevelopmentPolicyInputs armed_for(std::uint32_t route) {
  wsprrypi::Rp1GpclkDevelopmentPolicyInputs inputs;
  inputs.development_testing_enabled = true;
  inputs.rp1_backend_selected = true;
  inputs.requested_route = route;
  return inputs;
}

wsprrypi::Rp1GpclkDevelopmentPolicyInputs authorized_for(
    std::uint32_t route, std::uint64_t generation) {
  auto inputs = armed_for(route);
  inputs.persisted_route = route;
  inputs.configured_route = route;
  inputs.active_route = route;
  inputs.module_route = route;
  inputs.active_route_count = 1;
  inputs.route_transaction_resolved = true;
  inputs.scheduler_idle = true;
  inputs.application_owns_operation = true;
  inputs.endpoint_available = true;
  inputs.endpoint_closed = true;
  inputs.endpoint_exclusively_acquirable = true;
  inputs.physical_connection_confirmed = true;
  inputs.attenuation_and_load_confirmed = true;
  inputs.bounded_operation_confirmed = true;
  inputs.non_radiating_topology_confirmed = true;
  inputs.experimental_status_acknowledged = true;
  inputs.confirmation_current = true;
  inputs.route_transaction_generation = generation;
  inputs.confirmation_route_transaction_generation = generation;
  inputs.operation_id = "post-start-bounded-tone";
  inputs.confirmation_operation_id = inputs.operation_id;
  inputs.confirmation_route = route;
  inputs.identity.route = route;
  return inputs;
}

struct StartupOutcome {
  nlohmann::json result;
  std::vector<nlohmann::json> requests;
  bool inhibited{false};
};

StartupOutcome startup(const nlohmann::json &next, int persisted,
                       const std::string &development_route = {},
                       bool idle_startup = false) {
  StartupOutcome outcome;
  wsprrypi::Rp1GpclkRouteService service(
      {[&](const nlohmann::json &request) {
         outcome.requests.push_back(request);
         return next;
       },
       idle, [persisted] { return persisted; },
       [](int, std::string *) { return true; },
       [&](bool value, const std::string &) { outcome.inhibited = value; }});
  outcome.result = idle_startup
      ? service.reconcileIdleStartup(development_route)
      : development_route.empty()
          ? service.reconcileStartup()
          : service.reconcileDevelopmentStartup(development_route);
  if (!outcome.result.value("ok", false)) {
    const auto request_count = outcome.requests.size();
    const auto second = idle_startup
        ? service.reconcileIdleStartup(development_route)
        : development_route.empty()
            ? service.reconcileStartup()
            : service.reconcileDevelopmentStartup(development_route);
    assert(second.at("result") == "startup_failure_latched");
    assert(outcome.requests.size() == request_count);
    assert(outcome.inhibited);
  }
  return outcome;
}

} // namespace

int main() {
  std::vector<nlohmann::json> requests;
  nlohmann::json next = response("query", "ok", state());
  int persisted = 4;
  bool persistence_ok = true;
  int persisted_write = 0;
  bool inhibited = false;
  bool runtime_launch_fails = false;
  int qualification_queries = 0;
  std::string runtime_plan_classification = "neutral_ready";
  bool runtime_plan_operational_ready = false;
  bool runtime_plan_operational_ready_reported = true;
  bool runtime_plan_already_ready = false;
  std::string runtime_plan_active_route;
  std::vector<std::string> runtime_plans;
  std::vector<nlohmann::json> runtime_launches;

  wsprrypi::Rp1GpclkRouteService service(
      {[&](const nlohmann::json &request) {
         requests.push_back(request);
         return next;
       },
       idle, [&] { return persisted; },
       [&](int gpio, std::string *) {
         persisted_write = gpio;
         if (persistence_ok)
           persisted = gpio;
         return persistence_ok;
       },
       [&](bool value, const std::string &) { inhibited = value; },
       [&](const std::string &route) {
         runtime_plans.push_back(route);
         nlohmann::json safety = {{"outputInhibited", false},
                                  {"owner", false}, {"lease", false}};
         if (runtime_plan_operational_ready_reported)
           safety["operationalReady"] = runtime_plan_operational_ready;
         const bool exact = runtime_plan_classification == "exact_ready";
         return nlohmann::json{
             {"contract", "rp1-gpclk-runtime-readiness-v1"},
             {"result", runtime_plan_classification},
             {"state", runtime_plan_classification},
             {"administrationEligible", true},
             {"executionReady", exact},
             {"routeSelected", exact},
             {"routes", {{"active", runtime_plan_active_route.empty()
                                          ? nlohmann::json(nullptr)
                                          : nlohmann::json(runtime_plan_active_route)}}},
             {"safety", safety},
             {"routePlan", {{"operation", "select"}, {"route", route},
                            {"alreadyReady", runtime_plan_already_ready},
                            {"bindingSha256", std::string(64, 'a')},
                            {"planSha256", std::string(64, 'b')}}}};
       },
       [&](const std::string &operation, const std::string &route,
           const std::string &digest, const std::string &binding,
           const std::string &request_id) {
         if (runtime_launch_fails)
           throw std::runtime_error("submission failed");
         runtime_launches.push_back({{"operation", operation}, {"route", route},
             {"digest", digest}, {"binding", binding},
             {"requestId", request_id}});
       },
       [&](const std::string &route) {
         ++qualification_queries;
         return nlohmann::json{{"status", "verified"},
             {"route", route}, {"state", "Qualified (scoped TONE evidence)"},
             {"structuralCompatibility", "Compatible-unqualified"},
             {"permissionGate", false}};
       }});

  const auto query = service.query();
  assert(query.at("ok") == true);
  assert(query.at("compatible") == true);
  assert(query.at("eligible") == false);
  assert(query.at("liveQualification") == "Qualified (scoped TONE evidence)");
  assert(query.at("structuralCompatibility") == "Compatible-unqualified");
  assert(query.at("qualificationEvidence").at("route") == "gpio4");
  assert(query.at("requested") == "GPIO4");
  assert(query.at("configured") == "GPIO4");
  assert(query.at("active") == "GPIO4");
  assert(query.at("bootOwnership") == "current");
  assert(query.at("journal") == "none");
  assert(requests.size() == 1);
  assert(requests.back() ==
         nlohmann::json({{"operation", "query"}}));
  assert(qualification_queries == 1);
  assert(service.query(false).at("liveQualification") == "Not inspected");
  assert(qualification_queries == 1);

  next = response("preflight", "ok", state());
  auto preflight = service.operate("preflight", "GPIO20", 0);
  assert(preflight.at("ok") == true);
  assert(preflight.at("preflightValidated") == true);
  assert(preflight.at("services").at("wsprrypi.service") == "active");
  const auto generation = preflight.at("generation").get<std::uint64_t>();
  assert(generation != 0);
  assert(requests.back().at("route") == "gpio20");

  auto unsafe = state();
  unsafe["safety"]["endpointOpen"] = true;
  next = response("preflight", "ok", unsafe);
  assert(service.operate("preflight", "GPIO20", 0).at("ok") == false);
  unsafe = state();
  unsafe["safety"]["endpointOwned"] = false;
  next = response("preflight", "ok", unsafe);
  assert(service.operate("preflight", "GPIO20", 0).at("ok") == false);
  unsafe = state();
  unsafe["safety"]["outputInhibited"] = true;
  next = response("preflight", "ok", unsafe);
  assert(service.operate("preflight", "GPIO20", 0).at("ok") == false);
  unsafe = state();
  unsafe["safety"]["operationalReady"] = false;
  next = response("preflight", "ok", unsafe);
  assert(service.operate("preflight", "GPIO20", 0).at("ok") == false);
  next = response("preflight", "ok", state("gpio4", "gpio4"));
  preflight = service.operate("preflight", "GPIO20", 0);
  const auto apply_generation = preflight.at("generation").get<std::uint64_t>();
  next = response("apply-and-reboot", "reboot-requested",
                  state("gpio20", "gpio4"));
  const auto applied =
      service.operate("apply-and-reboot", "GPIO20", apply_generation);
  assert(applied.at("ok") == true);
  assert(persisted_write == 20);
  assert(requests.back().at("operation") == "apply-and-reboot");
  assert(requests.back().at("route") == "gpio20");
  assert(requests.back().at("execute") == true);
  assert(requests.back().at("actor") == "wsprrypi.service");
  assert(!requests.back().contains("path"));
  assert(!requests.back().contains("service"));
  assert(!requests.back().contains("command"));

  persistence_ok = false;
  next = response("preflight", "ok", state("gpio20", "gpio20"));
  preflight = service.operate("preflight", "GPIO4", 0);
  const auto before_failed_apply = requests.size();
  assert(service
             .operate("apply-and-reboot", "GPIO4",
                      preflight.at("generation").get<std::uint64_t>())
             .at("result") == "persistence_failed");
  assert(requests.size() == before_failed_apply);
  persistence_ok = true;

  next = response("reconcile", "complete", state("gpio20", "gpio20"));
  const auto reconciled = service.reconcileStartup();
  assert(reconciled.at("reconciled") == true);
  assert(reconciled.at("compatible") == true);
  assert(inhibited == false);

  auto external_provider = state("gpio20", "gpio20");
  external_provider["identity"] = {{"arbitrary", "ignored"}};
  wsprrypi::armRp1GpclkDevelopmentOperation(
      armed_for(wsprrypi::kRp1GpclkDevelopmentRouteGpio20));
  const auto external_reconciled =
      startup(response("reconcile", "complete", external_provider), 20);
  assert(external_reconciled.result.at("compatible") == true);
  assert(!external_reconciled.inhibited);
  wsprrypi::invalidateRp1GpclkDevelopmentOperation();

  auto pending = state("gpio20", "gpio4");
  pending["pendingTransaction"] = {{"status", "awaiting-reboot"}};
  const auto awaiting =
      startup(response("reconcile", "awaiting-reboot", pending), 20);
  assert(awaiting.result.at("ok") == false);
  assert(awaiting.result.at("journal") == "pending");
  assert(awaiting.inhibited);

  const auto mismatch =
      startup(response("reconcile", "mismatch", state("gpio20", "gpio4")), 20);
  assert(mismatch.result.at("ok") == false);
  assert(mismatch.result.at("state") == "mismatch");
  assert(mismatch.inhibited);

  auto recovery_state = state("gpio20", "gpio4");
  recovery_state["pendingTransaction"] = {{"status", "interrupted"}};
  const auto recovery = startup(
      response("reconcile", "recovery-required", recovery_state), 20);
  assert(recovery.result.at("ok") == false);
  assert(recovery.result.at("state") == "rollback_required");
  assert(recovery.inhibited);

  auto predecessor = state();
  predecessor["identity"]["debianVersion"] = "1.0.0-1";
  predecessor["identity"]["moduleVersion"] = "1.0.0";

  {
    nlohmann::json lifecycle_next = response("query", "ok", predecessor);
    std::vector<nlohmann::json> lifecycle_requests;
    bool lifecycle_inhibited = false;
    wsprrypi::Rp1GpclkRouteService lifecycle(
        {[&](const nlohmann::json &request) {
           lifecycle_requests.push_back(request);
           return lifecycle_next;
         },
         idle, [] { return 4; },
         [](int, std::string *) { return true; },
         [&](bool value, const std::string &) {
           lifecycle_inhibited = value;
         }});
    wsprrypi::invalidateRp1GpclkDevelopmentOperation();
    const auto idle_result = lifecycle.reconcileIdleStartup("GPIO4");
    assert(idle_result.at("ok") == true);
    assert(idle_result.at("executionAuthorized") == false);
    assert(!wsprrypi::rp1GpclkDevelopmentOperationArmedForRoute(
        wsprrypi::kRp1GpclkDevelopmentRouteGpio4));

    const auto development = lifecycle.reconcileDevelopmentStartup("GPIO4");
    assert(development.at("ok") == true);
    assert(development.at("generation").get<std::uint64_t>() != 0);
    auto authorization = authorized_for(
        wsprrypi::kRp1GpclkDevelopmentRouteGpio4,
        development.at("generation").get<std::uint64_t>());
    wsprrypi::armRp1GpclkDevelopmentOperation(authorization);
    auto consumed = wsprrypi::consumeRp1GpclkDevelopmentOperation(
        authorization.operation_id, authorization.requested_route,
        authorization.identity);
    assert(consumed.has_value());
    assert(wsprrypi::decideRp1GpclkDevelopmentUse(*consumed).allowed);
    assert(!wsprrypi::consumeRp1GpclkDevelopmentOperation(
        authorization.operation_id, authorization.requested_route,
        authorization.identity).has_value());
    assert(!lifecycle_requests.empty());
    assert(!lifecycle_inhibited);
  }

  for (const auto &route : {std::string("GPIO4"), std::string("GPIO20")}) {
    const int gpio = route == "GPIO4" ? 4 : 20;
    auto route_state = predecessor;
    route_state["configuredRoute"] = gpio == 4 ? "gpio4" : "gpio20";
    route_state["activeRoute"] = gpio == 4 ? "gpio4" : "gpio20";
    wsprrypi::invalidateRp1GpclkDevelopmentOperation();
    const auto idle_result =
        startup(response("query", "ok", route_state), gpio, route, true);
    assert(idle_result.result.at("ok") == true);
    assert(idle_result.result.at("policyDomain") == "startup-idle");
    assert(idle_result.result.at("executionAuthorized") == false);
    assert(idle_result.result.at("developmentAuthorizationRequired") == true);
    assert(idle_result.requests.size() == 1);
    assert(idle_result.requests.front().at("operation") == "query");
    assert(!wsprrypi::rp1GpclkDevelopmentOperationArmedForRoute(
        gpio == 4 ? wsprrypi::kRp1GpclkDevelopmentRouteGpio4
                  : wsprrypi::kRp1GpclkDevelopmentRouteGpio20));
    assert(idle_result.inhibited);
  }

  const auto external_idle =
      startup(response("query", "ok", state()), 4, "GPIO4", true);
  assert(external_idle.result.at("ok") == true);
  assert(external_idle.result.at("policyDomain") == "startup-idle");
  assert(external_idle.result.at("executionAuthorized") == false);
  assert(external_idle.requests.size() == 1);
  assert(external_idle.requests.front().at("operation") == "query");

  auto idle_pending = predecessor;
  idle_pending["pendingTransaction"] = {{"status", "awaiting-reboot"}};
  const auto pending_idle = startup(
      response("query", "ok", idle_pending), 4, "GPIO4", true);
  assert(pending_idle.result.at("ok") == false);
  assert(pending_idle.inhibited);

  auto idle_endpoint_open = predecessor;
  idle_endpoint_open["safety"]["endpointOpen"] = true;
  const auto unsafe_idle = startup(
      response("query", "ok", idle_endpoint_open), 4, "GPIO4", true);
  assert(unsafe_idle.result.at("result") == "idle_startup_endpoint_unsafe");
  assert(unsafe_idle.inhibited);

  for (const auto &route : {std::string("GPIO4"), std::string("GPIO20")}) {
    const int gpio = route == "GPIO4" ? 4 : 20;
    auto route_state = predecessor;
    route_state["configuredRoute"] = gpio == 4 ? "gpio4" : "gpio20";
    route_state["activeRoute"] = gpio == 4 ? "gpio4" : "gpio20";
    const auto development_route = gpio == 4
        ? wsprrypi::kRp1GpclkDevelopmentRouteGpio4
        : wsprrypi::kRp1GpclkDevelopmentRouteGpio20;
    wsprrypi::armRp1GpclkDevelopmentOperation(armed_for(development_route));
    const auto development =
        startup(response("query", "ok", route_state), gpio, route);
    assert(development.result.at("ok") == true);
    assert(development.result.at("policyDomain") == "external-provider");
    assert(development.requests.size() == 1);
    assert(development.requests.front().at("operation") == "query");
    assert(!development.requests.front().contains("execute"));
    assert(!development.inhibited);
    assert(wsprrypi::rp1GpclkDevelopmentOperationArmedForRoute(
        development_route));
    wsprrypi::invalidateRp1GpclkDevelopmentOperation();
  }

  const auto wrong_development_route =
      startup(response("query", "ok", state("gpio20", "gpio20")), 20,
              "GPIO4");
  assert(wrong_development_route.result.at("ok") == false);
  assert(wrong_development_route.inhibited);

  auto development_endpoint_open = predecessor;
  development_endpoint_open["safety"]["endpointOpen"] = true;
  const auto unsafe_development = startup(
      response("query", "ok", development_endpoint_open), 4, "GPIO4");
  assert(unsafe_development.result.at("ok") == false);
  assert(unsafe_development.inhibited);

  auto ambiguous = state();
  ambiguous["pendingTransaction"] = {{"status", "awaiting-reboot"}};
  const auto ambiguous_development =
      startup(response("query", "ok", ambiguous), 4, "GPIO4");
  assert(ambiguous_development.result.at("ok") == false);
  assert(ambiguous_development.inhibited);

  next = response("rollback", "rolled-back", state("gpio4", "gpio4"));
  const auto rolled_back = service.operate("rollback", "GPIO4", 7);
  assert(rolled_back.at("ok") == true);
  assert(requests.back().at("operation") == "rollback");
  assert(requests.back().at("execute") == true);
  assert(!requests.back().contains("route"));

  next = {{"schemaVersion", 2},
          {"contract", "foreign"},
          {"status", "ok"},
          {"state", state()}};
  assert(service.query().at("result") == "contract_mismatch");

  next = {{"schemaVersion", 3}, {"contract", "rp1-gpclk-route-manager-runtime"},
          {"status", "ok"}, {"state", {{"profile", "runtime"}, {"activeRoute", "gpio4"},
          {"outputEnabled", false}, {"qualification", false}, {"controller", {{"id", 9}}},
          {"applicationRestoration", true}, {"preflightToken", std::string(64, 'a')}}}};
  assert(service.query().at("profile") == "runtime");
  assert(inhibited);
  auto runtime_preflight = service.operate("preflight", "GPIO20", 0);
  assert(runtime_preflight.at("ok") == true);
  assert(runtime_preflight.at("state") == "runtime_preflight_ready");
  assert(runtime_preflight.at("changeStarted") == false);
  assert(runtime_preflight.at("recoveryRequired") == false);
  assert(runtime_plans.back() == "gpio20");
  assert(runtime_preflight.at("planSha256") == std::string(64, 'b'));
  const auto initial_runtime_generation =
      runtime_preflight.at("generation").get<std::uint64_t>();
  runtime_plan_operational_ready = true;
  auto contradictory_neutral = service.operate("preflight", "GPIO20", 0);
  assert(contradictory_neutral.at("ok") == false);
  assert(contradictory_neutral.at("state") == "runtime_preflight_failed");
  assert(contradictory_neutral.at("changeStarted") == false);
  assert(contradictory_neutral.at("recoveryRequired") == false);
  assert(service.operate("switch", "GPIO20", initial_runtime_generation).at("ok") == false);
  assert(runtime_launches.empty());
  runtime_plan_operational_ready_reported = false;
  assert(service.operate("preflight", "GPIO20", 0).at("ok") == false);
  runtime_plan_operational_ready_reported = true;
  runtime_plan_operational_ready = false;
  runtime_plan_already_ready = true;
  assert(service.operate("preflight", "GPIO20", 0).at("ok") == false);
  runtime_plan_already_ready = false;
  runtime_plan_classification = "exact_ready";
  runtime_plan_operational_ready = true;
  const auto missing_exact_route = service.operate("preflight", "GPIO20", 0);
  assert(missing_exact_route.at("ok") == false);
  assert(missing_exact_route.at("state") == "runtime_preflight_failed");
  runtime_plan_active_route = "gpio4";
  const auto gpio4_to_gpio20 = service.operate("preflight", "GPIO20", 0);
  assert(gpio4_to_gpio20.at("ok") == true);
  assert(gpio4_to_gpio20.at("state") == "runtime_preflight_ready");
  runtime_plan_active_route = "gpio20";
  const auto gpio20_idempotent = service.operate("preflight", "GPIO20", 0);
  assert(gpio20_idempotent.at("ok") == false);
  runtime_plan_already_ready = true;
  assert(service.operate("preflight", "GPIO20", 0).at("ok") == true);
  const auto gpio20_to_gpio4 = service.operate("preflight", "GPIO4", 0);
  assert(gpio20_to_gpio4.at("ok") == false);
  runtime_plan_already_ready = false;
  assert(service.operate("preflight", "GPIO4", 0).at("ok") == true);
  runtime_plan_active_route = "gpio4";
  runtime_plan_already_ready = true;
  assert(service.operate("preflight", "GPIO4", 0).at("ok") == true);
  const auto contradictory_exact = service.operate("preflight", "GPIO20", 0);
  assert(contradictory_exact.at("ok") == false);
  assert(contradictory_exact.at("state") == "runtime_preflight_failed");
  assert(contradictory_exact.at("changeStarted") == false);
  assert(contradictory_exact.at("recoveryRequired") == false);
  runtime_plan_already_ready = false;
  runtime_plan_operational_ready = false;
  const auto unready_exact = service.operate("preflight", "GPIO20", 0);
  assert(unready_exact.at("ok") == false);
  assert(unready_exact.at("state") == "runtime_preflight_failed");
  assert(unready_exact.at("changeStarted") == false);
  assert(unready_exact.at("recoveryRequired") == false);
  runtime_plan_classification = "neutral_ready";
  runtime_plan_active_route.clear();
  runtime_plan_already_ready = false;
  runtime_preflight = service.operate("preflight", "GPIO20", 0);
  assert(runtime_preflight.at("ok") == true);
  const auto reviewed_generation =
      runtime_preflight.at("generation").get<std::uint64_t>();
  assert(service.operate("switch", "GPIO20", reviewed_generation + 1).at("ok") == false);
  assert(service.operate("apply-and-reboot", "GPIO20", reviewed_generation).at("ok") == false);
  const int saved_before_runtime_switch = persisted;
  next["status"] = "complete-inhibited";
  assert(service.operate("switch", "GPIO20", reviewed_generation).at("ok") == true);
  assert(persisted == saved_before_runtime_switch);
  assert(runtime_launches.size() == 1);
  assert(runtime_launches.front().at("operation") == "switch");
  assert(runtime_launches.front().at("route") == "gpio20");
  assert(runtime_launches.front().at("digest") == std::string(64, 'b'));
  assert(runtime_launches.front().at("binding") == std::string(64, 'a'));
  assert(inhibited);
  assert(service.operate("switch", "GPIO20", reviewed_generation).at("ok") == false);
  runtime_launch_fails = true;
  const auto corrupt_preflight = service.operate("preflight", "GPIO20", 0);
  assert(corrupt_preflight.at("ok") == true);
  assert(service.operate("switch", "GPIO20",
                         corrupt_preflight.at("generation").get<std::uint64_t>())
             .at("ok") == false);
  runtime_launch_fails = false;
  const auto runtime_failure = service.operate("recover", "GPIO20", 0);
  assert(runtime_failure.at("ok") == true);
  assert(runtime_failure.at("state") == "runtime_recovery_queued");
  assert(runtime_launches.back().at("operation") == "recover");
  assert(runtime_launches.back().at("digest") == "");
  assert(runtime_launches.back().at("binding") == "");
  const auto runtime_remove = service.operate("remove", "GPIO20", 0);
  assert(runtime_remove.at("ok") == true);
  assert(runtime_remove.at("state") == "runtime_remove_queued");
  assert(runtime_launches.back().at("operation") == "remove");
  assert(runtime_launches.back().at("route") == "gpio20");
  assert(runtime_launches.back().at("digest") == "");
  assert(runtime_launches.back().at("binding") == "");
  runtime_launch_fails = true;
  assert(service.operate("recover", "GPIO20", 0).at("ok") == false);
  runtime_launch_fails = false;
  assert(inhibited);
  next = response("query", "ok", state());
  assert(service.query().at("result") == "contract_mismatch");
  assert(inhibited);

  // Runtime reconciliation uses real current-route evidence, not a boot overlay.
  nlohmann::json ctl = {{"id",1},{"route",2},{"flags",6},{"error",0},
                        {"session",1234},{"generation",7}};
  nlohmann::json lifecycle = {{"ready",true},{"executionAuthorized",false},
      {"route","gpio20"},{"controller",ctl},{"bootId","current-boot"},
      {"bindingSha256",std::string(64,'a')}};
  auto runtime = nlohmann::json{{"schemaVersion",3},
      {"contract","rp1-gpclk-route-manager-runtime"},{"status","ok"},
      {"state",{{"profile","runtime"},{"outputEnabled",false},{"qualification",false},
          {"controller",ctl},{"bootId","current-boot"},{"bindingSha256",std::string(64,'a')},
          {"outputLifecycle",lifecycle}}}};
  bool runtime_inhibited = true;
  std::vector<nlohmann::json> runtime_requests;
  wsprrypi::Rp1GpclkRouteService runtime_service({
      [&](const nlohmann::json &value) { runtime_requests.push_back(value); return runtime; },
      idle, [] {return 20;}, [](int,std::string*) {return false;},
      [&](bool value,const std::string&) {runtime_inhibited=value;}});
  auto idle_runtime = runtime_service.reconcileIdleStartup("GPIO20");
  assert(idle_runtime.at("ok") == true && runtime_inhibited);
  assert(idle_runtime.at("executionAuthorized") == false);
  assert(runtime_requests.back().at("operation") == "idle");
  auto development_runtime = runtime_service.reconcileDevelopmentStartup("GPIO20");
  assert(development_runtime.at("ok") == true && !runtime_inhibited);
  assert(development_runtime.at("executionAuthorized") == false);
  assert(runtime_requests.back().at("operation") == "reconcile-output");
  assert(development_runtime.at("generation") != idle_runtime.at("generation"));
  runtime["state"]["outputLifecycle"]["controller"]["route"] = 1;
  assert(runtime_service.reconcileDevelopmentStartup("GPIO20").at("ok") == false);
  assert(runtime_inhibited);
  runtime["state"]["outputLifecycle"] = lifecycle;
  runtime["state"]["outputLifecycle"]["executionAuthorized"] = true;
  assert(runtime_service.reconcileDevelopmentStartup("GPIO20").at("ok") == false);
  assert(runtime_inhibited);

  runtime["state"]["outputLifecycle"] = lifecycle;
  assert(!runtime_service.acknowledgeRestoration("offline-token", true));
  assert(runtime_service.acknowledgeRestoration("offline-token", false));
  assert(runtime_requests.back().at("operation") == "application-ready");
  assert(runtime_requests.back().at("route") == "gpio20");
  assert(runtime_requests.back().at("transmit") == false);
  runtime["state"]["activeRoute"] = "gpio20";
  runtime["state"]["application"] = {{"phase","restored"}, {"controller",ctl},
      {"boot","current-boot"}, {"binding",std::string(64,'a')}};
  auto restored = runtime_service.query();
  assert(restored.at("state") == "runtime_ready" && restored.at("reconciled") == true);
  assert(restored.at("message") ==
         "GPIO20 is active and Wsprry Pi is back online in idle mode. "
         "This does not start or authorize transmission.");
  runtime["state"]["controller"]["flags"] = 7;
  assert(runtime_service.query().at("state") == "runtime_recovery");
  runtime["state"]["controller"] = ctl;
  runtime["state"]["application"]["phase"] = "restoration-failed";
  runtime["state"]["application"]["error"] = "start failed";
  assert(runtime_service.query().at("state") == "runtime_restoration_failed");
  const nlohmann::json neutral_ctl = {{"id",0},{"route",0},{"flags",0},{"error",0},
                                      {"session",1234},{"generation",8}};
  runtime["state"]["controller"] = neutral_ctl;
  runtime["state"]["activeRoute"] = nullptr;
  runtime["state"]["pendingTransaction"] = {{"phase","recovered-inhibited"}};
  runtime["state"]["application"] = {{"phase","neutral-restored"},
      {"boot","current-boot"}, {"binding",std::string(64,'a')}};
  const auto neutral_running = runtime_service.query();
  assert(neutral_running.at("state") == "runtime_neutral_running");
  assert(neutral_running.at("active") == "None");
  assert(neutral_running.at("configured") == "None");
  runtime["state"]["application"]["phase"] = "neutral-stopped";
  assert(runtime_service.query().at("state") == "runtime_neutral_stopped");
  runtime["state"]["application"]["phase"] = "neutral-restoration-failed";
  runtime["state"]["application"]["error"] = "start failed";
  assert(runtime_service.query().at("state") ==
         "runtime_neutral_restoration_failed");

  for (const char* bad_field : {"profile", "safety", "controller"}) {
    auto malformed = runtime;
    malformed["state"][bad_field] = 17;
    wsprrypi::Rp1GpclkRouteService malformed_service({
        [&](const nlohmann::json&) { return malformed; }, idle, [] { return 20; },
        [](int, std::string*) { return false; }, [](bool, const std::string&) {}});
    assert(malformed_service.query().at("ok") == false);
    assert(malformed_service.reconcileDevelopmentStartup("GPIO20").at("ok") == false);
  }
  bool reject_provider = true;
  wsprrypi::Rp1GpclkRouteService rejecting_service({
      [&](const nlohmann::json&) -> nlohmann::json {
        if (reject_provider) throw std::runtime_error("provider EOPNOTSUPP exact detail");
        return runtime;
      }, idle, [] { return 20; }, [](int, std::string*) { return false; },
      [](bool, const std::string&) {}});
  const auto refused = rejecting_service.query();
  assert(refused.at("ok") == false &&
         refused.at("message").get<std::string>().find("EOPNOTSUPP exact detail") != std::string::npos);
  assert(rejecting_service.reconcileDevelopmentStartup("GPIO20").at("message")
             .get<std::string>().find("EOPNOTSUPP exact detail") != std::string::npos);
  wsprrypi::Rp1GpclkRouteService rejecting_startup({
      [](const nlohmann::json&) -> nlohmann::json {
        throw std::runtime_error("provider startup refusal detail");
      }, idle, [] { return 20; }, [](int, std::string*) { return false; },
      [](bool, const std::string&) {}});
  assert(rejecting_startup.reconcileStartup().at("message").get<std::string>()
             .find("provider startup refusal detail") != std::string::npos);
  wsprrypi::Rp1GpclkRouteService rejecting_idle({
      [](const nlohmann::json&) -> nlohmann::json {
        throw std::runtime_error("provider idle refusal detail");
      }, idle, [] { return 20; }, [](int, std::string*) { return false; },
      [](bool, const std::string&) {}});
  assert(rejecting_idle.reconcileIdleStartup("GPIO20").at("message").get<std::string>()
             .find("provider idle refusal detail") != std::string::npos);
  reject_provider = false;
  assert(rejecting_service.query().is_object());

  std::cout << "rp1_gpclk_route_service_test: PASS\n";
}
