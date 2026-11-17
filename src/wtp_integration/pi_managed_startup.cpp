// SPDX-License-Identifier: MIT
#include "pi_managed_startup.hpp"
#include "config_handler.hpp"
#include "config_handler_serialization.hpp"
#include "wtp_pi_control.hpp"
#include "wtp_settings_json.hpp"
#include "pi_managed_records.hpp"
#include "scheduling.hpp"
#include "runtime_config_bridge.hpp"
#include "recurrence_admission.hpp"
#include "controller_contract.hpp"
#include "native_recurrence.hpp"
#include "wtp_endpoint/runtime.hpp"
#include <charconv>
#include <stdexcept>
#include <filesystem>
#include <atomic>
namespace wsprrypi {
namespace {
// Access is serialized by the owning configuration mutex, including teardown.
const void *installed_owner{};
void require(bool value){if(!value)throw std::runtime_error("pi_management_startup_hold");}
}
struct PiManagedStartup::State {
    enum class Phase {Unprepared,Bootstrapping,Ready,Held};
    std::string ini,self;
    std::filesystem::path input_parent;
    std::optional<WtpSettings> bootstrap_active;
    WtpCatalog catalog;
    PiManagedStore store;
    ConfigurationGate gate;
    std::atomic<Phase> phase{Phase::Unprepared};
    std::atomic_bool validated{false};
    std::optional<ConfigIniDraftSnapshot> draft;
    std::optional<PiManagedStore::Operation> recovery_operation;
    bool recovery_attempted{};
    State(std::string filename,std::string identity)
        :ini(std::move(filename)),self(std::move(identity)),
         input_parent(std::filesystem::canonical(std::filesystem::path(ini).has_parent_path()?std::filesystem::path(ini).parent_path():".")),
         catalog(ini+".wtp-devices.json"),
         store(ini,self,anchor()){}
    PiManagementAnchor anchor(){
        return {[this]{return catalog.pi_management_anchor(self,store.initialized()?std::nullopt:bootstrap_active).read();},
            [this](const auto &history,const auto &commit){catalog.pi_management_anchor(self,store.initialized()?std::nullopt:bootstrap_active).coordinate(history,commit);}};
    }
    void source(const nlohmann::json &internal){
        require(internal.is_object()&&internal.contains("Meta")&&internal["Meta"].is_object()&&
            internal["Meta"].contains("Use INI")&&internal["Meta"]["Use INI"].is_boolean()&&internal["Meta"]["Use INI"]==true&&
            internal["Meta"].contains("INI Filename")&&internal["Meta"]["INI Filename"].is_string()&&internal["Meta"]["INI Filename"]==ini);
        require(phase==Phase::Bootstrapping||phase==Phase::Ready);
        if(!store.initialized()){
            const auto configuration=config_handler_serialization::saved_configuration_from_internal_json(internal);
            bootstrap_active.reset();
            if(configuration.at("Operation").at("Transmit Backend")=="wtp")
                bootstrap_active=parse_wtp_settings(configuration.at("WTP"),true);
        }
    }
    void guarded(const std::function<void()> &action){
        if(phase==Phase::Bootstrapping){action();return;}
        require(phase==Phase::Ready&&bool(gate));
        bool attempted=false,completed=false;
        gate([&]{require(!attempted);attempted=true;action();completed=true;});
        require(completed);
    }
    bool ready() noexcept {
        try{
            // The store serializes its own bounded complete transaction. A
            // concurrent settings read/edit is not evidence of invalid state
            // and must not reject an already accepted outgoing WTP job's ARM.
            // Atomic lifecycle flags still close teardown/startup immediately;
            // store verification continues to reject external/corrupt history.
            if(phase!=Phase::Ready||!validated)return false;
            (void)store.settings();return phase==Phase::Ready&&validated;
        }catch(...){return false;}
    }
};
PiManagedStartup::PiManagedStartup(std::string ini,std::string self)
    :state_(std::make_shared<State>(std::move(ini),std::move(self))){}
PiManagedStartup::~PiManagedStartup(){
    std::lock_guard lock(config_update_mutex());
    if(installed_owner!=state_.get())return;
    state_->phase=State::Phase::Held;
    state_->gate={};
    iniFile.set_file_reader({});
    iniFile.set_file_writer({});
    set_config_persistence_owner({},{},{});
    set_native_solar_snapshot_provider({});
    reset_native_recurrence_observation();
    set_legacy_recurrence_admission([]{return LegacyRecurrenceAdmission::Held;});
    // Teardown/failure is never permission to revert to an unguarded output
    // lane. A new explicitly prepared owner or process restart replaces this.
    set_wtp_pi_management_readiness([]{return false;});
    installed_owner=nullptr;
}
void PiManagedStartup::prepare(){
    std::lock_guard lock(config_update_mutex());
    require(!installed_owner&&state_->phase==State::Phase::Unprepared);
    installed_owner=state_.get();state_->phase=State::Phase::Held;
    const auto state=state_;
    iniFile.set_file_reader([state](const std::string &path){
        require(state->phase==State::Phase::Bootstrapping||state->phase==State::Phase::Ready);
        const auto prefix=state->ini+".preview.";
        const bool preview=path.starts_with(prefix)&&path.size()==prefix.size()+6&&
            path.substr(prefix.size()).find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")==std::string::npos;
        require(path==state->ini||path==state->ini+".stock"||preview);
        const auto parent=std::filesystem::path(path).has_parent_path()?std::filesystem::path(path).parent_path():std::filesystem::path(".");
        require(std::filesystem::canonical(parent)==state->input_parent);
        return PiManagementJournal::read_ini_input(path).bytes;
    });
    iniFile.set_file_writer([state](const std::string &path,const std::string &bytes){
        require((state->phase==State::Phase::Bootstrapping||state->phase==State::Phase::Ready)&&path==state->ini);
        const auto parent=std::filesystem::path(path).has_parent_path()?std::filesystem::path(path).parent_path():std::filesystem::path(".");
        require(std::filesystem::canonical(parent)==state->input_parent);
        state->store.write_committed_ini(bytes);
    });
    set_wtp_pi_management_readiness([state]{return state->ready();},[state]{state->phase=State::Phase::Held;});
    // Install the refusing owners before recovery too. A storage failure must
    // not leave a protected web process with the legacy uncoordinated writer.
    set_config_persistence_owner(
        [state](const auto &internal,const auto &persist){
            state->source(internal);state->guarded([&]{
                if(state->recovery_operation){
                    require(!state->recovery_attempted);state->recovery_attempted=true;
                    state->store.persist_recovery(internal,persist,*state->recovery_operation);
                }
                else if(state->store.initialized())state->store.persist(internal,persist);
                else {require(state->phase==State::Phase::Bootstrapping);state->store.initialize_persisted(internal,persist);}
            });
        },
        [state](const auto &internal){
            state->source(internal);
            if(!state->store.initialized()){require(state->phase==State::Phase::Bootstrapping);state->store.initialize(internal);}
            state->store.validate_snapshot(internal);state->validated=true;
        },
        [state](const auto &internal,const auto &bytes){
            state->source(internal);state->guarded([&]{
                if(state->recovery_operation){
                    require(!state->recovery_attempted);state->recovery_attempted=true;
                    state->store.persist_recovery(internal,[]{},*state->recovery_operation,bytes);
                }
                else if(state->store.initialized())state->store.persist_stock(internal,bytes);
                else {require(state->phase==State::Phase::Bootstrapping);state->store.initialize_stock(internal,bytes);}
            });
        },
        [state](const ConfigIniDraftSnapshot &draft){require(draft.filename==state->ini);state->draft=draft;},
        [state](const std::string &filename){
            require(filename==state->ini&&(state->phase==State::Phase::Bootstrapping||state->phase==State::Phase::Ready));
            (void)state->store.recovery_revision();
        },
        [state](const auto &internal,const std::string &before,const std::string &after){
            state->source(internal);require(state->phase==State::Phase::Ready);
            // The parent proves that only the private network-safety value
            // changes. It neither reserves RF nor applies transmitter settings.
            state->store.persist_private(internal,before,after);
        });
    // Recover committed images before the component or application reads INI.
    state->store.validate_initial_source();
    const auto recurrence=prepare_pi_recurrence_storage(state->self,state->ini);
    state->store.bind_recurrence_storage(recurrence.path,true);
    state->store.recover();
    state->store.prepare_installation(recurrence.prior_output_history);
    set_legacy_recurrence_admission([state]{
        if(!state->validated||(state->phase!=State::Phase::Bootstrapping&&state->phase!=State::Phase::Ready))return LegacyRecurrenceAdmission::Held;
        const auto authority=state->store.recurrence_authority();
        if(authority["state"]=="native")return LegacyRecurrenceAdmission::Native;
        if(authority["state"]=="unclassified"||authority["state"]=="legacy"||authority["state"]=="rollback")
            return state->store.settings()["scheduling"]["enabled"]==true?LegacyRecurrenceAdmission::Open:LegacyRecurrenceAdmission::DrainAcceptedPair;
        return authority["state"]=="retiring"?LegacyRecurrenceAdmission::DrainAcceptedPair:LegacyRecurrenceAdmission::Held;
    });
    state->phase=State::Phase::Bootstrapping;
    set_native_solar_snapshot_provider([state]{
        require(state->validated&&(state->phase==State::Phase::Bootstrapping||state->phase==State::Phase::Ready));
        const auto saved=state->store.settings();NativeSolarSnapshot result;
        result.ordinary=config;result.policy=parse_solar_configuration(saved.at("scheduling"),result.ordinary);
        const auto text=saved.at("configuration_generation").get<std::string>();
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result.generation);
        require(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&result.generation>0);
        const auto authority=state->store.recurrence_authority();result.native=authority["state"]=="native";
        result.device_id=state->self;
        const auto generation=authority["generation"].get<std::string>();
        const auto decoded=std::from_chars(generation.data(),generation.data()+generation.size(),result.authority_generation);
        require(decoded.ec==std::errc{}&&decoded.ptr==generation.data()+generation.size()&&result.authority_generation);
        if(result.native)result.progress=state->store.recurrence_progress();
        auto intent=saved["scheduling"];intent["enabled"]=false;
        result.intent_revision=PiManagementJournal::digest(canonical_pi_management_json(nlohmann::json{
            {"configuration",saved["configuration"]},{"scheduling",intent}}));
        result.revision=saved.at("revision").get<std::string>();return result;
    });
}
void PiManagedStartup::activate(ConfigurationGate gate){
    std::lock_guard lock(config_update_mutex());
    require(installed_owner==state_.get()&&state_->phase==State::Phase::Bootstrapping&&state_->validated&&bool(gate));
    (void)state_->store.settings();
    state_->gate=std::move(gate);state_->phase=State::Phase::Ready;
}
bool PiManagedStartup::admission_ready() noexcept {return state_->ready();}
const std::string &PiManagedStartup::device_id() const noexcept {return state_->self;}
bool PiManagedStartup::configuration_validated() const {
    std::lock_guard lock(config_update_mutex());return state_->validated;
}
std::optional<ConfigIniDraftSnapshot> PiManagedStartup::draft() const {
    std::lock_guard lock(config_update_mutex());
    if(!state_->draft||!config_ini_draft_is_current(*state_->draft))return {};
    return state_->draft;
}
nlohmann::json PiManagedStartup::execute_recovery(const PiManagedStore::Operation &operation,
    const std::function<void()> &validated_mutation){
    std::unique_lock tone(test_tone_lifecycle_mutex(),std::try_to_lock);
    if(!tone.owns_lock())throw std::runtime_error("management_busy");
    std::lock_guard lock(config_update_mutex());
    require(installed_owner==state_.get()&&bool(validated_mutation)&&!state_->recovery_operation);
    require(operation.route=="/api/control/v1/host/configuration-recovery");
    (void)pi_managed_request_digest(state_->self,operation.principal,operation.authorization_generation,operation.route,operation.request);
    if(state_->store.initialized())if(const auto receipt=state_->store.replay(operation))return *receipt;
    require(state_->phase==State::Phase::Bootstrapping&&!state_->validated);
    if(operation.request["expected_revision"]!=state_->store.recovery_revision())throw std::runtime_error("revision_conflict");
    state_->recovery_operation=operation;state_->recovery_attempted=false;
    struct Clear{std::shared_ptr<State> state;~Clear(){state->recovery_operation.reset();state->recovery_attempted=false;}} clear{state_};
    validated_mutation();
    require(state_->recovery_attempted);
    const auto receipt=state_->store.replay(operation);require(receipt.has_value());return *receipt;
}
PiManagedStore &PiManagedStartup::store(){return state_->store;}
WtpCatalog &PiManagedStartup::catalog(){return state_->catalog;}
} // namespace wsprrypi
