// SPDX-License-Identifier: MIT
#include <system_error>
#include "controller_retirement.hpp"
#include "controller_contract.hpp"
#include "pi_pair_contract.hpp"
#include "management_identity.hpp"
#include "owned_state_directory.hpp"
#include "browser_api.hpp"
#include "catalog.hpp"
#include "fleet_store.hpp"
#include "wtp_settings_json.hpp"
#include "wtp_endpoint/identity.hpp"
#include <filesystem>
#include <set>
#include <cerrno>
#ifdef _WIN32
#include "native_windows_file.hpp"
#else
#include <sys/stat.h>
#endif
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
std::filesystem::path disk_path(const std::string &path){
#ifdef _WIN32
    return windows_file::path_from_utf8(path);
#else
    return path;
#endif
}
std::string text(const std::filesystem::path &path){
#ifdef _WIN32
    const auto bytes=path.u8string();return {bytes.begin(),bytes.end()};
#else
    return path.string();
#endif
}
bool present(const std::string &name){
#ifdef _WIN32
    const auto path=disk_path(name);windows_file::ParentChain parents(path,windows_file::current_sid());
    if(::GetFileAttributesW(windows_file::api_path(path).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
    const auto error=::GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)throw std::runtime_error("controller_retirement_hold");return false;
#else
    struct stat info{};if(!::lstat(name.c_str(),&info))return true;
    if(errno!=ENOENT)throw std::runtime_error("controller_retirement_hold");
    return false;
#endif
}

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
Json controller_resume_assignment(const Json &frozen,const Json &live,const Json &grant,const std::string &authenticated_host){
    require(valid_pi_pair_document(grant,"resume-grant"));const auto target=grant["target_id"].get<std::string>(),controller=grant["controller_id"].get<std::string>();
    WtpFleetStore::validate({{"version",1},{"assignments",Json::array({frozen})},{"removals",Json::array()}},controller);
    WtpFleetStore::validate({{"version",1},{"assignments",Json::array({live})},{"removals",Json::array()}},controller);
    require(frozen["device_id"]==target&&frozen["product"]=="WsprryPi"&&live["device_id"]==target&&
        grant["assignment_revision"]==assignment_revision(frozen)&&assignment_revision(live)==assignment_revision(frozen));
    auto settings=parse_wtp_settings(frozen["settings"],true);require(settings.transport=="network_plain"&&settings.device_id==target);
    require(!authenticated_host.empty()&&authenticated_host.size()<=253&&authenticated_host.find_first_of("/\\?#@:\r\n\t ")==std::string::npos);
    settings.hostname=authenticated_host;settings.tcp_port=grant["wtp_binding"]["wtp_port"].get<unsigned>();
    validate_wtp_settings(settings,true);auto after=frozen;after["settings"]=wtp_settings_json(settings);
    after["management_port"]=grant["wtp_binding"]["management_port"];after["revocation_generation"]=grant["wtp_binding"]["revocation_generation"];
    after["in_flight"]=false;after["last_start_ns"]=std::to_string(std::max(wtp_fleet_decimal(frozen["last_start_ns"]),wtp_fleet_decimal(live["last_start_ns"])));
    WtpFleetStore::validate({{"version",1},{"assignments",Json::array({after})},{"removals",Json::array()}},controller);return after;
}
ControllerRetirementBook::ControllerRetirementBook(const std::string &directory,const std::string &source,
    const std::string &self,const Json &assignments,std::function<void(RetirementWritePoint)> checkpoint,
    std::function<void(PiPairInitializationProof)> pairing,const std::string &retained_source_binding)
    :self_(self),source_(retained_source_binding.empty()?PiManagementJournal::digest(text(std::filesystem::weakly_canonical(disk_path(source)))):retained_source_binding),checkpoint_(std::move(checkpoint)) {
    require(hex(source_,64));
    const bool anchor_present=present(directory+".established");
    if(anchor_present)require(present(directory));
    const auto initial=[&]{Json rows=Json::array();for(const auto &row:assignments)rows.push_back({{"target_id",row.at("device_id")},
        {"assignment_id",wtp_random_identity()},{"state","legacy"},{"frozen",nullptr},{"intent",nullptr},{"receipt",nullptr},{"observation",nullptr},{"completion",nullptr},{"request_revision",nullptr}});
        return sealed(Json{{"schema","controller-retirement-book/1"},{"controller_id",self_},{"source_binding",source_},{"generation","1"},{"installation_id",wtp_random_identity()},{"established",false},{"assignments",rows}}).dump();};
    const auto location=prepare_owned_state_directory(directory,initial,[&](const std::string &bytes){validate(private_record(bytes));});
    authority_path_=location.file;
    // Validate the immutable source binding before interpreting any retained WAL.
    validate(private_record(PiManagementJournal::read_ini_input(location.file).bytes));
    journal_=std::make_unique<PiManagementJournal>(directory+"/transactions",self_,std::map<std::string,std::string>{{"recurrence",location.file},{"retirement-anchor",text(disk_path(location.file).parent_path())+".established"},{"assignments",source}});
    journal_->recover();const auto observed=journal_->inspect("recurrence");require(bool(observed));
    data_=private_record(observed->bytes);validate(data_);revision_=PiManagementJournal::digest(observed->bytes);
    anchor_bytes_=anchor_record(data_,revision_).dump();
    auto anchor=journal_->inspect("retirement-anchor");
    if(data_.at("established")==false) {
        require(!anchor||anchor->bytes==anchor_record(data_,revision_).dump());
        // Before the first establishment decision, retained initialization is
        // still a positive pre-first-use event. Established history never
        // issues another proof just because pairing storage is missing.
        if(pairing){
            try{pairing(PiPairInitializationProof(self_,data_.at("installation_id"),PiPairInitializationProof::Source::ControllerEstablishment));}
            catch(...){/* Pairing failure cannot remove existing dispatch fences. */}
        }
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
    auto base_book=j;base_book.erase("history");base_book.erase("resumes");
    require(fields(base_book,{"schema","controller_id","source_binding","generation","assignments","checksum","installation_id","established"})&&j.at("schema")=="controller-retirement-book/1"&&
        j.at("controller_id")==self_&&j.at("source_binding")==source_&&hex(j.at("installation_id"),32)&&j.at("established").is_boolean()&&j.at("assignments").is_array()&&j.at("assignments").size()<=32&&wtp_fleet_decimal(j.at("generation"))>0);
    require(hex(j.at("checksum"),64)&&sealed(j)==j);
    if(j.at("established")==false){require(j.at("generation")=="1");for(const auto &row:j.at("assignments"))require(row.at("state")=="legacy");}
    std::set<std::string> targets,ids,operations;
    for(const auto &row:j.at("assignments"))require(targets.insert(row.at("target_id")).second&&ids.insert(row.at("assignment_id")).second);
    const auto history=j.value("history",Json::array()),resumes=j.value("resumes",Json::array());
    require(history.is_array()&&resumes.is_array()&&history.size()+resumes.size()<=256&&
        (j["established"]==true||(history.empty()&&resumes.empty())));
    auto fences=j["assignments"];for(const auto &row:history){require(row.is_object()&&row.value("state",Json(nullptr))=="retired");fences.push_back(row);}
    std::set<std::string> resume_ids;std::map<std::string,std::uint64_t> target_generations;std::uint64_t source_generation=0;
    for(const auto &row:resumes){
        require(fields(row,{"grant","receipt","acknowledgement","after"})&&valid_pi_pair_document(row["grant"],"resume-grant")&&
            valid_pi_pair_document(row["acknowledgement"],"resume-ack")&&row["grant"]["controller_id"]==self_);
        const auto &grant=row["grant"],&receipt=row["receipt"];
        require(fields(receipt,{"schema","operation_id","target_id","assignment_id","grant_digest","outcome","controller_generation","assignment_revision"})&&
            receipt["schema"]=="controller-resume-receipt/1"&&receipt["operation_id"]==grant["operation_id"]&&receipt["target_id"]==grant["target_id"]&&
            receipt["assignment_id"]==grant["assignment_id"]&&receipt["grant_digest"]==grant["grant_digest"]&&
            (receipt["outcome"]=="resumed"||receipt["outcome"]=="refused")&&wtp_fleet_decimal(receipt["controller_generation"])>0&&
            wtp_fleet_decimal(receipt["controller_generation"])<=wtp_fleet_decimal(j["generation"])&&resume_ids.insert(grant["operation_id"].get<std::string>()).second);
        const auto target_counter=wtp_fleet_decimal(grant["authority_generation"]),source_counter=wtp_fleet_decimal(receipt["controller_generation"]);
        const auto lineage=grant["target_id"].get<std::string>()+grant["assignment_id"].get<std::string>();
        require(target_counter>target_generations[lineage]&&source_counter>source_generation);target_generations[lineage]=target_counter;source_generation=source_counter;
        Json expected={{"contract","Wsprry-Pair/1"},{"schema","resume-ack"},{"operation_id",grant["operation_id"]},{"pair_id",grant["pair_id"]},
            {"authorization_generation",grant["authorization_generation"]},{"target_id",grant["target_id"]},{"controller_id",self_},{"assignment_id",grant["assignment_id"]},
            {"grant_digest",grant["grant_digest"]},{"outcome",receipt["outcome"]},{"controller_generation",receipt["controller_generation"]},
            {"receipt_digest",PiManagementJournal::digest(canonical_pi_management_json(receipt))}};
        auto actual=row["acknowledgement"];actual.erase("proof");require(actual==expected);
        if(receipt["outcome"]=="resumed"){
            WtpFleetStore::validate({{"version",1},{"assignments",Json::array({row["after"]})},{"removals",Json::array()}},self_);
            require(row["after"]["device_id"]==grant["target_id"]&&receipt["assignment_revision"]==assignment_revision(row["after"]));
            bool known=false;for(const auto &fence:fences)if(fence["assignment_id"]==grant["assignment_id"]&&fence["state"]=="retired"&&
                !fence["receipt"].is_null()&&grant["retirement_receipt_digest"]==PiManagementJournal::digest(canonical_pi_management_json(fence["receipt"]))&&
                fence["intent"]["expected_assignment_revision"]==grant["assignment_revision"]&&fence.contains("machine")&&
                fence["machine"]["request"]["transition_id"]==grant["transition_id"]&&
                wtp_fleet_decimal(grant["authority_generation"])>wtp_fleet_decimal(fence["machine"]["request"]["retirement_generation"])){
                auto live=fence["frozen"];live["last_start_ns"]=row["after"]["last_start_ns"];
                require(controller_resume_assignment(fence["frozen"],live,grant,parse_wtp_settings(row["after"]["settings"],true).hostname)==row["after"]);known=true;
            }
            require(known);
        }else require(row["after"].is_null()&&receipt["assignment_revision"].is_null());
    }
    for(const auto &row:fences) {
        auto base=row;base.erase("machine");base.erase("resume_operation");base.erase("target_generation");
        require(fields(base,{"target_id","assignment_id","state","frozen","intent","receipt","observation","completion","request_revision"})&&hex(row.at("target_id"),32)&&hex(row.at("assignment_id"),32));
        if(row.contains("target_generation"))require(wtp_fleet_decimal(row["target_generation"])>0);
        if(row.contains("resume_operation")){
            require(hex(row["resume_operation"],32)&&row["state"]=="retired");bool found=false;
            for(const auto &resume:resumes)if(resume["grant"]["operation_id"]==row["resume_operation"]&&resume["grant"]["target_id"]==row["target_id"]&&
                resume["grant"]["assignment_id"]==row["assignment_id"]&&resume["receipt"]["outcome"]=="resumed"&&
                row.contains("target_generation")&&row["target_generation"]==resume["grant"]["authority_generation"])found=true;
            require(found);
        }
        const bool legacy=row.at("state")=="legacy";
        require(legacy||row.at("state")=="retiring"||row.at("state")=="retired");
        if(legacy){require(!row.contains("machine"));require(row.at("frozen").is_null()&&row.at("intent").is_null()&&row.at("receipt").is_null()&&row.at("observation").is_null()&&row.at("completion").is_null()&&row.at("request_revision").is_null());continue;}
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
        if(row.contains("machine")){
            const auto &machine=row.at("machine");require(fields(machine,{"request","response"}));
            const auto &request=machine.at("request"),&response=machine.at("response");
            require(valid_pi_pair_document(request,"retirement")&&request.at("controller_id")==self_&&
                request.at("target_id")==row.at("target_id")&&request.at("assignment_id")==row.at("assignment_id")&&
                request.at("assignment_revision")==intent.at("expected_assignment_revision")&&
                (row.at("state")=="retired"||request.at("transition_id")==intent.at("transition_id")));
            if(row.at("state")=="retiring")require(response.is_null());
            else{
                require(valid_pi_pair_document(response,"retired"));auto expected=request;expected.erase("proof");expected["schema"]="retired";
                auto actual=response;actual.erase("proof");actual.erase("receipt_digest");
                require(actual==expected&&response.at("receipt_digest")==PiManagementJournal::digest(canonical_pi_management_json(receipt)));
            }
        }
        WtpFleetStore::validate({{"version",1},{"assignments",Json::array({row.at("frozen")})},{"removals",Json::array()}},self_);
        require(row.at("frozen").at("device_id")==row.at("target_id")&&assignment_revision(row.at("frozen"))==intent.at("expected_assignment_revision").get<std::string>());
    }
}
void ControllerRetirementBook::verify(){require(!held_);const auto anchor=journal_->inspect("retirement-anchor");require(anchor&&anchor->bytes==anchor_bytes_);const auto current=journal_->inspect("recurrence");require(current&&PiManagementJournal::digest(current->bytes)==revision_);}
void ControllerRetirementBook::with_backup_files(const std::function<void(const std::map<std::string,std::string> &)> &capture){
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())throw std::system_error(std::make_error_code(std::errc::device_or_resource_busy));
    require(bool(capture));
    verify();require(!journal_->decision());
    capture({{"retirement/authority.json",authority_path_},
        {"retirement/established.json",text(disk_path(authority_path_).parent_path())+".established"}});
    verify();require(!journal_->decision());
}
void ControllerRetirementBook::publish(Json candidate,const std::optional<PiManagementJournal::Image> &assignment) {
    candidate=sealed(std::move(candidate));validate(candidate);verify();
    const auto bytes=candidate.dump(),digest=PiManagementJournal::digest(bytes),anchor=anchor_record(candidate,digest).dump();
    try {
        std::vector<PiManagementJournal::Image> images{{"recurrence",revision_,bytes},{"retirement-anchor",PiManagementJournal::digest(anchor_bytes_),anchor}};
        if(assignment){require(assignment->kind=="assignments");images.push_back(*assignment);}
        journal_->prepare(images);
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
            {"state",row.contains("resume_operation")?Json("rollback"):row.at("state")},{"receipt",row.at("receipt")}});
    }
    Json result={{"contract","Wsprry-Control/1"},{"schema","controller-retirements"},{"controller_id",self_},{"controller_incarnation",incarnation},
        {"generation",data_.at("generation")},{"assignments",rows}};
    // The strong public revision covers every observable field, including the
    // live assignment revisions and process incarnation. It is independent of
    // the private image digest used for WAL publication.
    result["revision"]=PiManagementJournal::digest(result.dump());return result;
}
Json ControllerRetirementBook::read(const std::string &incarnation,const Json &assignments){std::lock_guard lock(mutex_);verify();return projection(incarnation,assignments);}
bool ControllerRetirementBook::permits(const std::string &target){std::lock_guard lock(mutex_);verify();const auto row=find(data_["assignments"],target);return row&&(row->at("state")=="legacy"||row->contains("resume_operation"));}
bool ControllerRetirementBook::pending(const std::string &target){std::lock_guard lock(mutex_);verify();const auto row=find(data_["assignments"],target);return row&&row->at("state")=="retiring";}
bool ControllerRetirementBook::retirement_scope_closed(const Json &pair,const Json &migration){
    std::lock_guard lock(mutex_);verify();
    if(pair.value("state",Json(nullptr))!="revoked"||pair.value("self_id",Json(nullptr))!=self_||
        !valid_controller_document(migration,"target-migration-read")||migration["binding"]["device_id"]!=pair["peer_id"]||
        (migration["state"]!="staged"&&migration["state"]!="native")||!migration["jobs"].empty()||data_["established"]!=true)return false;
    const auto target=pair["peer_id"].get<std::string>();const auto current=find(data_["assignments"],target);
    if(!current||(*current)["state"]!="retired"||current->contains("resume_operation")||!current->contains("machine")||
        (*current)["machine"]["response"].is_null())return false;
    const auto confirmed=[&](const Json &fence){
        if(fence["state"]!="retired"||fence["receipt"].is_null()||!fence.contains("machine")||fence["machine"]["response"].is_null())return false;
        const auto &request=fence["machine"]["request"];
        for(const auto &proof:migration["retirements"])if(proof["controller_id"]==self_&&proof["assignment_id"]==fence["assignment_id"]&&
            proof["transition_id"]==request["transition_id"]&&proof["retirement_generation"]==request["retirement_generation"]&&
            proof["receipt_digest"]==PiManagementJournal::digest(canonical_pi_management_json(fence["receipt"])))return true;
        return false;
    };
    if(!confirmed(*current))return false;
    bool known=false;
    const auto scope=[&](const Json &row){return row.contains("machine")&&row["machine"]["request"]["pair_id"]==pair["pair_id"]&&
        row["machine"]["request"]["authorization_generation"]==pair["authorization_generation"];};
    known=scope(*current);
    for(const auto &row:data_.value("history",Json::array()))if(row["target_id"]==target){
        known=known||scope(row);
        if(row["assignment_id"]!=(*current)["assignment_id"]&&!confirmed(row))return false;
    }
    for(const auto &row:data_.value("resumes",Json::array()))if(row["grant"]["target_id"]==target&&row["receipt"]["outcome"]=="resumed"){
        bool closed=false;for(const auto &proof:migration["retirements"])if(proof["controller_id"]==self_&&proof["assignment_id"]==row["grant"]["assignment_id"]&&
            wtp_fleet_decimal(proof["retirement_generation"])>wtp_fleet_decimal(row["grant"]["authority_generation"]))closed=true;
        if(!closed)return false;
    }
    return known;
}
std::optional<Json> ControllerRetirementBook::resume_receipt(const Json &grant){
    std::lock_guard lock(mutex_);verify();conflict(valid_pi_pair_document(grant,"resume-grant")&&grant["controller_id"]==self_);
    for(const auto &row:data_.value("resumes",Json::array()))if(row["grant"]["operation_id"]==grant["operation_id"]){conflict(row["grant"]==grant);return row["acknowledgement"];}
    return {};
}
ControllerRetirementBook::ResumeIntent ControllerRetirementBook::prepare_resume(const Json &grant,const std::string &incarnation,
    const Json &assignments,const std::string &host,bool current_target_valid){
    std::lock_guard lock(mutex_);verify();conflict(valid_pi_pair_document(grant,"resume-grant")&&grant["controller_id"]==self_&&hex(incarnation,32));
    for(const auto &saved:data_.value("resumes",Json::array()))conflict(saved["grant"]["operation_id"]!=grant["operation_id"]);
    const auto row=find(data_["assignments"],grant["target_id"].get<std::string>());
    // Missing or inconsistent positive source history never proves refusal.
    conflict(row&&row->at("assignment_id")==grant["assignment_id"]&&row->at("state")=="retired"&&!row->contains("resume_operation"));
    if(row->contains("target_generation"))conflict(wtp_fleet_decimal(grant["authority_generation"])>wtp_fleet_decimal((*row)["target_generation"]));
    if(row->contains("machine"))conflict(wtp_fleet_decimal(grant["authority_generation"])>wtp_fleet_decimal((*row)["machine"]["request"]["retirement_generation"]));
    const auto observed=journal_->inspect("assignments");require(observed.has_value());const auto source=private_record(observed->bytes);WtpFleetStore::validate(source,self_);
    conflict(source["assignments"]==assignments);const auto live=assignment(assignments,row->at("target_id"));
    require(data_.value("history",Json::array()).size()+data_.value("resumes",Json::array()).size()<256);
    bool consume=current_target_valid&&grant["controller_incarnation"]==incarnation&&live&&
        grant["retirement_receipt_digest"]==PiManagementJournal::digest(canonical_pi_management_json(row->at("receipt")))&&
        grant["assignment_revision"]==row->at("intent").at("expected_assignment_revision")&&row->contains("machine")&&
        grant["transition_id"]==row->at("machine").at("request").at("transition_id");
    Json after=nullptr,publication=source;
    if(consume){try{after=controller_resume_assignment(row->at("frozen"),*live,grant,host);}catch(...){consume=false;}}
    if(consume)for(auto &current:publication["assignments"])if(current["device_id"]==grant["target_id"])current=after;
    const auto generation=next(data_["generation"]);
    const Json receipt={{"schema","controller-resume-receipt/1"},{"operation_id",grant["operation_id"]},{"target_id",grant["target_id"]},
        {"assignment_id",grant["assignment_id"]},{"grant_digest",grant["grant_digest"]},{"outcome",consume?"resumed":"refused"},
        {"controller_generation",generation},{"assignment_revision",consume?Json(assignment_revision(after)):Json(nullptr)}};
    const Json ack={{"contract","Wsprry-Pair/1"},{"schema","resume-ack"},{"operation_id",grant["operation_id"]},{"pair_id",grant["pair_id"]},
        {"authorization_generation",grant["authorization_generation"]},{"target_id",grant["target_id"]},{"controller_id",self_},{"assignment_id",grant["assignment_id"]},
        {"grant_digest",grant["grant_digest"]},{"outcome",receipt["outcome"]},{"controller_generation",generation},
        {"receipt_digest",PiManagementJournal::digest(canonical_pi_management_json(receipt))}};
    return ResumeIntent(this,revision_,grant,std::move(publication),receipt,ack,observed);
}
Json ControllerRetirementBook::publish_resume(const ResumeIntent &intent,const Json &signed_ack){
    std::lock_guard lock(mutex_);verify();conflict(intent.owner_==this&&std::chrono::steady_clock::now()<intent.until_&&intent.revision_==revision_&&
        valid_pi_pair_document(signed_ack,"resume-ack"));auto unsigned_ack=signed_ack;unsigned_ack.erase("proof");conflict(unsigned_ack==intent.acknowledgement_);
    const auto observed=journal_->inspect("assignments");conflict(observed&&intent.source_&&observed->signature==intent.source_->signature);
    auto candidate=data_;candidate["generation"]=intent.receipt_["controller_generation"];
    if(!candidate.contains("resumes"))candidate["resumes"]=Json::array();
    const bool resumed=intent.receipt_["outcome"]=="resumed";
    const auto live=assignment(intent.assignments_["assignments"],intent.grant_["target_id"].get<std::string>());
    candidate["resumes"].push_back({{"grant",intent.grant_},{"receipt",intent.receipt_},{"acknowledgement",signed_ack},{"after",resumed?*live:Json(nullptr)}});
    auto row=find(candidate["assignments"],intent.grant_["target_id"].get<std::string>());require(row);(*row)["target_generation"]=intent.grant_["authority_generation"];
    if(resumed)(*row)["resume_operation"]=intent.grant_["operation_id"];
    const auto image=resumed?std::optional<PiManagementJournal::Image>{{"assignments",PiManagementJournal::digest(observed->bytes),intent.assignments_.dump(2)+"\n",intent.source_->signature}}:std::nullopt;
    publish(std::move(candidate),image);return signed_ack;
}
std::optional<Json> ControllerRetirementBook::replay_locked(const Json &intent,const std::string &revision) const {
    conflict(valid_controller_document(intent,"controller-retirement"));
    auto rows=data_["assignments"];for(const auto &row:data_.value("history",Json::array()))rows.push_back(row);
    for(const auto &row:rows)if(!row.at("intent").is_null()&&row.at("intent").at("operation_id")==intent.at("operation_id")) {
        conflict(row.at("intent")==intent&&row.at("request_revision")==revision);return row.at("receipt");
    }
    return {};
}
std::optional<Json> ControllerRetirementBook::replay(const Json &intent,const std::string &revision) {
    std::lock_guard lock(mutex_);verify();return replay_locked(intent,revision);
}
Json ControllerRetirementBook::retire(const Json &intent,const std::string &revision,const std::string &incarnation,const Json &assignments,const Json &observation,const Json &machine_request){
    std::lock_guard lock(mutex_);verify();
    if(const auto retained=replay_locked(intent,revision)){
        const auto row=find(data_["assignments"],intent.at("target_id"));
        conflict(row&&(machine_request.is_null()?!row->contains("machine"):
            row->contains("machine")&&row->at("machine").at("request")==machine_request));return *retained;
    }
    conflict(intent.at("controller_id")==self_&&intent.at("controller_incarnation")==incarnation&&revision==projection(incarnation,assignments).at("revision").get<std::string>()&&intent.at("expected_retirement_generation")==data_.at("generation"));
    auto candidate=data_;auto row=find(candidate["assignments"],intent.at("target_id"));const auto live=assignment(assignments,intent.at("target_id"));
    conflict(row&&live&&(row->at("state")=="legacy"||row->contains("resume_operation"))&&row->at("assignment_id")==intent.at("assignment_id")&&assignment_revision(*live)==intent.at("expected_assignment_revision").get<std::string>());
    if(row->contains("resume_operation")){
        require(candidate.value("history",Json::array()).size()+candidate.value("resumes",Json::array()).size()<256);
        if(!candidate.contains("history"))candidate["history"]=Json::array();
        candidate["history"].push_back(*row);
        if(!machine_request.is_null())conflict(wtp_fleet_decimal(machine_request["retirement_generation"])>wtp_fleet_decimal((*row)["target_generation"]));
        row->erase("resume_operation");row->erase("machine");(*row)["completion"]=nullptr;
    }
    candidate["generation"]=next(candidate.at("generation"));
    const Json receipt={{"contract","Wsprry-Control/1"},{"schema","controller-retirement-operation"},{"controller_id",self_},{"target_id",intent.at("target_id")},
        {"assignment_id",row->at("assignment_id")},{"operation_id",intent.at("operation_id")},{"transition_id",intent.at("transition_id")},{"state","retiring"},{"generation",candidate.at("generation")}};
    (*row)["state"]="retiring";(*row)["frozen"]=*live;(*row)["intent"]=intent;(*row)["receipt"]=receipt;(*row)["observation"]=observation;(*row)["request_revision"]=revision;
    if(!machine_request.is_null()){
        (*row)["machine"]={{"request",machine_request},{"response",nullptr}};(*row)["target_generation"]=machine_request["retirement_generation"];
    }
    publish(std::move(candidate));return receipt;
}
void ControllerRetirementBook::confirm_idle(const std::string &target,const Json &observation,const RetirementSigner &signer){
    std::unique_lock lock(mutex_);verify();const auto before=revision_;auto candidate=data_;auto row=find(candidate["assignments"],target);
    require(row&&row->at("state")=="retiring");candidate["generation"]=next(candidate.at("generation"));(*row)["state"]="retired";
    (*row)["completion"]=observation;(*row)["receipt"]["state"]="retired";(*row)["receipt"]["generation"]=candidate.at("generation");
    if(row->contains("machine")){
        require(bool(signer));const auto request=row->at("machine").at("request"),receipt=row->at("receipt");
        // Root history admission precedes book admission elsewhere. Never call
        // its signer while holding the book: recheck the exact before-image.
        lock.unlock();const auto response=signer(request,receipt);lock.lock();verify();
        require(revision_==before&&response.has_value());(*row)["machine"]["response"]=*response;
    }
    publish(std::move(candidate));
}
std::optional<Json> ControllerRetirementBook::machine_receipt(const Json &request){
    std::lock_guard lock(mutex_);verify();conflict(valid_pi_pair_document(request,"retirement")||valid_pi_pair_document(request,"retirement-status"));
    auto rows=data_["assignments"];for(const auto &row:data_.value("history",Json::array()))rows.push_back(row);
    for(const auto &row:rows)if(row.contains("machine")){
        const auto &machine=row.at("machine");auto expected=machine.at("request");expected.erase("proof");
        auto actual=request;actual.erase("proof");actual["schema"]="retirement";
        if(request.at("schema")=="retirement-status"){expected.erase("assignment_revision");expected.erase("retirement_generation");}
        if(expected.at("transition_id")!=actual.at("transition_id"))continue;
        conflict(expected==actual);
        if(!machine.at("response").is_null())return machine.at("response");
        const auto &original=machine.at("request");
        return Json{{"contract","Wsprry-Pair/1"},{"schema","operation"},{"operation_id",original.at("transition_id")},
            {"pair_id",original.at("pair_id")},{"authorization_generation",original.at("authorization_generation")},
            {"request_digest",PiManagementJournal::digest(canonical_pi_management_json(original))},{"state","pending"},{"result_digest",nullptr}};
    }
    return {};
}
std::optional<Json> ControllerRetirementBook::retired_fence(const Json &request){
    std::lock_guard lock(mutex_);verify();conflict(valid_pi_pair_document(request,"retirement")&&request.at("controller_id")==self_);
    const auto row=find(data_["assignments"],request.at("target_id"));
    if(!row||row->at("state")!="retired"||row->contains("machine"))return {};
    conflict(row->at("assignment_id")==request.at("assignment_id")&&row->at("intent").at("expected_assignment_revision")==request.at("assignment_revision"));
    return Json{{"revision",revision_},{"receipt",row->at("receipt")}};
}
void ControllerRetirementBook::bind_retired(const Json &request,const std::string &before_revision,const Json &response){
    std::lock_guard lock(mutex_);verify();conflict(valid_pi_pair_document(request,"retirement")&&request.at("controller_id")==self_&&revision_==before_revision);
    auto candidate=data_;auto row=find(candidate["assignments"],request.at("target_id"));
    conflict(row&&row->at("state")=="retired"&&!row->contains("machine")&&row->at("assignment_id")==request.at("assignment_id")&&
        row->at("intent").at("expected_assignment_revision")==request.at("assignment_revision"));
    candidate["generation"]=next(candidate.at("generation"));(*row)["machine"]={{"request",request},{"response",response}};
    publish(std::move(candidate));
}

}
