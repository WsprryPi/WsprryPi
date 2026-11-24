// SPDX-License-Identifier: MIT
// Explicit disposable-VM fixture. This is not an installer or product service
// entry point. Its fixed modes use the real separated service accounts and
// consume the production controller, browser service, and recovery worker.
#include "application_recovery/worker_process.hpp"
#include "application_recovery/windows_owner_pipe.hpp"
#include "application_recovery/private_json.hpp"
#include "native_windows_arguments.hpp"
#include "native_windows_process.hpp"
#include "native_windows_child_pipe.hpp"
#include <fstream>
#include <atomic>
#include <wincrypt.h>

namespace {
namespace wf=wsprrypi::windows_file;
namespace wp=wsprrypi::windows_process;
using Json=nlohmann::json;
std::atomic_bool stopping{};
SERVICE_STATUS_HANDLE status_handle{};
bool provision{},web{},http{},backend_probe{},export_probe{},preview_probe{},resume_probe{},backup_probe{},retry_preview_probe{},quiesce_probe{};
DWORD outcome=1;
DWORD failure_error{};
std::string failure_detail;
std::string step="admission";
Json evidence=Json::object();
std::string text(const std::filesystem::path &p){const auto bytes=p.u8string();return {bytes.begin(),bytes.end()};}
Json observe_record(const std::filesystem::path &path){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    for(;;){
        const char *check="parent-admission";
        try{
            const auto owner=wf::current_sid();wf::ParentChain parents(path,owner);
            check="opening-record";
            wf::Handle input(::CreateFileW(wf::api_path(path).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,
                nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
            wf::require(input.get()!=INVALID_HANDLE_VALUE);
            check="metadata-before";const auto before=wf::inspect(input.get(),false);
            check="record-security";wf::verify_exact_owner(input.get(),owner);wf::verify_security(input.get(),owner,true,false);
            check="record-bound";wf::require(before.standard.EndOfFile.QuadPart>0&&before.standard.EndOfFile.QuadPart<=1048576);
            std::vector<unsigned char> bytes(static_cast<std::size_t>(before.standard.EndOfFile.QuadPart));DWORD count{};
            check="reading-record";
            wf::require(::ReadFile(input.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size());
            check="metadata-after";wf::require(wf::same_snapshot(before,wf::inspect(input.get(),false)));
            check="security-after";wf::verify_exact_owner(input.get(),owner);wf::verify_security(input.get(),owner,true,false);
            check="strict-json";
            return wsprrypi::parse_recovery_json(std::string(bytes.begin(),bytes.end()),1048576,32);
        }catch(const std::runtime_error &){
            // Read-only observation can race record publication. Every attempt
            // repeats all native safety checks; no failed read admits an action.
            if(stopping||std::chrono::steady_clock::now()>=deadline){
                evidence["observation_failed_check"]=check;evidence["observation_record"]=text(path.filename());throw;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
}
template<class Worker> Json recovery_request(Worker &worker,const Json &value){
    const bool admission=value.at("action")=="restore.upload.start"||value.at("action")=="backup.create";
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
    for(;;){
        const auto reply=worker.request(value);
        if(reply.is_object()&&reply.size()==2&&reply.value("ok",false)&&reply.contains("value"))return reply["value"];
        // Only a definite pre-admission refusal permits retrying this request.
        // Transport uncertainty throws out of the fixture without replay.
        wf::require(admission&&reply==Json({{"ok",false},{"error","busy"}})&&!stopping&&std::chrono::steady_clock::now()<deadline);
        evidence["admission_busy_refusals"]=evidence.value("admission_busy_refusals",0u)+1;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}
void report(DWORD state,DWORD error=NO_ERROR){
    SERVICE_STATUS status{};status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;status.dwCurrentState=state;
    status.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP:0;status.dwWin32ExitCode=error;
    status.dwWaitHint=state==SERVICE_START_PENDING?60000:0;wf::require(::SetServiceStatus(status_handle,&status));
}
DWORD WINAPI control(DWORD code,DWORD,void *,void *){
    if(code==SERVICE_CONTROL_STOP){stopping=true;return NO_ERROR;}
    return code==SERVICE_CONTROL_INTERROGATE?NO_ERROR:ERROR_CALL_NOT_IMPLEMENTED;
}
void result(const std::filesystem::path &home,bool pass){
    // The preprovisioned role-private directory supplies the file's ACL.
    std::ofstream output(home/L"qualification-result.json",std::ios::binary|std::ios::trunc);
    wf::require(bool(output));output<<Json{{"schema","native-service-owner-fixture/1"},{"pass",pass},{"step",step},
        {"sid",wsprrypi::windows_argument_utf8(wf::current_sid())},{"pid",::GetCurrentProcessId()},{"win32_error",failure_error},{"detail",failure_detail},{"evidence",evidence}}.dump()<<'\n';
    output.flush();wf::require(bool(output));
}
void denied_file(const std::filesystem::path &path){
    wf::Handle denied(::CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
    wf::require(denied.get()==INVALID_HANDLE_VALUE&&::GetLastError()==ERROR_ACCESS_DENIED);
}
void frontend_denials(const wsprrypi::NativeWindowsOwners &owners){
    step="frontend-private-file-refusal";
    denied_file(wf::path_from_utf8(owners.backend.home)/L".wsprrypi-controller-identity"/L"authority.json");
    denied_file(wf::path_from_utf8(owners.maintenance.home)/L"jobs.json");
}
void probe_last_export(){
    step="retained-owner-export";const auto paths=wsprrypi::recovery_paths();
    const auto root=wf::path_from_utf8(paths.root);
    const auto read=[](const std::filesystem::path &path){const auto bytes=wf::read_protected(text(path),true,1048576);return wsprrypi::parse_recovery_json(std::string(bytes.begin(),bytes.end()),1048576,32);};
    const auto registry=read(root/L"jobs.json");wf::require(registry["active"].is_null()&&registry["jobs"].is_array()&&!registry["jobs"].empty());
    const auto id=registry["jobs"].back().get<std::string>();wf::require(wsprrypi::local_protocol::identity(id));
    const auto job=read(root/L"jobs"/id/L"job.json");
    wf::require(job["job_id"]==id&&job["kind"]=="restore"&&job["state"]=="failed"&&job["confirmation_sha256"].is_null());
    wsprrypi::recovery_windows::ControllerService service;const auto status=service.query();
    auto pipe=wsprrypi::recovery_windows::OwnerPipe::connect(status.pid);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    auto io=[&](void *bytes,std::size_t size,bool sending,auto limit){pipe->transfer(bytes,size,sending,limit);};
    const auto principal=wsprrypi::local_protocol::receive_frame(io,deadline,1024);const auto nonce=wsprrypi::local_protocol::token();const auto &owner=*paths.backend_owner;
    wf::require(principal["identity"]==job["identity"]&&principal["service_pid"]==status.pid);
    const Json context{{"schema","native-recovery-owner/1"},{"nonce",nonce},
        {"owner",{{"sid",wsprrypi::windows_argument_utf8(owner.sid)},{"home",owner.home},{"root",owner.root}}},
        {"application_root",paths.application_root},{"identity",job["identity"]},{"role","controller"},{"job_id",id},
        {"operation","export"},{"arguments",Json::object()},{"inputs",Json::array()},
        {"backend_command",paths.backend_command},{"backend_runtime",paths.backend_runtime}};
    wsprrypi::local_protocol::send_frame(io,context,deadline);
    auto ready=principal;ready["nonce"]=nonce;ready["ready"]=true;wf::require(wsprrypi::local_protocol::receive_frame(io,deadline)==ready);
    const auto response=wsprrypi::local_protocol::receive_frame(io,deadline);evidence["export_ok"]=response.value("ok",false);
    wf::require(response.value("nonce",std::string{})==nonce);
    if(response.value("ok",false)!=true)throw std::runtime_error("Retained owner export refused; inspect private code-location diagnostic");
    const auto &files=response.at("files");wf::require(files.is_array()&&files.size()<=128);std::uint64_t total{};std::array<unsigned char,65536> buffer{};
    for(const auto &file:files){const auto size=file.at("size").get<std::uint64_t>();wf::require(size<=8*1024*1024);total+=size;wf::require(total<=16*1024*1024);
        for(std::uint64_t left=size;left;){const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(left,buffer.size()));pipe->transfer(buffer.data(),count,false,deadline);left-=count;}}
    evidence["export_file_count"]=files.size();step="complete";
}
std::string base64(const unsigned char *,std::size_t);
void probe_retained_preview(bool wrong_first=false){
    step="retained-preview-admission";const auto paths=wsprrypi::recovery_paths();const auto root=wf::path_from_utf8(paths.root);
    const auto read=[](const std::filesystem::path &path){const auto bytes=wf::read_protected(text(path),true,1048576);return wsprrypi::parse_recovery_json(std::string(bytes.begin(),bytes.end()),1048576,32);};
    const auto registry=read(root/L"jobs.json");wf::require(registry["jobs"].is_array()&&!registry["jobs"].empty());
    const auto id=registry["jobs"].back().get<std::string>();wf::require(wsprrypi::local_protocol::identity(id));
    wf::require(registry["active"].is_null()||registry["active"]==id);
    const auto prior=read(root/L"jobs"/id/L"job.json");
    const auto identity=wsprrypi::recovery_windows::public_identity(true);
    wf::require(prior["job_id"]==id&&prior["kind"]=="restore"&&prior["state"]=="failed"&&prior["confirmation_sha256"].is_null()&&prior["identity"]==identity);
    wsprrypi::RecoveryWorkerProcess worker(paths,identity,"controller");
    const auto request=[&](const Json &value){return recovery_request(worker,value);};
    wf::require(request({{"action","list"}})["active"].is_null());
    // The retained failed upload remains usable after normal backup retention.
    // Re-admit its exact ciphertext through a fresh production upload.
    const auto encrypted=wf::read_protected(text(root/L"jobs"/id/L"upload.age"),true,8*1024*1024);
    wf::require(prior["archive"]["size"]==std::to_string(encrypted.size()));
    std::array<unsigned char,32> digest{};DWORD digest_size=static_cast<DWORD>(digest.size());
    wf::require(::CryptHashCertificate2(L"SHA256",0,nullptr,encrypted.data(),static_cast<DWORD>(encrypted.size()),digest.data(),&digest_size)&&digest_size==digest.size());
    std::string digest_hex;for(const auto byte:digest){digest_hex+="0123456789abcdef"[byte>>4];digest_hex+="0123456789abcdef"[byte&15];}
    wf::require(prior["archive"]["sha256"]==digest_hex);
    const auto upload=request({{"action","restore.upload.start"},{"request_id",wsprrypi::local_protocol::token()},{"size",std::to_string(encrypted.size())}});
    const auto selected=upload.at("job_id").get<std::string>();
    for(std::size_t offset=0;offset<encrypted.size();){const auto count=std::min<std::size_t>(65536,encrypted.size()-offset);
        const auto receipt=request({{"action","restore.upload.chunk"},{"job_id",selected},{"offset",std::to_string(offset)},{"bytes",base64(encrypted.data()+offset,count)}});
        offset+=count;wf::require(receipt["offset"]==std::to_string(offset));}
    (void)request({{"action","restore.upload.finish"},{"job_id",selected}});
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    for(;;){wf::require(!stopping&&std::chrono::steady_clock::now()<deadline);const auto uploaded=request({{"action","status"},{"job_id",selected}});
        if(uploaded["state"]=="uploaded"){wf::require(uploaded["archive"]["sha256"]==prior["archive"]["sha256"]);break;}
        wf::require(uploaded["state"]!="failed");std::this_thread::sleep_for(std::chrono::milliseconds(100));}
    if(wrong_first){
        step="retained-wrong-passphrase";
        (void)request({{"action","restore.validate"},{"job_id",selected},{"purpose","recover"},{"passphrase","synthetic wrong phrase"}});
        deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        for(;;){
            wf::require(!stopping&&std::chrono::steady_clock::now()<deadline);
            const auto wrong=request({{"action","status"},{"job_id",selected}});
            if(wrong["state"]=="failed"){wf::require(wrong["error"]=="archive_invalid");break;}
            wf::require(wrong["state"]!="preview");std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        evidence["wrong_passphrase_refused"]=true;
    }
    step="retained-preview-validation";
    evidence["validation_attempts"]=Json::array();unsigned attempts{};
    (void)request({{"action","restore.validate"},{"job_id",selected},{"purpose","recover"},{"passphrase","synthetic native service recovery fixture"}});
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    for(;;){
        wf::require(!stopping&&std::chrono::steady_clock::now()<deadline);const auto current=request({{"action","status"},{"job_id",selected}});
        if(current["state"]=="preview"||current["state"]=="failed"){
            ++attempts;evidence["validation_attempts"].push_back({{"state",current["state"]},{"error",current["error"]}});
            if(current["state"]=="failed"&&current["error"]=="validation_failed"&&attempts<5){
                const auto saved=read(root/L"jobs"/selected/L"job.json");
                const auto proof=read(root/L"jobs"/selected/L"validation-result.json");
                wf::require(saved["confirmation_sha256"].is_null()&&saved["backend"].is_object()&&proof.is_object()&&proof.size()==6&&
                    proof.value("schema",Json(nullptr))=="recovery-validation-result/1"&&proof.value("job_id",Json(nullptr))==selected&&
                    proof.value("backend_boot",Json(nullptr))==saved["backend"]["boot"]&&proof.value("state",Json(nullptr))=="failed"&&
                    proof.value("error",Json(nullptr))=="busy"&&proof.at("preview_digest").is_null());
                (void)request({{"action","restore.validate"},{"job_id",selected},{"purpose","recover"},{"passphrase","synthetic native service recovery fixture"}});
                continue;
            }
            evidence["validation_state"]=current["state"];evidence["validation_error"]=current["error"];
            wf::require(read(root/L"jobs"/selected/L"job.json")["confirmation_sha256"].is_null()&&current["state"]=="preview");
            const auto cancelled=request({{"action","cancel"},{"job_id",selected}});wf::require(cancelled["state"]=="failed"&&cancelled["error"]=="cancelled");step="complete";return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
void browser_service(const wsprrypi::NativeWindowsAccount &account){
    step="browser-service-launch";const auto home=wf::path_from_utf8(account.home);
    wf::Handle null(::CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));wf::require(null.get()!=INVALID_HANDLE_VALUE);
    wp::ChildPipe log(wp::ChildPipe::Direction::FromChild);
    wp::OwnedChild child(text(wsprrypi::windows_program_root()/L"bin"/L"wsprrypi.exe"),account.home,{null.get(),log.child(),log.child()},{},[](const auto &){return std::vector<std::wstring>{L"--web-service"};});
    log.close_child();report(SERVICE_RUNNING);std::ofstream output(home/L"qualification-browser-output.txt",std::ios::binary|std::ios::trunc);wf::require(bool(output));std::size_t total{};
    while(!stopping){const auto next=log.poll(std::chrono::steady_clock::now()+std::chrono::seconds(1));
        total+=next.bytes.size();wf::require(total<=1024*1024);output<<next.bytes;output.flush();wf::require(bool(output)&&child.alive()&&!next.eof);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));}
    child.stop();step="complete";
}
std::string base64(const unsigned char *bytes,std::size_t size){
    wf::require(size>0&&size<=65536);DWORD length{};constexpr DWORD flags=CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF;
    wf::require(::CryptBinaryToStringA(bytes,static_cast<DWORD>(size),flags,nullptr,&length)&&length<=87385);
    std::vector<char> output(length,'\0');wf::require(::CryptBinaryToStringA(bytes,static_cast<DWORD>(size),flags,output.data(),&length));
    const auto end=std::find(output.begin(),output.end(),'\0');wf::require(end!=output.end());return {output.begin(),end};
}
class MaintenanceBroker {
public:
    explicit MaintenanceBroker(const wsprrypi::RecoveryPaths &paths):output_(wp::ChildPipe::Direction::FromChild){
        const auto owners=wsprrypi::native_windows_controller_owners();wf::require(wf::current_sid()==owners.maintenance.sid);
        wf::Handle null(::CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
        wf::require(null.get()!=INVALID_HANDLE_VALUE);
        child_=std::make_unique<wp::OwnedChild>(text(wsprrypi::windows_program_root()/L"bin"/L"wsprrypi.exe"),owners.maintenance.home,
            std::array<HANDLE,3>{null.get(),output_.child(),output_.child()},std::vector<HANDLE>{},[](const auto &){
                return std::vector<std::wstring>{L"--maintenance-worker",L"--role",L"controller"};});
        output_.close_child();
        options_=wsprrypi::LocalTransportOptions{paths.runtime,owners.maintenance.sid,owners.web.sid,wsprrypi::LocalChannel::Recovery,owners.backend.sid,true};
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(75);
        for(;;){
            wf::require(!stopping&&std::chrono::steady_clock::now()<deadline&&alive());
            try{
                const auto ready=request({{"action","list.summary"}});
                wf::require(ready.value("ok",false)&&ready.at("value").value("schema",Json(nullptr))=="recovery-job-list/1");
                break;
            }catch(const wsprrypi::LocalControlError &error){
                wf::require(error.code()==wsprrypi::LocalControlError::Code::Unavailable);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    bool alive(){
        const auto next=output_.poll(std::chrono::steady_clock::now()+std::chrono::milliseconds(10));
        log_+=next.bytes;wf::require(log_.size()<=16384);
        if(!next.bytes.empty()){
            const auto home=wf::path_from_utf8(wsprrypi::native_windows_controller_owners().maintenance.home);
            std::ofstream log(home/L"qualification-maintenance-broker-output.txt",std::ios::binary|std::ios::trunc);
            wf::require(bool(log));log<<log_;log.flush();wf::require(bool(log));
        }
        return child_&&child_->alive()&&!next.eof;
    }
    // Recovery commands use one authenticated request per connection, like the CLI.
    Json request(const Json &value){wf::require(options_.has_value()&&alive());return wsprrypi::local_control_request(*options_,{wsprrypi::LocalOperation::RecoveryCommand,value});}
private:
    wp::ChildPipe output_;std::unique_ptr<wp::OwnedChild> child_;std::optional<wsprrypi::LocalTransportOptions> options_;std::string log_;
};
void resume_confirmed_restore(){
    const auto paths=wsprrypi::recovery_paths();const auto root=wf::path_from_utf8(paths.root);
    const auto read=[](const std::filesystem::path &path){return observe_record(path);};
    step="admit-confirmed-recovery";const auto registry=read(root/L"jobs.json");
    wf::require(registry["jobs"].is_array()&&!registry["jobs"].empty());
    const auto selected=registry["active"].is_null()?registry["jobs"].back():registry["active"];
    wf::require(wsprrypi::local_protocol::identity(selected));
    const auto id=selected.get<std::string>();const auto path=root/L"jobs"/id/L"job.json";
    const auto prior=read(path);wf::require(prior["kind"]=="restore"&&!prior["confirmation_sha256"].is_null()&&
        (prior["state"]=="applying"||prior["state"]=="awaiting_backend"||(prior["state"]=="failed"&&prior["error"]!="destination_changed")));
    wf::require(!registry["active"].is_null()||prior["state"]=="failed");
    evidence["job_id"]=id;step="automatic-confirmed-recovery";
    // Startup resumes the one durable confirmed job without replaying commands.
    MaintenanceBroker worker(paths);
    if(prior["state"]=="failed"){
        const auto resumed=worker.request({{"action","restore.resume"},{"job_id",id}});
        wf::require(resumed.value("ok",false));
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
    for(;;){
        if(stopping)throw std::runtime_error("Confirmed recovery fixture received stop");
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Confirmed recovery observation deadline expired");
        if(!worker.alive())throw std::runtime_error("Maintenance broker exited during confirmed recovery");
        const auto current=read(path);evidence["state"]=current["state"];evidence["phase"]=current["phase"];evidence["error"]=current["error"];
        if(current["state"]=="completed"){
            if(read(root/L"jobs.json")["active"]==id){std::this_thread::sleep_for(std::chrono::milliseconds(250));continue;}
            const auto completed=read(root/L"jobs"/id/L"result.json");
            wf::require(completed["state"]=="completed_held");evidence["restore_result"]=completed["state"];step="complete";return;
        }
        if(current["state"]=="failed")wf::require(read(root/L"jobs.json")["active"]==id);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
void quiesce_helper(){
    step="authenticate-idle-helper";const auto paths=wsprrypi::recovery_paths();const auto &owner=*paths.backend_owner;
    const auto identity=wsprrypi::recovery_windows::public_identity(true);
    wsprrypi::recovery_windows::ControllerService service;const auto status=service.query();
    auto pipe=wsprrypi::recovery_windows::OwnerPipe::connect(status.pid);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    auto io=[&](void *bytes,std::size_t size,bool sending,auto limit){pipe->transfer(bytes,size,sending,limit);};
    const auto principal=wsprrypi::local_protocol::receive_frame(io,deadline,1024);
    wf::require(principal.is_object()&&principal.size()==6&&principal["schema"]=="native-recovery-owner/1"&&
        principal["identity"]==identity&&principal["service_pid"]==status.pid&&principal["mode"]=="recovery-owner"&&
        principal["sid"]==wsprrypi::windows_argument_utf8(owner.sid)&&wsprrypi::local_protocol::identity(principal["helper_nonce"]));
    const auto nonce=wsprrypi::local_protocol::token();
    const Json context{{"schema","native-recovery-owner/1"},{"nonce",nonce},
        {"owner",{{"sid",wsprrypi::windows_argument_utf8(owner.sid)},{"home",owner.home},{"root",owner.root}}},
        {"application_root",paths.application_root},{"identity",identity},{"role","controller"},{"job_id",nullptr},
        {"operation","root-available"},{"arguments",Json::object()},{"inputs",Json::array()},
        {"backend_command",paths.backend_command},{"backend_runtime",paths.backend_runtime}};
    wsprrypi::local_protocol::send_frame(io,context,deadline);
    auto ready=principal;ready["nonce"]=nonce;ready["ready"]=true;wf::require(wsprrypi::local_protocol::receive_frame(io,deadline)==ready);
    const auto reply=wsprrypi::local_protocol::receive_frame(io,deadline);
    wf::require(reply==Json({{"schema","native-recovery-owner/1"},{"nonce",nonce},{"ok",true},{"value",true},{"files",Json::array()}}));
    pipe.reset();evidence["owner_idle"]=true;evidence["helper_pid"]=status.pid;
    // This closed fixture mode runs without a maintenance worker. The serial
    // owner has completed the preceding exchange and holds no active root.
    step="stop-authenticated-idle-helper";service.stop(status.pid);step="complete";
}
void exercise(bool backup_only=false){
    const auto paths=wsprrypi::recovery_paths();const auto owners=wsprrypi::native_windows_controller_owners();
    wf::require(wf::current_sid()==owners.maintenance.sid);
    step="private-file-refusal";
    const auto private_identity=wf::path_from_utf8(owners.backend.home)/L".wsprrypi-controller-identity"/L"authority.json";
    wf::Handle denied(::CreateFileW(private_identity.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
    wf::require(denied.get()==INVALID_HANDLE_VALUE&&::GetLastError()==ERROR_ACCESS_DENIED);
    step="authenticated-service-identity";
    const auto identity=wsprrypi::recovery_windows::public_identity(true);
    wf::require(wsprrypi::local_protocol::identity(identity));
    step="shared-owner-exchange";
    const auto owner_request=[&](bool changed_identity,bool delayed=false){
        wsprrypi::recovery_windows::ControllerService service;const auto status=service.query();
        auto pipe=wsprrypi::recovery_windows::OwnerPipe::connect(status.pid);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        auto io=[&](void *bytes,std::size_t size,bool sending,auto limit){pipe->transfer(bytes,size,sending,limit);};
        const auto principal=wsprrypi::local_protocol::receive_frame(io,deadline,1024);
        wf::require(principal["identity"]==identity&&principal["service_pid"]==status.pid);
        const auto nonce=wsprrypi::local_protocol::token();const auto &owner=*paths.backend_owner;auto wrong_identity=identity;wrong_identity[0]=identity[0]=='0'?'1':'0';
        Json context{{"schema","native-recovery-owner/1"},{"nonce",nonce},
            {"owner",{{"sid",wsprrypi::windows_argument_utf8(owner.sid)},{"home",owner.home},{"root",owner.root}}},
            {"application_root",paths.application_root},{"identity",changed_identity?wrong_identity:identity},
            {"role","controller"},{"job_id",nullptr},{"operation","root-available"},{"arguments",Json::object()},{"inputs",Json::array()},
            {"backend_command",paths.backend_command},{"backend_runtime",paths.backend_runtime}};
        wsprrypi::local_protocol::send_frame(io,context,deadline);
        if(delayed)::Sleep(250);
        const auto first=wsprrypi::local_protocol::receive_frame(io,deadline);
        if(changed_identity){wf::require(first["ok"]==false&&first["error"]=="unavailable");return;}
        auto ready=principal;ready["nonce"]=nonce;ready["ready"]=true;wf::require(first==ready);
        const auto response=wsprrypi::local_protocol::receive_frame(io,deadline);
        wf::require(response["ok"]==true&&response["nonce"]==nonce&&response["value"]==true&&response["files"]==Json::array());
    };
    owner_request(false);owner_request(true);step="delayed-owner-reply";owner_request(false,true);
    step="wrong-service-pid-refusal";
    bool refused=false;try{(void)wsprrypi::recovery_windows::OwnerPipe::connect(::GetCurrentProcessId());}catch(...){refused=true;}
    wf::require(refused);
    step="production-maintenance-broker";
    MaintenanceBroker worker(paths);
    const auto request=[&](const Json &value){return recovery_request(worker,value);};
    const auto jobs=request({{"action","list"}});wf::require(jobs["schema"]=="recovery-job-list/1"&&jobs["active"].is_null());
    if(!backup_only){
        step="upload-and-cancel";
        const auto job=request({{"action","restore.upload.start"},{"request_id",wsprrypi::local_protocol::token()},{"size","8"}});
        wf::require(job["state"]=="uploading");
        const auto cancelled=request({{"action","cancel"},{"job_id",job["job_id"]}});wf::require(cancelled["state"]=="failed"&&cancelled["error"]=="cancelled");
        wf::require(worker.alive());
    }
    const auto home=wf::path_from_utf8(owners.maintenance.home);
    const auto phase=[&](const char *value){step=value;std::ofstream progress(home/L"qualification-progress.json",std::ios::binary|std::ios::trunc);
        wf::require(bool(progress));progress<<Json{{"state","running"},{"step",step},{"pid",::GetCurrentProcessId()}}.dump()<<'\n';progress.flush();wf::require(bool(progress));};
    const auto await_job=[&](const Json &id,const std::vector<std::string> &terminal){
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(240);
        for(;;){wf::require(!stopping&&std::chrono::steady_clock::now()<deadline);const auto state=request({{"action","status"},{"job_id",id}});
            wf::require(state["state"].is_string());if(std::find(terminal.begin(),terminal.end(),state["state"].get<std::string>())!=terminal.end())return state;
            std::this_thread::sleep_for(std::chrono::seconds(1));}
    };
    wsprrypi::recovery_windows::ControllerService service;const auto helper=service.query();
    phase("normal-controller-start");service.stop(helper.pid);const auto running=service.start(wsprrypi::recovery_windows::ControllerService::Start::Controller);
    wsprrypi::LocalTransportOptions options{paths.backend_runtime,owners.backend.sid,owners.web.sid,wsprrypi::LocalChannel::Control,owners.maintenance.sid,true};
    const auto observe=[&]{wsprrypi::LocalControlConnection connection(options);const auto reply=connection.request({wsprrypi::LocalOperation::RecoveryObserve,Json::object()});
        wf::require(reply["ok"]==true);const auto value=reply["value"];wf::require(value["schema"]=="recovery-backend-observation/1"&&value["identity"]==identity&&value["role"]=="controller");return value;};
    Json before;const auto startup=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    const auto listeners_ready=[](const Json &value){const auto &listeners=value.at("listeners");
        return (listeners.at("http_enabled")==true?listeners.at("http_bound")==listeners.at("web_port"):listeners.at("http_bound").is_null())&&
            (listeners.at("websocket_enabled")==true?listeners.at("websocket_bound")==listeners.at("socket_port"):listeners.at("websocket_bound").is_null());};
    for(;;){wf::require(!stopping&&std::chrono::steady_clock::now()<startup);try{before=observe();if(before["ready"]==true&&listeners_ready(before))break;}catch(const wsprrypi::LocalControlError &error){wf::require(error.code()==wsprrypi::LocalControlError::Code::Unavailable);}
        std::this_thread::sleep_for(std::chrono::milliseconds(100));}
    wf::require(before["pid"]==running.pid);evidence["identity"]=identity;evidence["initial_boot"]=before["boot"];
    phase("encrypted-backup");const std::string phrase="synthetic native service recovery fixture";
    Json backup,saved;evidence["backup_attempts"]=Json::array();
    const auto capture_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
    for(unsigned attempt=0;attempt<8;++attempt){
        backup=request({{"action","backup.create"},{"request_id",wsprrypi::local_protocol::token()},{"passphrase",phrase},{"confirmation",phrase}});
        saved=await_job(backup["job_id"],{"ready","failed"});
        evidence["backup_attempts"].push_back({{"state",saved["state"]},{"error",saved["error"]}});
        if(saved["state"]=="ready"||saved["error"]!="capture_busy"||stopping||std::chrono::steady_clock::now()>=capture_deadline)break;
        // Pairing startup may still own its history after controller readiness.
        // Retry only an explicit terminal busy result, with a fresh request.
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    evidence["backup_state"]=saved["state"];evidence["backup_error"]=saved["error"];
    if(saved["state"]!="ready"){
        throw std::runtime_error("Encrypted backup did not become ready");
    }
    if(backup_only){phase("complete");return;}
    phase("encrypted-backup-read");
    const auto encrypted=wf::read_protected(text(home/L"jobs"/backup["job_id"].get<std::string>()/L"backup.age"),true,8*1024*1024);
    phase("encrypted-upload");const auto restore=request({{"action","restore.upload.start"},{"request_id",wsprrypi::local_protocol::token()},{"size",std::to_string(encrypted.size())}});
    evidence["restore_job_id"]=restore["job_id"];
    evidence["upload_chunk_bytes"]=4096;
    for(std::size_t offset=0;offset<encrypted.size();){const auto count=std::min<std::size_t>(4096,encrypted.size()-offset);
        const auto receipt=request({{"action","restore.upload.chunk"},{"job_id",restore["job_id"]},{"offset",std::to_string(offset)},{"bytes",base64(encrypted.data()+offset,count)}});
        offset+=count;wf::require(receipt["offset"]==std::to_string(offset));}
    (void)request({{"action","restore.upload.finish"},{"job_id",restore["job_id"]}});
    wf::require(await_job(restore["job_id"],{"uploaded","failed"})["state"]=="uploaded");
    phase("wrong-passphrase-refusal");(void)request({{"action","restore.validate"},{"job_id",restore["job_id"]},{"purpose","recover"},{"passphrase","synthetic wrong phrase"}});
    const auto wrong=await_job(restore["job_id"],{"preview","failed"});wf::require(wrong["state"]=="failed"&&wrong["error"]=="archive_invalid");
    phase("recovery-preview");Json preview;evidence["validation_attempts"]=Json::array();
    const auto validation_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
    for(unsigned attempt=0;attempt<8;++attempt){
        (void)request({{"action","restore.validate"},{"job_id",restore["job_id"]},{"purpose","recover"},{"passphrase",phrase}});
        preview=await_job(restore["job_id"],{"preview","failed"});
        evidence["validation_attempts"].push_back({{"state",preview["state"]},{"error",preview["error"]}});
        if(preview["state"]=="preview"||preview["error"]!="validation_failed"||stopping||std::chrono::steady_clock::now()>=validation_deadline)break;
        const auto bytes=wf::read_protected(text(home/L"jobs"/restore["job_id"].get<std::string>()/L"validation-result.json"),true,1048576);
        const auto proof=wsprrypi::parse_recovery_json(std::string(bytes.begin(),bytes.end()),1048576,32);
        if(!proof.is_object()||proof.size()!=6||proof.value("schema",Json(nullptr))!="recovery-validation-result/1"||
            proof.value("job_id",Json(nullptr))!=restore["job_id"]||proof.value("backend_boot",Json(nullptr))!=before["boot"]||
            proof.value("state",Json(nullptr))!="failed"||proof.value("error",Json(nullptr))!="busy"||!proof.at("preview_digest").is_null())break;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    wf::require(preview["state"]=="preview");
    phase("recovery-publication-and-restart");(void)request({{"action","restore.confirm"},{"job_id",restore["job_id"]},{"preview_digest",preview["preview"]["digest"]}});
    evidence["confirmation_acknowledged"]=true;phase("recovery-confirmed-awaiting-restart");
    const auto completed=await_job(restore["job_id"],{"completed","failed"});wf::require(completed["state"]=="completed"&&completed["result"]["state"]=="completed_held");
    const auto after=observe();const auto previous=before["generation"].get<std::string>();const auto generation=std::stoull(previous);
    wf::require(std::to_string(generation)==previous&&generation<std::numeric_limits<unsigned long long>::max());
    wf::require(after["boot"]!=before["boot"]&&after["generation"]==std::to_string(generation+1)&&after["ready"]==true&&listeners_ready(after));
    evidence["final_boot"]=after["boot"];evidence["generation"]=after["generation"];evidence["restore_result"]=completed["result"]["state"];phase("complete");
}
void WINAPI service_main(DWORD argc,LPWSTR *argv){
    std::filesystem::path home;
    try{
        wf::require(argc==1&&argv&&argv[0]);const auto owners=wsprrypi::native_windows_controller_owners();
        const auto account=provision||backend_probe?owners.backend:web?owners.web:http?owners.http:owners.maintenance;home=wf::path_from_utf8(account.home);
        wf::require(wf::current_sid()==account.sid);wsprrypi::recovery_windows::service_detail::unprivileged();
        const auto expected=provision||backend_probe?L"WsprryPiController":web?L"WsprryPiWeb":http?L"WsprryPiHttp":L"WsprryPiMaintenance";wf::require(std::wstring_view(argv[0])==expected);
        status_handle=::RegisterServiceCtrlHandlerExW(expected,control,nullptr);wf::require(status_handle!=nullptr);report(SERVICE_START_PENDING);
        if(backend_probe){
            step="backend-private-file-refusal";
            denied_file(wf::path_from_utf8(owners.maintenance.home)/L"jobs.json");
            denied_file(wf::path_from_utf8(owners.web.home)/L"qualification-browser-output.txt");
            denied_file(wf::path_from_utf8(owners.http.home)/L"qualification-result.json");
            step="complete";
        }else if(provision){
            step="launch-controller-initialization";
            wf::Handle null(::CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));wf::require(null.get()!=INVALID_HANDLE_VALUE);
            step="private-child-output";
            wp::ChildPipe log(wp::ChildPipe::Direction::FromChild);
            step="create-controller-process";
            wp::OwnedChild child(text(wsprrypi::windows_program_root()/L"bin"/L"wsprrypi.exe"),account.home,{null.get(),log.child(),log.child()},{},[&](const auto &){
                return std::vector<std::wstring>{L"--controller-only",L"--initialize-state",L"--state-directory",(home/L"application-state").native()};});
            log.close_child();step="initialize-controller-state";
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);std::string output;
            for(;;){wf::require(!stopping&&std::chrono::steady_clock::now()<deadline);const auto next=log.poll(deadline);
                output+=next.bytes;wf::require(output.size()<=16384);if(next.eof)break;std::this_thread::sleep_for(std::chrono::milliseconds(25));}
            std::ofstream diagnostic(home/L"initialization-output.txt",std::ios::binary|std::ios::trunc);wf::require(bool(diagnostic));diagnostic<<output;diagnostic.close();
            wf::require(child.wait(std::chrono::seconds(5))==0);step="complete";
        }else if(web){frontend_denials(owners);browser_service(account);}
        else if(http){frontend_denials(owners);step="complete";}
        else if(export_probe){report(SERVICE_RUNNING);probe_last_export();}
        else if(preview_probe||retry_preview_probe){report(SERVICE_RUNNING);probe_retained_preview(retry_preview_probe);}
        else if(resume_probe){report(SERVICE_RUNNING);resume_confirmed_restore();}
        else if(quiesce_probe){report(SERVICE_RUNNING);quiesce_helper();}
        else{report(SERVICE_RUNNING);exercise(backup_probe);}
        result(home,true);outcome=0;report(SERVICE_STOPPED);
    }catch(const std::exception &error){failure_error=::GetLastError();failure_detail=error.what();failure_detail.resize(std::min<std::size_t>(failure_detail.size(),256));
        try{if(!home.empty())result(home,false);}catch(...){}outcome=1;if(status_handle)try{report(SERVICE_STOPPED,ERROR_GEN_FAILURE);}catch(...){}}
}
}
int main(){try{
    const auto args=wsprrypi::native_windows_arguments();wf::require(args.size()==2&&(args[1]=="--provision"||args[1]=="--exercise"||args[1]=="--web"||args[1]=="--http-probe"||args[1]=="--backend-probe"||args[1]=="--export-probe"||args[1]=="--preview-probe"||args[1]=="--resume-probe"||args[1]=="--backup-probe"||args[1]=="--retry-preview-probe"||args[1]=="--quiesce-owner-probe"));
    provision=args[1]=="--provision";web=args[1]=="--web";http=args[1]=="--http-probe";backend_probe=args[1]=="--backend-probe";
    export_probe=args[1]=="--export-probe";
    preview_probe=args[1]=="--preview-probe";
    resume_probe=args[1]=="--resume-probe";
    backup_probe=args[1]=="--backup-probe";
    retry_preview_probe=args[1]=="--retry-preview-probe";
    quiesce_probe=args[1]=="--quiesce-owner-probe";
    std::array<wchar_t,32768> image{};const auto count=::GetModuleFileNameW(nullptr,image.data(),static_cast<DWORD>(image.size()));
    wf::require(count>0&&count<image.size()&&std::filesystem::path(std::wstring(image.data(),count))==wsprrypi::windows_program_root()/L"bin"/L"native_windows_service_owner_fixture.exe");
    const auto name=provision||backend_probe?L"WsprryPiController":web?L"WsprryPiWeb":http?L"WsprryPiHttp":L"WsprryPiMaintenance";
    SERVICE_TABLE_ENTRYW table[]{{const_cast<wchar_t *>(name),service_main},{nullptr,nullptr}};
    wf::require(::StartServiceCtrlDispatcherW(table));return static_cast<int>(outcome);
}catch(...){return 2;}}
