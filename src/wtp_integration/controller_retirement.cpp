// SPDX-License-Identifier: MIT
#include "controller_retirement.hpp"
#include "controller_contract.hpp"
#include "application.hpp"
#include "owned_state_directory.hpp"
#include "browser_api.hpp"
#include "catalog.hpp"
#include "fleet_store.hpp"
#include "wtp_endpoint/identity.hpp"
#include <filesystem>
#include <set>
#include <cerrno>
#include <sys/stat.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
Json private_record(const std::string &bytes){
    if(bytes.size()>1048576)throw std::runtime_error("controller_retirement_hold");
    std::vector<std::set<std::string>> objects;
    return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json &value){
        if(depth<0||depth>32)throw std::runtime_error("controller_retirement_hold");
        if(event==Json::parse_event_t::object_start)objects.emplace_back();
        if(event==Json::parse_event_t::key&&!objects.back().insert(value.get<std::string>()).second)throw std::runtime_error("controller_retirement_hold");
        if(event==Json::parse_event_t::object_end)objects.pop_back();
        return true;
    });
}
void conflict(bool value){if(!value)throw ControllerRetirementConflict{};}
void require(bool value){if(!value)throw std::runtime_error("controller_retirement_hold");}
bool fields(const Json &j,std::initializer_list<const char*> names){if(!j.is_object()||j.size()!=names.size())return false;for(auto name:names)if(!j.contains(name))return false;return true;}
bool hex(const Json &j,std::size_t n){return j.is_string()&&j.get_ref<const std::string&>().size()==n&&j.get_ref<const std::string&>().find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string assignment_revision(Json row){row.erase("in_flight");row.erase("last_start_ns");row.erase("enabled");return PiManagementJournal::digest(row.dump());}
Json *find(Json &rows,const std::string &id){for(auto &row:rows)if(row.at("target_id")==id)return &row;return nullptr;}
Json sealed(Json value){value.erase("checksum");value["checksum"]=PiManagementJournal::digest(value.dump());return value;}
Json anchor_record(const Json &book,const std::string &digest) {
    return sealed(Json{{"schema","controller-retirement-anchor/1"},{"controller_id",book.at("controller_id")},
        {"source_binding",book.at("source_binding")},{"installation_id",book.at("installation_id")},
        {"generation",book.at("generation")},{"authority_digest",digest}});
}
const Json *assignment(const Json &rows,const std::string &id){for(const auto &row:rows)if(row.at("device_id")==id)return &row;return nullptr;}
std::string next(const Json &value){const auto n=wtp_fleet_decimal(value);require(n<UINT64_MAX);return std::to_string(n+1);}
}
ControllerRetirementBook::ControllerRetirementBook(const std::string &directory,const std::string &source,
    const std::string &self,const Json &assignments,std::function<void(RetirementWritePoint)> checkpoint):self_(self),source_(PiManagementJournal::digest(std::filesystem::weakly_canonical(source).string())),checkpoint_(std::move(checkpoint)) {
    struct stat marker{},owner{};
    const bool anchor_present=!::lstat((directory+".established").c_str(),&marker);
    require(anchor_present||errno==ENOENT);
    if(anchor_present)require(!::lstat(directory.c_str(),&owner));
    const auto initial=[&]{Json rows=Json::array();for(const auto &row:assignments)rows.push_back({{"target_id",row.at("device_id")},
        {"assignment_id",wtp_random_identity()},{"state","legacy"},{"frozen",nullptr},{"intent",nullptr},{"receipt",nullptr},{"observation",nullptr},{"completion",nullptr},{"request_revision",nullptr}});
        return sealed(Json{{"schema","controller-retirement-book/1"},{"controller_id",self_},{"source_binding",source_},{"generation","1"},{"installation_id",wtp_random_identity()},{"established",false},{"assignments",rows}}).dump();};
    const auto location=prepare_owned_state_directory(directory,initial,[&](const std::string &bytes){validate(private_record(bytes));});
    // Validate the immutable source binding before interpreting any retained WAL.
    validate(private_record(PiManagementJournal::read_ini_input(location.file).bytes));
    journal_=std::make_unique<PiManagementJournal>(directory+"/transactions",self_,std::map<std::string,std::string>{{"recurrence",location.file},{"retirement-anchor",std::filesystem::path(location.file).parent_path().string()+".established"}});
    journal_->recover();const auto observed=journal_->inspect("recurrence");require(bool(observed));
    data_=private_record(observed->bytes);validate(data_);revision_=PiManagementJournal::digest(observed->bytes);
    anchor_bytes_=anchor_record(data_,revision_).dump();
    auto anchor=journal_->inspect("retirement-anchor");
    if(data_.at("established")==false) {
        require(!anchor||anchor->bytes==anchor_record(data_,revision_).dump());
        auto established=data_;established["established"]=true;established=sealed(std::move(established));validate(established);
        const auto image=established.dump();
        anchor_bytes_=anchor_record(established,PiManagementJournal::digest(image)).dump();
        journal_->prepare({{"recurrence",revision_,image},{"retirement-anchor",anchor?std::optional<std::string>(PiManagementJournal::digest(anchor->bytes)):std::nullopt,anchor_bytes_}});
        if(checkpoint_)checkpoint_(RetirementWritePoint::EstablishmentPrepared);
        journal_->commit();if(checkpoint_)checkpoint_(RetirementWritePoint::EstablishmentCommitted);
        journal_->recover();if(checkpoint_)checkpoint_(RetirementWritePoint::EstablishmentPublished);data_=std::move(established);revision_=PiManagementJournal::digest(image);
        anchor=journal_->inspect("retirement-anchor");
    }
    require(anchor&&anchor->bytes==anchor_bytes_);
    for(const auto &row:assignments)require(find(data_["assignments"],row.at("device_id"))!=nullptr);
}
void ControllerRetirementBook::validate(const Json &j) const {
    require(fields(j,{"schema","controller_id","source_binding","generation","assignments","checksum","installation_id","established"})&&j.at("schema")=="controller-retirement-book/1"&&
        j.at("controller_id")==self_&&j.at("source_binding")==source_&&hex(j.at("installation_id"),32)&&j.at("established").is_boolean()&&j.at("assignments").is_array()&&j.at("assignments").size()<=32&&wtp_fleet_decimal(j.at("generation"))>0);
    require(hex(j.at("checksum"),64)&&sealed(j)==j);
    if(j.at("established")==false){require(j.at("generation")=="1");for(const auto &row:j.at("assignments"))require(row.at("state")=="legacy");}
    std::set<std::string> targets,ids,operations;
    for(const auto &row:j.at("assignments")) {
        require(fields(row,{"target_id","assignment_id","state","frozen","intent","receipt","observation","completion","request_revision"})&&hex(row.at("target_id"),32)&&hex(row.at("assignment_id"),32)&&
            targets.insert(row.at("target_id")).second&&ids.insert(row.at("assignment_id")).second);
        const bool legacy=row.at("state")=="legacy";
        require(legacy||row.at("state")=="retiring"||row.at("state")=="retired");
        if(legacy){require(row.at("frozen").is_null()&&row.at("intent").is_null()&&row.at("receipt").is_null()&&row.at("observation").is_null()&&row.at("completion").is_null()&&row.at("request_revision").is_null());continue;}
        for(const auto *key:{"observation","completion"})if(!row.at(key).is_null()) {
            const auto &observation=row.at(key);
            require(fields(observation,{"device_id","boot_id","owner_id","job_id","output_active","state","submitted_job_id","session_id","controller_owner_id"})&&
                observation.at("device_id")==row.at("target_id")&&hex(observation.at("boot_id"),32)&&
                (observation.at("owner_id").is_null()||hex(observation.at("owner_id"),32))&&
                (observation.at("job_id").is_null()||hex(observation.at("job_id"),32))&&observation.at("output_active").is_boolean()&&
                (observation.at("submitted_job_id").is_null()||hex(observation.at("submitted_job_id"),32))&&
                (observation.at("session_id").is_null()||hex(observation.at("session_id"),32))&&
                (observation.at("controller_owner_id").is_null()||hex(observation.at("controller_owner_id"),32))&&
                (observation.at("state")=="empty"||observation.at("state")=="loaded"||observation.at("state")=="armed"||observation.at("state")=="running"||
                 observation.at("state")=="complete"||observation.at("state")=="aborted"||observation.at("state")=="missed"||observation.at("state")=="failed"));
        }
        if(row.at("state")=="retired") {
            const auto &proof=row.at("completion");require(!proof.is_null()&&proof.at("owner_id").is_null()&&proof.at("output_active")==false&&
                (proof.at("state")=="empty"||proof.at("state")=="complete"||proof.at("state")=="aborted"||proof.at("state")=="missed"));
        }else require(row.at("completion").is_null());
        require(hex(row.at("request_revision"),64));
        const auto &intent=row.at("intent"),&receipt=row.at("receipt");
        require(valid_controller_document(intent,"controller-retirement")&&valid_controller_document(receipt,"controller-retirement-operation")&&
            intent.at("controller_id")==self_&&intent.at("target_id")==row.at("target_id")&&intent.at("assignment_id")==row.at("assignment_id")&&
            receipt.at("controller_id")==self_&&receipt.at("target_id")==row.at("target_id")&&receipt.at("assignment_id")==row.at("assignment_id")&&
            receipt.at("operation_id")==intent.at("operation_id")&&receipt.at("transition_id")==intent.at("transition_id")&&
            receipt.at("state")==row.at("state")&&operations.insert(intent.at("operation_id")).second);
        WtpFleetStore::validate({{"version",1},{"assignments",Json::array({row.at("frozen")})},{"removals",Json::array()}},self_);
        require(row.at("frozen").at("device_id")==row.at("target_id")&&assignment_revision(row.at("frozen"))==intent.at("expected_assignment_revision").get<std::string>());
    }
}
void ControllerRetirementBook::verify(){require(!held_);const auto anchor=journal_->inspect("retirement-anchor");require(anchor&&anchor->bytes==anchor_bytes_);const auto current=journal_->inspect("recurrence");require(current&&PiManagementJournal::digest(current->bytes)==revision_);}
void ControllerRetirementBook::publish(Json candidate) {
    candidate=sealed(std::move(candidate));validate(candidate);verify();
    const auto bytes=candidate.dump(),digest=PiManagementJournal::digest(bytes),anchor=anchor_record(candidate,digest).dump();
    try {
        journal_->prepare({{"recurrence",revision_,bytes},{"retirement-anchor",PiManagementJournal::digest(anchor_bytes_),anchor}});
        if(checkpoint_)checkpoint_(RetirementWritePoint::Prepared);
        journal_->commit();if(checkpoint_)checkpoint_(RetirementWritePoint::Committed);
        journal_->recover();if(checkpoint_)checkpoint_(RetirementWritePoint::Published);
    }catch(...){held_=true;throw;}
    data_=std::move(candidate);revision_=digest;anchor_bytes_=anchor;
}
Json ControllerRetirementBook::projection(const std::string &incarnation,const Json &assignments) const {
    Json rows=Json::array();for(const auto &row:data_.at("assignments")) {
        const auto live=assignment(assignments,row.at("target_id"));
        rows.push_back({{"target_id",row.at("target_id")},{"assignment_id",row.at("assignment_id")},{"assignment_revision",live?Json(assignment_revision(*live)):Json(nullptr)},
            {"state",row.at("state")},{"receipt",row.at("receipt")}});
    }
    Json result={{"contract","Wsprry-Control/1"},{"schema","controller-retirements"},{"controller_id",self_},{"controller_incarnation",incarnation},
        {"generation",data_.at("generation")},{"assignments",rows}};
    // The strong public revision covers every observable field, including the
    // live assignment revisions and process incarnation. It is independent of
    // the private image digest used for WAL publication.
    result["revision"]=PiManagementJournal::digest(result.dump());return result;
}
Json ControllerRetirementBook::read(const std::string &incarnation,const Json &assignments){std::lock_guard lock(mutex_);verify();return projection(incarnation,assignments);}
bool ControllerRetirementBook::permits(const std::string &target){std::lock_guard lock(mutex_);verify();const auto row=find(data_["assignments"],target);return row&&row->at("state")=="legacy";}
bool ControllerRetirementBook::pending(const std::string &target){std::lock_guard lock(mutex_);verify();const auto row=find(data_["assignments"],target);return row&&row->at("state")=="retiring";}
std::optional<Json> ControllerRetirementBook::replay_locked(const Json &intent,const std::string &revision) const {
    conflict(valid_controller_document(intent,"controller-retirement"));
    for(const auto &row:data_.at("assignments"))if(!row.at("intent").is_null()&&row.at("intent").at("operation_id")==intent.at("operation_id")) {
        conflict(row.at("intent")==intent&&row.at("request_revision")==revision);return row.at("receipt");
    }
    return {};
}
std::optional<Json> ControllerRetirementBook::replay(const Json &intent,const std::string &revision) {
    std::lock_guard lock(mutex_);verify();return replay_locked(intent,revision);
}
Json ControllerRetirementBook::retire(const Json &intent,const std::string &revision,const std::string &incarnation,const Json &assignments,const Json &observation){
    std::lock_guard lock(mutex_);verify();
    if(const auto retained=replay_locked(intent,revision))return *retained;
    conflict(intent.at("controller_id")==self_&&intent.at("controller_incarnation")==incarnation&&revision==projection(incarnation,assignments).at("revision").get<std::string>()&&intent.at("expected_retirement_generation")==data_.at("generation"));
    auto candidate=data_;auto row=find(candidate["assignments"],intent.at("target_id"));const auto live=assignment(assignments,intent.at("target_id"));
    conflict(row&&live&&row->at("state")=="legacy"&&row->at("assignment_id")==intent.at("assignment_id")&&assignment_revision(*live)==intent.at("expected_assignment_revision").get<std::string>());
    candidate["generation"]=next(candidate.at("generation"));
    const Json receipt={{"contract","Wsprry-Control/1"},{"schema","controller-retirement-operation"},{"controller_id",self_},{"target_id",intent.at("target_id")},
        {"assignment_id",row->at("assignment_id")},{"operation_id",intent.at("operation_id")},{"transition_id",intent.at("transition_id")},{"state","retiring"},{"generation",candidate.at("generation")}};
    (*row)["state"]="retiring";(*row)["frozen"]=*live;(*row)["intent"]=intent;(*row)["receipt"]=receipt;(*row)["observation"]=observation;(*row)["request_revision"]=revision;
    publish(std::move(candidate));return receipt;
}
void ControllerRetirementBook::confirm_idle(const std::string &target,const Json &observation){std::lock_guard lock(mutex_);verify();auto candidate=data_;auto row=find(candidate["assignments"],target);
    require(row&&row->at("state")=="retiring");candidate["generation"]=next(candidate.at("generation"));(*row)["state"]="retired";
    (*row)["completion"]=observation;(*row)["receipt"]["state"]="retired";(*row)["receipt"]["generation"]=candidate.at("generation");publish(std::move(candidate));}
}
