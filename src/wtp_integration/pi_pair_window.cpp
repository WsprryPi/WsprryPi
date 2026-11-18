// SPDX-License-Identifier: MIT
#include "pi_pair_window.hpp"
#include "pi_pair_contract.hpp"
#include "pi_pair_records.hpp"
#include "controller_contract.hpp"
#include "privileged_network_runtime.hpp"
#include <openssl/rand.h>
#include <charconv>
#include <deque>
#include <mutex>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
constexpr auto suite="SPAKE2+-P256-SHA256-HKDF-HMAC";
bool policy_ready(){const auto state=privileged_network_runtime_state();return state.active_known&&
    state.active==PrivilegedNetworkMode::enforced&&state.configured==PrivilegedNetworkMode::enforced;}
void require(bool value){if(!value)throw std::runtime_error("pairing_window_unavailable");}
bool hex(const std::string &value,std::size_t size){return value.size()==size&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string fresh(){
    std::array<unsigned char,16> value{};require(RAND_bytes(value.data(),value.size())==1);std::string result;
    for(auto byte:value){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;
}
std::string nonce(){
    std::array<unsigned char,32> value{};require(RAND_bytes(value.data(),value.size())==1);return PiPairSecret(value).storage_encoding();
}
PicoHttpResponse result(unsigned status,const Json &pair,const char *code,bool retry=false,const char *state="rejected"){
    Json body{{"contract","Wsprry-Pair/1"},{"schema","result"},{"pair_id",pair},{"state",state},{"code",code},{"retryable",retry}};
    require(valid_pi_pair_document(body,"result"));return {status,body.dump(),{}};
}
PicoHttpResponse document(const Json &value,const char *family,bool control=false){
    require(control?valid_controller_document(value,family):valid_pi_pair_document(value,family));return {200,value.dump(),{}};
}
}
PiPairCommitApproval::PiPairCommitApproval(Json hello,std::string digest,PiPairSecret tls,PiPairSecret retirement,
    std::function<Time()> clock,Time deadline,ManagementBrowserSessions &sessions):hello_(std::move(hello)),digest_(std::move(digest)),tls_(std::move(tls)),
    retirement_(std::move(retirement)),clock_(std::move(clock)),deadline_(deadline),sessions_(sessions){}
bool PiPairCommitApproval::valid() const noexcept{try{return clock_&&clock_()<deadline_;}catch(...){return false;}}
struct PiPairWindow::Impl {
    struct Attempt {
        Json hello,challenge,exchange_request,exchange_response,confirmation,confirmed,commit_request;
        std::optional<PicoHttpResponse> commit_response;
        std::unique_ptr<PiPairExchange> exchange;
        Time deadline;
        bool busy{},mutually_confirmed{};
    };
    struct Window {
        std::string id,pair,operation,scope,digest,generation,action;
        Time deadline;
        std::shared_ptr<PiPairCode> code;
        std::shared_ptr<Attempt> attempt;
        unsigned failures{};
        bool open{true};
    };
    struct Operation {std::string id,scope,digest;};
    PiPairHistory &history;
    ManagementBrowserSessions &sessions;
    std::string self,boot;
    Commit commit;
    Clock clock;
    std::mutex mutex;
    std::shared_ptr<Window> window;
    std::deque<Operation> operations;
    std::optional<Time> last;
    Impl(PiPairHistory &owner,ManagementBrowserSessions &browser,std::string device,std::string incarnation,Commit writer,Clock time)
        :history(owner),sessions(browser),self(std::move(device)),boot(std::move(incarnation)),commit(std::move(writer)),clock(std::move(time)){
        require(hex(self,32)&&hex(boot,32)&&bool(commit)&&bool(clock));
    }
    Time now(){
        const auto current=clock();if(last&&current<*last)window.reset();last=current;
        if(window&&current>=window->deadline)window.reset();
        return current;
    }
    bool live(Time current) const{return window&&window->open&&window->code&&current<window->deadline&&
        (!window->attempt||current<window->attempt->deadline);}
    Json catalog(){return parse_pi_pair_catalog(history.snapshot().catalog.bytes,self);}
    void failed(Time current){
        if(!window)return;
        window->attempt.reset();
        if(++window->failures>=5||current>=window->deadline){window->open=false;window->code.reset();return;}
        window->open=false;
        try{window->pair=fresh();window->open=true;}catch(...){window->code.reset();throw;}
    }
    PicoHttpResponse opened() const {
        require(window&&window->code);
        return document(Json{{"contract","Wsprry-Control/1"},{"schema","pair-window"},{"window_id",window->id},
            {"device_id",self},{"boot_id",boot},{"authorization_generation",window->generation},
            {"code",window->code->display()},{"expires_in_ms",120000}},"pair-window-response",true);
    }
};
PiPairWindow::PiPairWindow(PiPairHistory &history,ManagementBrowserSessions &sessions,std::string self,std::string boot,Commit commit,Clock clock)
    :impl_(std::make_unique<Impl>(history,sessions,std::move(self),std::move(boot),std::move(commit),std::move(clock))){}
PiPairWindow::~PiPairWindow()=default;
void PiPairWindow::cancel() noexcept{std::lock_guard lock(impl_->mutex);impl_->window.reset();}
void PiPairWindow::advance(){
    std::lock_guard lock(impl_->mutex);const auto current=impl_->now();
    if(!policy_ready()){impl_->window.reset();return;}
    if(impl_->window&&impl_->window->open&&impl_->window->attempt&&current>=impl_->window->attempt->deadline)impl_->failed(current);
}
PicoHttpResponse PiPairWindow::open(const BrowserRequest &request,const LocalBrowserPrincipal &principal){
    auto &p=*impl_;
    if(!policy_ready()||!principal.matches(request)||!p.sessions.owns(principal)||request.method!="POST"||request.path!="/api/control/v1/host/pairing/window")
        return controller_target_error(403,"unauthorized");
    Json intent;try{intent=strict_browser_json(request.body,8);}catch(...){return controller_target_error(400,"invalid_json");}
    if(!valid_controller_document(intent,"pair-window-request"))return controller_target_error(400,"invalid_shape");
    std::lock_guard lock(p.mutex);const auto current=p.now();
    if(!principal.matches(request))return controller_target_error(403,"unauthorized");
    const auto digest=PiManagementJournal::digest(canonical_pi_management_json(intent));
    for(const auto &known:p.operations)if(intent["operation_id"]==known.id){
        if(known.scope!=principal.session_scope()||known.digest!=digest)return controller_target_error(409,"request_id_reuse");
        if(p.live(current)&&p.window->operation==known.id&&p.window->scope==known.scope){
            try{
                if(p.catalog()["authorization_generation"]!=p.window->generation){p.window.reset();return controller_target_error(409,"revision_conflict");}
                return p.opened();
            }catch(...){return controller_target_error(503,"storage_unavailable");}
        }
        return controller_target_error(409,"operation_unknown");
    }
    try{
        const auto hint=p.history.recovery_generation();if(!hint)return controller_target_error(503,"storage_unavailable");
        if(intent["expected_authorization_generation"]!=*hint)return controller_target_error(409,"revision_conflict");
        if(intent["action"]=="recover"){
            if(p.history.available())return controller_target_error(409,"confirmation_required");
            p.history.recover_access(request,principal,p.sessions);
        }
        if(!policy_ready())return controller_target_error(403,"unauthorized");
        const auto catalog=p.catalog();const bool paired=!catalog["active"].is_null();
        if(catalog["authorization_generation"]=="18446744073709551615")return controller_target_error(409,"capacity");
        if((intent["action"]=="pair"&&paired)||(intent["action"]=="replace"&&!paired))return controller_target_error(409,"confirmation_required");
        if(paired&&catalog["active"]["state"]!="active")return controller_target_error(503,"authority_pending");
        // Keep one of the existing 32 tombstone slots available for Remove.
        // Exhaustion never permits dropping unresolved retirement evidence.
        if(catalog["retired"].size()>=(paired?31U:32U))return controller_target_error(429,"capacity");
        auto next=std::make_shared<Impl::Window>();next->id=fresh();next->pair=fresh();next->operation=intent["operation_id"];
        next->scope=principal.session_scope();next->digest=digest;next->generation=catalog["authorization_generation"];next->action=intent["action"];
        next->deadline=current+std::chrono::seconds(120);next->code=std::make_shared<PiPairCode>(PiPairCode::generate());
        if(p.operations.size()>=256){p.sessions.retire();p.operations.pop_front();}
        p.operations.push_back({next->operation,next->scope,next->digest});p.window=std::move(next);return p.opened();
    }catch(...){return controller_target_error(503,"storage_unavailable");}
}
PicoHttpResponse PiPairWindow::state(const BrowserRequest &request,const LocalPairingReadPrincipal &principal){
    auto &p=*impl_;if(!policy_ready()||!principal.matches(request))return controller_target_error(403,"unauthorized");
    std::lock_guard lock(p.mutex);const auto current=p.now();Json manager=nullptr,visible=nullptr;std::string status="access_recovery",generation="0";
    try{
        const auto catalog=p.catalog();generation=catalog["authorization_generation"];status=catalog["active"].is_null()?"unpaired":
            catalog["active"]["state"]=="active"?"paired":"unavailable";
        if(status=="paired")manager={{"device_id",catalog["active"]["peer_id"]},{"name","Controller "+catalog["active"]["peer_id"].get<std::string>()}};
        if(p.window&&p.window->open&&p.window->generation!=generation)p.window.reset();
        if(p.live(current))visible={{"window_id",p.window->id},{"expires_in_ms",std::chrono::duration_cast<std::chrono::milliseconds>(p.window->deadline-current).count()}};
    }catch(...){const auto hint=p.history.recovery_generation();if(hint)generation=*hint;else status="unavailable";}
    return document(Json{{"contract","Wsprry-Control/1"},{"schema","pairing-state"},{"device_id",p.self},{"boot_id",p.boot},
        {"authorization_generation",generation},{"state",status},{"manager",manager},{"window",visible}},"pairing-state",true);
}
PicoHttpResponse PiPairWindow::bootstrap(const BrowserRequest &request,const PiPairBootstrapPrincipal &principal){
    auto &p=*impl_;if(!principal.matches(request))return result(403,nullptr,"invalid_proof");
    if(!policy_ready()){cancel();return result(403,nullptr,"pairing_closed");}
    const auto prefix=std::string("/api/pair/v1/");if(!request.path.starts_with(prefix))return result(400,nullptr,"invalid_shape");
    const auto resource=request.path.substr(prefix.size());
    const char *family=resource=="hello"?"hello":resource=="exchange"?"exchange-request":resource=="confirm"?"confirm":resource=="commit"?"commit":nullptr;
    Json body;
    if(request.method!="GET")try{require(family&&request.method=="POST");body=parse_pi_pair_document(request.body,family);}catch(...){return result(400,nullptr,"invalid_shape");}
    std::unique_lock lock(p.mutex);const auto current=p.now();
    try{
        const auto catalog=p.catalog();const auto generation=catalog["authorization_generation"].get<std::string>();
        // A historical MAC-bound public reply cannot restore authorization.
        // Preserve its exact ephemeral replay across its own generation bump;
        // the controller still requires current paired TLS readback to enroll.
        if(request.method=="POST"&&p.window&&p.window->attempt&&current<p.window->attempt->deadline&&resource=="commit"&&p.window->attempt->commit_response)
            return p.window->attempt->commit_request==body?*p.window->attempt->commit_response:result(409,body["pair_id"],"request_id_reuse");
        if(p.window&&p.window->open&&p.window->generation!=generation)p.window.reset();
        if(request.method=="GET"){
            if(resource!="window")return result(400,nullptr,"invalid_shape");
            const bool open=p.live(current);
            return document(Json{{"contract","Wsprry-Pair/1"},{"schema","window"},{"suite",suite},{"device_id",p.self},{"boot_id",p.boot},
                {"open",open},{"window_id",open?Json(p.window->id):Json(nullptr)},{"pair_id",open?Json(p.window->pair):Json(nullptr)},
                {"authorization_generation",generation},{"action",open?Json(p.window->action):Json(nullptr)}},"window");
        }
        const auto pair=body["pair_id"];
        if(!p.live(current))return result(409,pair,p.window?"expired":"pairing_closed");
        const auto window=p.window;
        if(pair!=window->pair){p.failed(current);return result(409,pair,"identity_changed");}
        if(resource=="hello"){
            if(body["window_id"]!=window->id||body["target_id"]!=p.self||body["target_boot_id"]!=p.boot||body["controller_id"]==p.self||
                body["authorization_generation"]!=window->generation||body["action"]!=window->action){p.failed(current);return result(409,pair,"identity_changed");}
            if(window->attempt){
                if(window->attempt->hello==body)return document(window->attempt->challenge,"challenge");
                p.failed(current);return result(409,pair,"request_id_reuse");
            }
            auto attempt=std::make_shared<Impl::Attempt>();attempt->hello=body;
            attempt->challenge={{"contract","Wsprry-Pair/1"},{"schema","challenge"},{"suite",suite},{"pair_id",pair},{"target_nonce",nonce()}};
            attempt->deadline=std::min(window->deadline,current+std::chrono::seconds(30));window->attempt=attempt;
            return document(attempt->challenge,"challenge");
        }
        const auto attempt=window->attempt;if(!attempt)return result(409,pair,"invalid_proof");
        if(attempt->busy)return result(429,pair,"busy",true,"pending");
        if(resource=="exchange"){
            if(!attempt->exchange_request.is_null()){
                if(attempt->exchange_request==body)return document(attempt->exchange_response,"exchange-response");
                p.failed(current);return result(409,pair,"request_id_reuse");
            }
            if(!PiPairExchange::valid_peer_point(body["pA"].get<std::string>())){p.failed(current);return result(403,pair,"invalid_proof");}
            attempt->busy=true;const auto code=window->code;lock.unlock();
            std::unique_ptr<PiPairExchange> exchange;Json response;PiPairCryptoFailure failure=PiPairCryptoFailure::Unavailable;bool success=false;
            try{exchange=std::make_unique<PiPairExchange>(PiPairExchange::Role::Target,*code,attempt->hello,attempt->challenge);response=exchange->target_exchange(body);success=true;}
            catch(const PiPairCryptoError &error){failure=error.code();}catch(...){}
            lock.lock();const auto completed=p.now();
            if(!policy_ready()){p.window.reset();return result(403,pair,"pairing_closed");}
            if(p.window!=window||window->attempt!=attempt||!p.live(completed))return result(409,pair,"expired");
            attempt->busy=false;
            if(!success){
                if(failure==PiPairCryptoFailure::Busy)return result(429,pair,"busy",true,"pending");
                p.failed(completed);return result(failure==PiPairCryptoFailure::Unavailable?503:403,pair,
                    failure==PiPairCryptoFailure::Unavailable?"storage_unavailable":"invalid_proof");
            }
            attempt->exchange=std::move(exchange);attempt->exchange_request=body;attempt->exchange_response=response;return document(response,"exchange-response");
        }
        if(resource=="confirm"){
            if(!attempt->confirmation.is_null()){
                if(attempt->confirmation==body)return document(attempt->confirmed,"confirmed");
                p.failed(current);return result(409,pair,"request_id_reuse");
            }
            if(!attempt->exchange){p.failed(current);return result(403,pair,"invalid_proof");}
            try{attempt->confirmed=attempt->exchange->target_confirmation(body);attempt->confirmation=body;attempt->mutually_confirmed=true;return document(attempt->confirmed,"confirmed");}
            catch(...){p.failed(current);return result(403,pair,"invalid_proof");}
        }
        if(resource!="commit"||!attempt->mutually_confirmed||!attempt->exchange){p.failed(current);return result(403,pair,"invalid_proof");}
        auto keys=attempt->exchange->keys();
        if(body["request_digest"]!=attempt->exchange->request_digest()||!pi_pair_verify_mac(keys.commit,"request/"+attempt->exchange->request_digest(),body["proof"].get<std::string>())){
            p.failed(current);return result(403,pair,"invalid_proof");
        }
        const PiPairCommitApproval approval(attempt->hello,attempt->exchange->request_digest(),std::move(keys.tls),std::move(keys.retirement),p.clock,attempt->deadline,p.sessions);
        window->open=false;window->code.reset();attempt->commit_request=body;
        try{
            require(approval.valid());const auto committed=p.commit(approval);std::uint64_t expected{};
            const auto parsed=std::from_chars(window->generation.data(),window->generation.data()+window->generation.size(),expected);
            require(parsed.ec==std::errc{}&&parsed.ptr==window->generation.data()+window->generation.size()&&expected<UINT64_MAX&&
                committed.authorization_generation==std::to_string(expected+1)&&hex(committed.receipt_digest,64));
            Json response{{"contract","Wsprry-Pair/1"},{"schema","committed"},{"pair_id",pair},
                {"authorization_generation",committed.authorization_generation},{"receipt_digest",committed.receipt_digest}};
            response["proof"]=pi_pair_mac(keys.commit,"response/"+canonical_pi_management_json(response));
            attempt->commit_response=document(response,"committed");
        }catch(const PiPairCommitRefusal &refusal){
            const auto reason=refusal.reason();
            attempt->commit_response=result(reason==PiPairCommitRefusal::Reason::Unavailable?503:reason==PiPairCommitRefusal::Reason::Capacity?429:reason==PiPairCommitRefusal::Reason::Proof?403:409,pair,
                reason==PiPairCommitRefusal::Reason::Expired?"expired":reason==PiPairCommitRefusal::Reason::Generation?"generation_changed":
                reason==PiPairCommitRefusal::Reason::Capacity?"capacity":reason==PiPairCommitRefusal::Reason::Unavailable?"pairing_closed":"invalid_proof");
        }catch(...){attempt->commit_response=result(503,pair,"operation_unknown",true,"unknown");}
        attempt->exchange.reset();return *attempt->commit_response;
    }catch(...){return result(503,nullptr,"storage_unavailable");}
}
} // namespace wsprrypi
