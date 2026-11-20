// SPDX-License-Identifier: MIT
#include "destination_review.hpp"
#include "restore_hold.hpp"
#include "validation_request.hpp"
#include "application_control/local_transport.hpp"
#include "application_storage/application_state.hpp"
#include "application_storage/persistent_configuration.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "config_handler.hpp"
#include "controller_runtime.hpp"
#include "native_recurrence.hpp"
#include "scheduling.hpp"
#include "scheduling_internal.hpp"
#include "test_tone_request.hpp"
#include <mutex>
namespace wsprrypi {
nlohmann::json recovery_destination_review(const nlohmann::json &request,const std::string &boot){
    using Json=nlohmann::json;
    const auto require=[](bool ok){if(!ok)throw LocalCommandRefused(LocalRefusal::InvalidRequest);};
    require(request.is_object()&&request.contains("action")&&request["action"].is_string());
    const bool release=request["action"]=="release";
    require((!release&&request["action"]=="read"&&request.size()==1)||(release&&request.size()==4&&
        request.contains("digest")&&request["digest"].is_string()&&request.contains("release_local")&&request["release_local"].is_boolean()&&
        request.contains("release_remote")&&request["release_remote"].is_boolean()));
    std::unique_lock configuration(config_update_mutex(),std::try_to_lock);
    if(!configuration.owns_lock())throw LocalCommandRefused(LocalRefusal::Busy);
    const auto database=application_database();require(database&&!database->held()&&!restore_maintenance_active());
    const auto held=application_restore_hold_view();
    const auto saved=configuration_from_snapshot(database->snapshot(),database->role());
    Json view{{"schema","recovery-destination-review/1"},{"identity",database->identity()},{"boot",boot},
        {"role",database->role()==StateRole::Pi?"pi":"controller"},
        {"hold",held},{"settings",saved.configuration},{"destination_digest",recovery_destination_context(database)},
        {"remote_authority_policy","Existing pairing recovery and authority checks remain required; no remote jobs are adopted."},
        {"local_resume_policy","Releasing the hold leaves local transmission stopped. Use Start after reviewing the destination."}};
    view["digest"]=PiManagementJournal::digest(view.dump());
    if(!release)return view;
    if(request["digest"]!=view["digest"])throw LocalCommandRefused(LocalRefusal::RevisionConflict);
    require(!held["revision"].is_null()&&(request["release_local"]==true||request["release_remote"]==true));
    if(request["release_local"]==true&&database->role()==StateRole::Pi){
        (void)stop_transmission_by_user_request(false);
        require(transmitter_output_inactive_confirmed()&&!native_occurrence_active()&&!current_test_tone_status().owned);
    }
    if(database->role()==StateRole::Controller)require(controller_only_ready());
    release_application_restore_hold(database,held["revision"].get<std::string>(),request["release_local"].get<bool>(),request["release_remote"].get<bool>());
    return {{"schema","recovery-destination-review-result/1"},{"hold",application_restore_hold_view()},{"local_transmission_started",false},
        {"remote_authority_reconciled",false}};
}
}
