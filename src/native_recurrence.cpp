// SPDX-License-Identifier: MIT
#include "native_recurrence.hpp"
#include "application_storage/sqlite_state_store.hpp"
#include "runtime_config_bridge.hpp"
#include "config_handler.hpp"
#include "scheduling.hpp"
#include "scheduling_internal.hpp"
#include "transmitter_runtime_bridge.hpp"
#include "wtp_runtime_bridge.hpp"
#include "wtp_pi_control.hpp"
#include "recurrence_admission.hpp"
#include "runtime_config_operations.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "wtp_endpoint/identity.hpp"
#include <cmath>
#include <mutex>
#include <string_view>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
constexpr std::array names{"ordinary","day","night","morning","evening"};
std::mutex state_mutex;
std::shared_ptr<NativeOccurrence> active;
std::shared_ptr<NativeOccurrence> pending_skip;
std::shared_ptr<NativeOccurrence> pending_refusal;
Json observation;
std::string observation_self;
std::chrono::steady_clock::time_point observed_at{},last_poll{};
SolarClockProvider clock_provider;
std::function<SolarClockObservation(bool)> clock_reader;
std::function<std::uint64_t()> wspr_start;
std::string forecast_key;
std::optional<std::uint64_t> forecast_start;
SolarClockObservation clock(bool fresh){return clock_reader?clock_reader(fresh):clock_provider.observe(fresh);}
std::string decimal_hz(std::uint64_t value){
    auto text=std::to_string(value/1'000'000'000ULL);const auto remainder=value%1'000'000'000ULL;
    if(remainder){auto fraction=std::to_string(1'000'000'000ULL+remainder).substr(1);while(fraction.back()=='0')fraction.pop_back();text+='.'+fraction;}return text;
}
SolarModeIntent mode_intent(const ArgParserConfig &cfg){
    SolarModeIntent intent;intent.mode=cfg.mode;intent.wspr={cfg.frequencies,cfg.wspr.frequency_profile,cfg.wspr.band_preferences,cfg.wspr_frequency_entries};
    intent.wspr_audio_offset_hz=cfg.wspr.audio_offset_hz;
    const auto nano=[](double hz)->std::uint64_t{return solar_frequency_nhz(hz).value_or(0);};
    intent.cw_base_nhz=nano(cfg.mode==ModeType::QRSS?cfg.qrss.frequency_hz:cfg.mode==ModeType::FSKCW?cfg.fskcw.space_frequency_hz:cfg.dfcw.dot_frequency_hz);
    const auto shift=std::round(static_cast<long double>(cfg.modulation_fsk_offset_hz)*1e9L);
    if(std::isfinite(shift)&&shift>=0&&shift<9223372036854775808.0L)intent.cw_shift_nhz=static_cast<std::int64_t>(shift);
    return intent;
}
void lists(const NativeSolarSnapshot &snapshot,RecurrenceListDigests &digests,std::array<std::uint32_t,5> &sizes){
    const SolarWsprList ordinary{snapshot.ordinary.frequencies,snapshot.ordinary.wspr.frequency_profile,snapshot.ordinary.wspr.band_preferences,snapshot.ordinary.wspr_frequency_entries};
    const std::array<const SolarWsprList *,5> all{&ordinary,snapshot.policy.day.wspr_override?&*snapshot.policy.day.wspr_override:nullptr,
        snapshot.policy.night.wspr_override?&*snapshot.policy.night.wspr_override:nullptr,snapshot.policy.morning.wspr_override?&*snapshot.policy.morning.wspr_override:nullptr,
        snapshot.policy.evening.wspr_override?&*snapshot.policy.evening.wspr_override:nullptr};
    for(std::size_t i=0;i<all.size();++i){
        Json encoded=nullptr;
        if(const auto *list=all[i]){
            if(list->entries.size()>UINT32_MAX)throw std::runtime_error("recurrence_list_capacity");
            sizes[i]=static_cast<std::uint32_t>(list->entries.size());
            encoded=Json::array({list->frequencies,list->profile});
            Json preferences=Json::object();for(const auto &[name,value]:list->preferences)std::visit([&](const auto &item){preferences[name]=item;},value);
            encoded.push_back(preferences);
            for(const auto &entry:list->entries)encoded.push_back(Json::array({entry.token,entry.dial_frequency_hz,entry.selector_gpio,entry.selector_gpio_active_high,entry.allow_band_gpio_fallback}));
        }
        digests[i]=PiManagementJournal::digest(encoded.dump());
    }
}
const char *waiting(SolarWait reason){
    switch(reason){case SolarWait::None:return "none";case SolarWait::Clock:case SolarWait::Date:return "clock";
        case SolarWait::Location:case SolarWait::UnsupportedLocation:return "location";case SolarWait::Period:return "solar";
        case SolarWait::Frequency:return "frequency";default:return "recovery";}
}
void publish(const std::shared_ptr<NativeOccurrence> &plan,const char *phase,const char *reason,std::optional<std::uint64_t> forecast={}){
    const auto &result=plan->evaluation;Json frequencies=Json::array();
    if(result.intent)for(auto value:plan->realized_tones.empty()?result.intent->nominal_tones_nhz:plan->realized_tones)frequencies.push_back(decimal_hz(value));
    const auto &c=result.observation.clock;
    const bool ready=solar_clock_ready(c);
    const auto uncertainty=ready?c.maximum_error_ns+c.holdover_age_ns/20'000ULL+(c.holdover_age_ns%20'000ULL!=0):0;
    Json next=nullptr;if(!plan->resolved&&std::string_view(phase)!="fault"&&std::string_view(reason)!="recovery"&&
        (result.outcome==SolarOutcome::Allowed||result.outcome==SolarOutcome::Skipped)&&!plan->starts.empty())next=std::to_string(plan->starts.front());
    if(forecast)next=std::to_string(*forecast);
    std::lock_guard lock(state_mutex);
    observation={{"active_revision",plan->accepted?Json(plan->snapshot.revision):Json(nullptr)},{"phase",phase},{"waiting_reason",reason},
        {"clock",{{"state",ready?(c.state==SolarClockState::Synchronized?"synchronized":"holdover"):c.state==SolarClockState::Unavailable?"unknown":"unsynchronized"},
            {"uncertainty_ns",ready?Json(std::to_string(uncertainty)):Json(nullptr)},{"holdover_age_ns",ready?Json(std::to_string(c.holdover_age_ns)):Json(nullptr)},
            {"generation",std::to_string(c.generation)}}},{"selection",result.selected_period?names[static_cast<std::size_t>(*result.selected_period)+1]:(result.intent?"ordinary":"unknown")},
        {"selected_frequencies_hz",frequencies},{"next_start_utc_ns",next},{"forecast",next.is_null()?"unknown":"known"},
        {"prolonged_window",result.position?Json(result.prolonged):Json(nullptr)}};
    observed_at=std::chrono::steady_clock::now();
    observation_self=plan->snapshot.device_id;
}
bool current(const std::shared_ptr<NativeOccurrence> &plan,bool accepting){
    if(!plan||plan->resolved||exiting_wspr.load()||web_test_tone.load())return false;
    if(ini_reload_generation.load()!=plan->reload_generation){
        // The monitor can report our already applied atomic save after this
        // occurrence was accepted. Only the identical protected file is safe;
        // a later replacement, even with restored contents, invalidates it.
        if(!plan->snapshot.ordinary.use_ini||plan->ini_signature.empty()||
            configuration_input_signature(plan->snapshot.ordinary.ini_filename)!=plan->ini_signature)return false;
    }
    if(legacy_recurrence_admission()!=LegacyRecurrenceAdmission::Native||!wtp_pi_management_ready())return false;
    const auto snapshot=current_native_solar_snapshot();
    if(!snapshot.native||!snapshot.progress||snapshot.progress->held()||snapshot.authority_generation!=plan->snapshot.authority_generation||snapshot.intent_revision!=plan->snapshot.intent_revision)return false;
    if(accepting&&!snapshot.policy.schedule_enabled)return false;
    if(accepting){
        const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if(now<0||plan->starts.empty()||static_cast<std::uint64_t>(now)>=plan->starts.front())return false;
    }
    if(!snapshot.ordinary.transmit||!runtime_transmit_preparation_enabled(snapshot.ordinary)||
        (snapshot.ordinary.transmit_backend!=TransmitBackendKind::WTP&&!wtp_pi_local_effective()))return false;
    if(snapshot.policy.solar_enabled){const auto now=clock(true);if(!solar_clock_ready(now)||now.generation!=plan->evaluation.observation.clock.generation)return false;}
    return solar_backend_frequencies(snapshot.ordinary,snapshot.ordinary.mode,plan->realized_tones.empty()?plan->evaluation.intent->nominal_tones_nhz:plan->realized_tones);
}
}
std::shared_ptr<NativeOccurrence> prepare_native_occurrence(const ArgParserConfig &cfg,std::span<const std::uint64_t> starts){
    auto plan=std::make_shared<NativeOccurrence>();
    try{
    plan->snapshot=current_native_solar_snapshot();
    if(!plan->snapshot.native||!plan->snapshot.progress)throw std::runtime_error("native_authority_required");
    plan->snapshot.ordinary=cfg;plan->starts.assign(starts.begin(),starts.end());plan->id=WtpPiBootIdentity{}.new_boot_id();plan->reload_generation=ini_reload_generation.load();
    if(cfg.use_ini)plan->ini_signature=configuration_input_signature(cfg.ini_filename);
    lists(plan->snapshot,plan->digests,plan->sizes);
    const auto progress=plan->snapshot.progress->snapshot();std::array<std::size_t,5> cursors{};
    for(std::size_t i=0;i<names.size();++i){
        const auto &row=progress["cursors"][names[i]];
        if(row["list_digest"]==plan->digests[i]){
            cursors[i]=row["index"].get<std::uint32_t>();
            if((!plan->sizes[i]&&cursors[i])||(plan->sizes[i]&&cursors[i]>=plan->sizes[i]))throw std::runtime_error("recurrence_cursor_hold");
        }
    }
    if(!progress["in_flight"].is_null()||starts.empty()||starts.front()<=std::stoull(progress["consumed_through_utc_ns"].get<std::string>())){
        publish(plan,"waiting",progress["in_flight"].is_null()?"clock":"recovery");return plan;
    }
    const auto observed=clock(false);
    SolarEvaluationContext context{starts.front(),plan->snapshot.generation,plan->snapshot.generation,observed,SolarLocationSource::Locator};
    const auto mode=mode_intent(cfg);
    const auto allowed=[&](auto selected_mode,auto tones){return solar_backend_frequencies(cfg,selected_mode,tones);};
    const auto first=evaluate_solar_policy(plan->snapshot.policy,mode,context,cursors,allowed);
    if(first.outcome==SolarOutcome::Skipped){plan->starts.resize(1);plan->evaluation=first;}
    else plan->evaluation=evaluate_solar_occurrence(plan->snapshot.policy,mode,context,starts,cursors,allowed);
    std::optional<std::uint64_t> forecast;
    if(plan->snapshot.policy.solar_enabled&&plan->evaluation.outcome==SolarOutcome::Held&&
        (plan->evaluation.waiting==SolarWait::Period||plan->evaluation.waiting==SolarWait::Frequency)){
        const auto key=plan->snapshot.device_id+plan->snapshot.intent_revision+std::to_string(starts.front())+":"+std::to_string(observed.generation);
        bool cached=false;{std::lock_guard lock(state_mutex);if(forecast_key==key){forecast=forecast_start;cached=true;}}
        if(!cached){
            constexpr std::uint64_t horizon=48ULL*3600ULL*1'000'000'000ULL;
            auto candidates=plan->starts;
            if(cfg.mode==ModeType::WSPR||cfg.schedule_repeat_minutes>0)for(unsigned step=0;step<4096;++step){
                if(cfg.mode==ModeType::WSPR){
                    if(candidates.back()>UINT64_MAX-120'000'000'000ULL)break;
                    for(auto &start:candidates)start+=120'000'000'000ULL;
                }else{
                    const auto previous=candidates.front();
                    const auto when=std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(previous)));
                    const auto next=std::chrono::duration_cast<std::chrono::nanoseconds>(next_non_wspr_schedule_time_for_test(cfg,when).time_since_epoch()).count();
                    if(next<=0||static_cast<std::uint64_t>(next)<=previous)break;
                    candidates.front()=static_cast<std::uint64_t>(next);
                }
                if(candidates.front()-plan->starts.front()>horizon)break;
                const auto predicted=evaluate_solar_occurrence(plan->snapshot.policy,mode,context,candidates,cursors,allowed);
                if(predicted.outcome==SolarOutcome::Allowed||predicted.outcome==SolarOutcome::Skipped){forecast=candidates.front();break;}
            }
            {std::lock_guard lock(state_mutex);forecast_key=key;forecast_start=forecast;}
        }
    }
    publish(plan,"waiting",plan->evaluation.outcome==SolarOutcome::Disabled?"disabled":waiting(plan->evaluation.waiting),forecast);return plan;
    }catch(...){publish(plan,"fault","storage");return plan;}
}
bool reserve_native_occurrence(const std::shared_ptr<NativeOccurrence> &plan,const Json &intent,std::span<const std::uint64_t> realized){
    std::lock_guard lock(config_update_mutex());
    try{
        if(!plan||!plan->evaluation.intent||(plan->evaluation.outcome!=SolarOutcome::Allowed&&plan->evaluation.outcome!=SolarOutcome::Skipped))return false;
        plan->realized_tones.assign(realized.begin(),realized.end());
        if(plan->evaluation.outcome!=SolarOutcome::Skipped&&!current(plan,true)){publish(plan,"waiting","recovery");return false;}
        if(plan->evaluation.outcome==SolarOutcome::Skipped){
            const auto now=current_native_solar_snapshot();
            if(!now.native||!now.policy.schedule_enabled||now.intent_revision!=plan->snapshot.intent_revision||now.generation!=plan->snapshot.generation)return false;
            if(!now.ordinary.transmit||!wtp_pi_management_ready()||!runtime_transmit_preparation_enabled(now.ordinary)||
                (now.ordinary.transmit_backend!=TransmitBackendKind::WTP&&!wtp_pi_local_effective()))return false;
            if(now.policy.solar_enabled){const auto sampled=clock(true);if(!solar_clock_ready(sampled)||sampled.generation!=plan->evaluation.observation.clock.generation)return false;}
            const auto utc=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            if(utc<0||static_cast<std::uint64_t>(utc)<plan->starts.front()||static_cast<std::uint64_t>(utc)-plan->starts.front()>1'500'000'000ULL)return false;
        }
        plan->snapshot.progress->reserve(plan->id,plan->starts.front(),plan->starts.back(),PiManagementJournal::digest(intent.dump()),plan->snapshot.authority_generation,
            plan->snapshot.generation,plan->evaluation.observation.clock.generation,plan->digests,plan->sizes,
            static_cast<std::size_t>(plan->evaluation.intent->cursor_scope),plan->evaluation.outcome==SolarOutcome::Skipped);
        if(plan->evaluation.outcome==SolarOutcome::Skipped){plan->resolved=true;publish(plan,"waiting","none");return true;}
        plan->reserved=true;publish(plan,"prepared","none");return true;
    }catch(...){if(plan)publish(plan,"fault","storage");return false;}
}
bool accept_native_occurrence(const std::shared_ptr<NativeOccurrence> &plan,const std::function<void()> &accept){
    std::lock_guard lock(config_update_mutex());
    if(!plan||!plan->reserved||!current(plan,true)){refuse_native_occurrence(plan);return false;}
    try{
        accept();
        if(!current(plan,true)){transmitter_stop_and_join();refuse_native_occurrence(plan);return false;}
        plan->accepted=true;{std::lock_guard state(state_mutex);active=plan;}publish(plan,"accepted","none");return true;
    }
    catch(...){refuse_native_occurrence(plan);throw;}
}
void refuse_native_occurrence(const std::shared_ptr<NativeOccurrence> &plan) noexcept {
    std::lock_guard lock(config_update_mutex());
    if(!plan||!plan->reserved||plan->resolved)return;
    {std::lock_guard state(state_mutex);if(active==plan)active.reset();}
    if(!transmitter_output_inactive_confirmed()){
        {std::lock_guard state(state_mutex);pending_refusal=plan;}
        if(!wtp_runtime_selected())wtp_pi_end_scheduled(false);
        publish(plan,"fault","recovery");return;
    }
    try{plan->snapshot.progress->resolve(false,plan->digests);plan->resolved=true;publish(plan,"waiting","recovery");}
    catch(...){publish(plan,"fault","storage");}
}
void finish_native_occurrence(bool reconciled) noexcept {
    std::lock_guard configuration(config_update_mutex());
    const auto plan=active_native_occurrence();if(!plan||plan->resolved)return;
    try{
        if(!reconciled){plan->terminal_pending=true;publish(plan,"fault","recovery");return;}
        const auto snapshot=current_native_solar_snapshot();RecurrenceListDigests digests;std::array<std::uint32_t,5> sizes{};lists(snapshot,digests,sizes);
        plan->snapshot.progress->resolve(true,digests);plan->resolved=true;
        {std::lock_guard lock(state_mutex);if(active==plan)active.reset();}publish(plan,"idle","none");
    }catch(...){plan->terminal_pending=true;publish(plan,"fault","storage");}
}
std::shared_ptr<NativeOccurrence> active_native_occurrence(){std::lock_guard lock(state_mutex);return active;}
bool native_occurrence_active() noexcept {return bool(active_native_occurrence());}
bool native_launch_current(const std::shared_ptr<NativeOccurrence> &plan) noexcept {
    try{SqliteExecutionReadScope committed_state;
        std::unique_lock lock(config_update_mutex(),std::try_to_lock);return lock.owns_lock()&&plan&&plan->accepted&&current(plan,false);}catch(...){return false;}
}
TransmitterRuntimeCallback native_execution_callback(const std::shared_ptr<NativeOccurrence> &plan){
    const auto generation=++plan->execution_generation;const auto callback=transmitter_callbacks();
    return [plan,generation,callback](auto event,auto level,const auto &message,double value){
        if(callback&&plan->accepted&&!plan->resolved&&plan->execution_generation==generation&&active_native_occurrence()==plan)
            callback(event,level,message,value);
    };
}
Json native_recurrence_observation(std::string_view self){
    std::lock_guard lock(state_mutex);if(observation.is_null()||(!self.empty()&&self!=observation_self))return {};
    auto result=observation;const auto age=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-observed_at).count();
    const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if(!result["next_start_utc_ns"].is_null()&&now>=0&&std::stoull(result["next_start_utc_ns"].get<std::string>())<static_cast<std::uint64_t>(now)){
        result["next_start_utc_ns"]=nullptr;result["forecast"]="unknown";
        if(active)for(auto start:active->starts)if(start>static_cast<std::uint64_t>(now)){
            result["next_start_utc_ns"]=std::to_string(start);result["forecast"]="known";break;
        }
    }
    result["observation_age_ms"]=static_cast<std::uint32_t>(std::min<std::int64_t>(std::max<std::int64_t>(age,0),UINT32_MAX));return result;
}
void poll_native_recurrence(){
    if(legacy_recurrence_admission()!=LegacyRecurrenceAdmission::Native)return;
    const auto now=std::chrono::steady_clock::now();if(now-last_poll<std::chrono::seconds(1))return;last_poll=now;
    std::unique_lock lock(test_tone_lifecycle_mutex(),std::try_to_lock);if(!lock.owns_lock()||web_test_tone.load()||exiting_wspr.load())return;
    if(const auto plan=active_native_occurrence()){
        if(plan->terminal_pending){
            const auto authority=wtp_pi_management_observation();
            if(!plan->snapshot.progress->held()&&transmitter_output_inactive_confirmed()&&authority&&
                !authority->authority.output_unknown&&!authority->authority.stop_reserved&&
                !authority->authority.remote.output_active&&!authority->authority.remote.owner_id&&
                !authority->authority.local_work_active&&!authority->authority.revocation_unavailable)
                finish_native_occurrence(true);
            return;
        }
        if(plan->snapshot.policy.solar_enabled){
            const auto sampled=clock(false);
            if((!solar_clock_ready(sampled)||sampled.generation!=plan->evaluation.observation.clock.generation)&&transmitter_state()!=WsprTransmitState::TRANSMITTING)
                transmitter_stop_and_join();
        }
        return;
    }
    try{
        const auto snapshot=current_native_solar_snapshot();const auto progress=snapshot.progress->snapshot();
        if(!progress["in_flight"].is_null()){
            if(!transmitter_output_inactive_confirmed())return;
            const auto authority=wtp_pi_management_observation();
            if(!authority||authority->authority.output_unknown||authority->authority.stop_reserved||authority->authority.remote.output_active||
                authority->authority.remote.owner_id||authority->authority.local_work_active||authority->authority.revocation_unavailable)return;
            RecurrenceListDigests digests;std::array<std::uint32_t,5> sizes{};lists(snapshot,digests,sizes);
            std::shared_ptr<NativeOccurrence> refusal;{std::lock_guard state(state_mutex);refusal=pending_refusal;}
            const bool known_refusal=refusal&&progress["in_flight"]["occurrence_id"]==refusal->id;
            snapshot.progress->resolve(!known_refusal,digests); // positive reconciliation; unknown acceptance consumes conservatively
            if(known_refusal){refusal->resolved=true;std::lock_guard state(state_mutex);pending_refusal.reset();}
        }
        // Accepted work and uncertain output were handled above. Disabled
        // recurrence must not repeatedly enter ordinary backend preparation or
        // emit its disabled-transmission log on every health poll.
        if(!snapshot.policy.schedule_enabled||!snapshot.ordinary.transmit)return;
    }catch(...){return;}
    std::shared_ptr<NativeOccurrence> skip;{std::lock_guard state(state_mutex);skip=pending_skip;}
    if(skip){
        const auto utc=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if(utc>=0&&static_cast<std::uint64_t>(utc)<skip->starts.front())return;
        {std::lock_guard state(state_mutex);pending_skip.reset();}
        if(utc>=0&&static_cast<std::uint64_t>(utc)-skip->starts.front()<=1'500'000'000ULL)
            (void)reserve_native_occurrence(skip,Json{{"skip",true},{"revision",skip->snapshot.revision}});
    }
    try{
        if(is_non_wspr_runtime_mode(config.mode)&&!ini_reload_pending.load())schedule_next_non_wspr_launch(config);
        else (void)set_config(false);
    }catch(...){}
}
void defer_native_skip(const std::shared_ptr<NativeOccurrence> &plan){std::lock_guard lock(state_mutex);pending_skip=plan;}
void reset_native_recurrence_observation() noexcept{std::lock_guard lock(state_mutex);active.reset();pending_skip.reset();pending_refusal.reset();observation=nullptr;observation_self.clear();last_poll={};forecast_key.clear();forecast_start.reset();}
std::uint64_t next_native_wspr_start(const ArgParserConfig &cfg){
    if(wspr_start)return wspr_start();
    const auto now=std::chrono::system_clock::now();
    const auto lead=cfg.transmit_backend==TransmitBackendKind::WTP?wtp_runtime_preparation_lead():std::chrono::seconds(2);
    const auto earliest=std::chrono::duration_cast<std::chrono::nanoseconds>((now+lead).time_since_epoch()).count();
    if(earliest<=0||static_cast<std::uint64_t>(earliest)>UINT64_MAX-120'000'000'000ULL)throw std::runtime_error("recurrence_time_hold");
    return ((static_cast<std::uint64_t>(earliest)-1'000'000'000ULL+119'999'999'999ULL)/120'000'000'000ULL)*120'000'000'000ULL+1'000'000'000ULL;
}
namespace testing {void set_native_clock_reader(std::function<SolarClockObservation(bool)> reader){std::lock_guard lock(config_update_mutex());clock_reader=std::move(reader);}}
namespace testing {void set_native_wspr_start(std::function<std::uint64_t()> planner){std::lock_guard lock(config_update_mutex());wspr_start=std::move(planner);}}
}
