// SPDX-License-Identifier: MIT
#ifdef _WIN32
#include "file_api.hpp"
#include "directory_lock.hpp"
#include "installation_lock.hpp"
#include "application_recovery/windows_controller_service.hpp"
#include "application_recovery/windows_backend_command.hpp"
#include "application_recovery/windows_owner_pipe.hpp"
#include "native_windows_file.hpp"
#include "native_windows_process.hpp"
#include "native_random.hpp"
#include "application_storage/windows_generation_admission.hpp"
#include "application_storage/windows_sqlite_owner_lock.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <limits>
#include <cstring>
namespace {
namespace wf=wsprrypi::windows_file;
using Journal=wsprrypi::PiManagementJournal;
static_assert(sizeof(void *)==8&&sizeof(WrnInfo)==256);
struct Status {uint32_t value;};
void require(bool valid){if(!valid)throw Status{3};}
template<class F>uint32_t status(F action)noexcept{try{action();return 0;}catch(const Status &error){return error.value;}catch(const wsprrypi::windows_generation::Busy &){return 5;}catch(const wsprrypi::WindowsSqliteOwnerLock::Busy &){return 5;}catch(const wsprrypi::recovery_windows::InstallationAdmission::Busy &){return 5;}catch(...){return 1;}}
std::string input(const char *value,size_t length){require(value&&length>0&&length<=32760);return {value,length};}
std::filesystem::path path(const char *value,size_t length){return wf::path_from_utf8(input(value,length));}
std::string text(const std::filesystem::path &value){const auto bytes=value.u8string();return {bytes.begin(),bytes.end()};}
bool native_exists(const std::filesystem::path &value){
    if(::GetFileAttributesW(wf::api_path(value).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
    const auto error=::GetLastError();wf::require(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);return false;
}
wf::Handle directory(const std::filesystem::path &value){
    wf::ParentChain parents(value,wf::current_sid());
    wf::Handle handle(::CreateFileW(wf::api_path(value).c_str(),FILE_LIST_DIRECTORY|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    wf::require(handle.get()!=INVALID_HANDLE_VALUE);(void)wf::inspect(handle.get(),true);
    wf::verify_exact_owner(handle.get(),wf::current_sid());wf::verify_security(handle.get(),wf::current_sid(),true,true);return handle;
}
void info(HANDLE handle,WrnInfo *output,size_t size){
    require(output&&size==sizeof(WrnInfo));FILE_ATTRIBUTE_TAG_INFO attributes{};
    wf::require(::GetFileInformationByHandleEx(handle,FileAttributeTagInfo,&attributes,sizeof attributes));
    const bool is_directory=(attributes.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;const auto snapshot=wf::inspect(handle,is_directory);
    wf::verify_exact_owner(handle,wf::current_sid());wf::verify_security(handle,wf::current_sid(),true,is_directory);
    wf::require(snapshot.standard.EndOfFile.QuadPart>=0);WrnInfo result;std::memset(&result,0,sizeof result);
    result.volume=snapshot.file_id.VolumeSerialNumber;std::memcpy(result.file_id,snapshot.file_id.FileId.Identifier,sizeof result.file_id);
    result.size=static_cast<uint64_t>(snapshot.standard.EndOfFile.QuadPart);result.links=snapshot.standard.NumberOfLinks;
    result.attributes=snapshot.basic.FileAttributes;result.modified_ticks=snapshot.basic.LastWriteTime.QuadPart;
    result.changed_ticks=snapshot.basic.ChangeTime.QuadPart;result.directory=is_directory?1:0;
    const auto owner=wf::file_owner_sid(handle);wf::require(owner.size()<sizeof result.owner_sid);
    for(size_t index=0;index<owner.size();++index){wf::require(static_cast<unsigned>(owner[index])<128);result.owner_sid[index]=static_cast<char>(owner[index]);}
    std::memcpy(output,&result,sizeof result);
}
struct File {
    std::filesystem::path name;
    std::unique_ptr<wf::ParentChain> parents;
    wf::Handle handle;wf::Snapshot initial{};uint64_t maximum{};uint32_t mode{};
    void check(){
        const auto current=wf::inspect(handle.get(),false);wf::verify_exact_owner(handle.get(),wf::current_sid());wf::verify_security(handle.get(),wf::current_sid(),true,false);
        wf::require(current.standard.EndOfFile.QuadPart>=0&&static_cast<uint64_t>(current.standard.EndOfFile.QuadPart)<=maximum);
        if(mode==1)wf::require(wf::same_snapshot(initial,current));
    }
};
std::mutex mutex;
std::map<uint64_t,std::shared_ptr<File>> files;
uint64_t next_token=1;
struct Prune {
    std::mutex mutex;std::filesystem::path name;std::unique_ptr<wf::ParentChain> parents;
    wf::Handle handle;wf::Snapshot initial{};
    void check(){wf::require(handle.get()!=nullptr&&handle.get()!=INVALID_HANDLE_VALUE);
        const auto current=wf::inspect(handle.get(),false);wf::verify_exact_owner(handle.get(),wf::current_sid());
        wf::verify_security(handle.get(),wf::current_sid(),true,false);wf::require(wf::same_snapshot(initial,current));}
};
std::map<uint64_t,std::shared_ptr<Prune>> prunes;
std::shared_ptr<Prune> prune(uint64_t token){std::lock_guard lock(mutex);const auto found=prunes.find(token);require(found!=prunes.end());return found->second;}
std::shared_ptr<File> file(uint64_t token){std::lock_guard lock(mutex);const auto found=files.find(token);require(found!=files.end()&&found->second);return found->second;}
class Reservation {
public:
    Reservation(){std::lock_guard lock(mutex);wf::require(files.size()<4096&&next_token!=std::numeric_limits<uint64_t>::max());id_=next_token++;files.emplace(id_,nullptr);}
    ~Reservation(){if(!committed_){std::lock_guard lock(mutex);files.erase(id_);}}
    uint64_t commit(std::shared_ptr<File> opened){std::lock_guard lock(mutex);files.at(id_)=std::move(opened);committed_=true;return id_;}
private:
    uint64_t id_{};bool committed_{};
};

using OwnerPipe=wsprrypi::recovery_windows::OwnerPipe;
std::map<uint64_t,std::shared_ptr<OwnerPipe>> owner_pipes;
std::shared_ptr<OwnerPipe> owner_pipe(uint64_t token){std::lock_guard lock(mutex);const auto found=owner_pipes.find(token);require(found!=owner_pipes.end());return found->second;}
uint64_t retain_pipe(std::shared_ptr<OwnerPipe> pipe){std::lock_guard lock(mutex);require(owner_pipes.size()<4&&next_token!=std::numeric_limits<uint64_t>::max());const auto id=next_token++;owner_pipes.emplace(id,std::move(pipe));return id;}
using BackendCommand=wsprrypi::recovery_windows::BackendCommand;
std::map<uint64_t,std::shared_ptr<BackendCommand>> backend_commands;
std::shared_ptr<BackendCommand> backend_command(uint64_t token){std::lock_guard lock(mutex);const auto found=backend_commands.find(token);require(found!=backend_commands.end());return found->second;}
using ControllerService=wsprrypi::recovery_windows::ControllerService;
std::map<uint64_t,std::shared_ptr<ControllerService>> services;
std::shared_ptr<ControllerService> service(uint64_t token){std::lock_guard lock(mutex);const auto found=services.find(token);require(found!=services.end());return found->second;}
struct Crypto {std::unique_ptr<wsprrypi::windows_process::OwnedChild> child;};
std::map<uint64_t,std::shared_ptr<Crypto>> cryptos;
std::map<uint64_t,std::shared_ptr<wsprrypi::windows_generation::Admission>> generations;
struct Database {
    std::shared_ptr<wsprrypi::windows_generation::Admission> generation;
    wsprrypi::WindowsSqliteOwnerLock owner;
    Database(std::shared_ptr<wsprrypi::windows_generation::Admission> admission,const std::filesystem::path &root)
        :generation(std::move(admission)),owner(root,false,wsprrypi::WindowsSqliteOwnerLock::Mobility::StoppedCandidate){}
};
std::map<uint64_t,std::shared_ptr<Database>> databases;
std::shared_ptr<Database> database(uint64_t token){std::lock_guard lock(mutex);const auto found=databases.find(token);require(found!=databases.end()&&found->second);return found->second;}
std::map<uint64_t,std::unique_ptr<wsprrypi::recovery_windows::DirectoryLock>> directories;
std::map<uint64_t,std::unique_ptr<wsprrypi::recovery_windows::InstallationAdmission>> installations;
std::shared_ptr<Crypto> crypto(uint64_t token){std::lock_guard lock(mutex);const auto found=cryptos.find(token);require(found!=cryptos.end()&&found->second);return found->second;}
class CryptoReservation {
public:
    CryptoReservation(){std::lock_guard lock(mutex);wf::require(cryptos.size()<4&&next_token!=std::numeric_limits<uint64_t>::max());id_=next_token++;cryptos.emplace(id_,nullptr);}
    ~CryptoReservation(){if(!committed_){std::lock_guard lock(mutex);cryptos.erase(id_);}}
    uint64_t commit(std::shared_ptr<Crypto> value){std::lock_guard lock(mutex);cryptos.at(id_)=std::move(value);committed_=true;return id_;}
private:
    uint64_t id_{};bool committed_{};
};
HANDLE stream(uint64_t value,bool pipe_only=false){
    require(value&&value!=std::numeric_limits<uint64_t>::max());const auto handle=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
    const auto type=::GetFileType(handle);
    if(type==FILE_TYPE_PIPE){DWORD flags{};wf::require(::GetNamedPipeInfo(handle,&flags,nullptr,nullptr,nullptr)&&!(flags&PIPE_TYPE_MESSAGE));}
    else{require(!pipe_only&&type==FILE_TYPE_DISK);const auto snapshot=wf::inspect(handle,false);
        wf::require(snapshot.standard.EndOfFile.QuadPart>=0&&snapshot.standard.EndOfFile.QuadPart<=int64_t{2}*1024*1024*1024);}
    wf::verify_exact_owner(handle,wf::current_sid());wf::verify_security(handle,wf::current_sid(),true,false);return handle;
}

}
uint32_t WRN_CALL wrn_version(){return 1;}
uint32_t WRN_CALL wrn_info_size(){return sizeof(WrnInfo);}
uint32_t WRN_CALL wrn_owner_attach(uint64_t handle,uint32_t pid,uint64_t *token){return status([&]{require(token&&handle&&handle!=std::numeric_limits<uint64_t>::max());*token=0;*token=retain_pipe(OwnerPipe::inherited(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handle)),pid));});}
uint32_t WRN_CALL wrn_owner_connect(uint32_t pid,uint64_t *token){return status([&]{require(token&&pid);*token=0;*token=retain_pipe(OwnerPipe::connect(pid));});}
uint32_t WRN_CALL wrn_owner_accept(uint64_t token){return status([&]{owner_pipe(token)->accept();});}
uint32_t WRN_CALL wrn_owner_disconnect(uint64_t token){return status([&]{owner_pipe(token)->disconnect();});}
uint32_t WRN_CALL wrn_owner_transfer(uint64_t token,void *bytes,size_t size,uint32_t sending,uint32_t timeout){return status([&]{require(bytes&&size>0&&size<=65536&&sending<=1&&timeout>0&&timeout<=2400000);owner_pipe(token)->transfer(bytes,size,sending!=0,std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout));});}
uint32_t WRN_CALL wrn_owner_lost(uint64_t token,uint32_t *lost){return status([&]{require(lost);*lost=owner_pipe(token)->lost()?1:0;});}
uint32_t WRN_CALL wrn_owner_close(uint64_t token){return status([&]{std::lock_guard lock(mutex);require(owner_pipes.erase(token)==1);});}
uint32_t WRN_CALL wrn_backend_start(const char *action,size_t action_size,const char *job,size_t job_size,const char *payload,size_t payload_size,uint64_t *token){return status([&]{
    require(token);*token=0;require(action_size<=32&&job_size<=32&&payload_size<=8192&&(job||job_size==0)&&(payload||payload_size==0));
    const auto operation=input(action,action_size);auto command=std::make_shared<BackendCommand>(operation,job_size?std::string(job,job_size):std::string(),payload_size?std::string(payload,payload_size):std::string());
    std::lock_guard lock(mutex);require(backend_commands.size()<4&&next_token!=std::numeric_limits<uint64_t>::max());const auto id=next_token++;backend_commands.emplace(id,std::move(command));*token=id;
});}
uint32_t WRN_CALL wrn_backend_poll(uint64_t token,void *bytes,size_t capacity,uint32_t *size,uint32_t *done,uint32_t *code){return status([&]{
    require(bytes&&capacity==8192&&size&&done&&code);*size=0;*done=0;*code=0;const auto next=backend_command(token)->poll();require(next.bytes.size()<=capacity);
    std::memcpy(bytes,next.bytes.data(),next.bytes.size());*size=static_cast<uint32_t>(next.bytes.size());*done=next.done?1:0;*code=next.code;
});}
uint32_t WRN_CALL wrn_backend_close(uint64_t token){return status([&]{std::shared_ptr<BackendCommand> command;{std::lock_guard lock(mutex);const auto found=backend_commands.find(token);require(found!=backend_commands.end());command=std::move(found->second);backend_commands.erase(found);}});}
uint32_t WRN_CALL wrn_service_open(uint64_t *token){return status([&]{
    require(token);*token=0;auto owner=std::make_shared<ControllerService>();std::lock_guard lock(mutex);
    require(services.size()<4&&next_token!=std::numeric_limits<uint64_t>::max());const auto id=next_token++;services.emplace(id,std::move(owner));*token=id;
});}
uint32_t WRN_CALL wrn_service_query(uint64_t token,uint32_t *state,uint32_t *pid){return status([&]{
    require(state&&pid);*state=0;*pid=0;const auto value=service(token)->query();*state=value.state;*pid=value.pid;
});}
uint32_t WRN_CALL wrn_service_start(uint64_t token,uint32_t mode,const char *nonce,size_t size,uint32_t *pid){return status([&]{
    require(pid&&(mode==1||mode==2));*pid=0;std::wstring value;
    if(mode==2){const auto encoded=input(nonce,size);require(encoded.size()==32&&encoded.find_first_not_of("0123456789abcdef")==std::string::npos);value.assign(encoded.begin(),encoded.end());}
    else require(!nonce&&size==0);
    const auto result=service(token)->start(mode==1?ControllerService::Start::Controller:ControllerService::Start::RecoveryOwner,value);*pid=result.pid;
});}
uint32_t WRN_CALL wrn_service_stop(uint64_t token,uint32_t pid){return status([&]{service(token)->stop(pid);});}
uint32_t WRN_CALL wrn_service_close(uint64_t token){return status([&]{std::lock_guard lock(mutex);require(services.erase(token)==1);});}
uint32_t WRN_CALL wrn_installation_acquire(const char *root,size_t length,uint64_t *token){return status([&]{
    require(token);*token=0;const auto directory=path(root,length);std::lock_guard lock(mutex);
    require(installations.size()<64&&next_token!=std::numeric_limits<uint64_t>::max());
    auto owner=std::make_unique<wsprrypi::recovery_windows::InstallationAdmission>(directory);const auto id=next_token++;
    installations.emplace(id,std::move(owner));*token=id;
});}
uint32_t WRN_CALL wrn_installation_close(uint64_t token){return status([&]{std::lock_guard lock(mutex);require(installations.erase(token)==1);});}
uint32_t WRN_CALL wrn_check_parent(const char *value,size_t length){return status([&]{wf::ParentChain parents(path(value,length),wf::current_sid());});}
uint32_t WRN_CALL wrn_path_info(const char *value,size_t length,WrnInfo *output,size_t size){return status([&]{
    const auto name=path(value,length);if(!native_exists(name))throw Status{2};wf::ParentChain parents(name,wf::current_sid());
    wf::Handle handle(::CreateFileW(wf::api_path(name).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    wf::require(handle.get()!=INVALID_HANDLE_VALUE);info(handle.get(),output,size);
});}
uint32_t WRN_CALL wrn_open(const char *value,size_t length,uint32_t mode,uint64_t maximum,uint64_t *token){return status([&]{
    require(token&&mode>=1&&mode<=3&&maximum<=uint64_t{2}*1024*1024*1024);*token=0;
    const auto name=path(value,length);const bool present=native_exists(name);
    if(mode==2&&present)throw Status{4};
    if(mode!=2&&!present)throw Status{2};
    Reservation reservation;auto opened=std::make_shared<File>();opened->name=name;opened->maximum=maximum;opened->mode=mode;
    opened->parents=std::make_unique<wf::ParentChain>(name,wf::current_sid());(void)directory(name.parent_path());
    if(mode==2)wf::flush_directory(name.parent_path(),wf::current_sid());
    wf::PrivateSecurity security(wf::current_sid());
    opened->handle=wf::Handle(::CreateFileW(wf::api_path(name).c_str(),GENERIC_READ|READ_CONTROL|(mode==1?0:GENERIC_WRITE),FILE_SHARE_READ,
        mode==2?security.attributes():nullptr,mode==2?CREATE_NEW:OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|(mode==1?FILE_FLAG_SEQUENTIAL_SCAN:FILE_FLAG_WRITE_THROUGH),nullptr));
    wf::require(opened->handle.get()!=INVALID_HANDLE_VALUE);opened->initial=wf::inspect(opened->handle.get(),false);opened->check();
    *token=reservation.commit(std::move(opened));
});}
uint32_t WRN_CALL wrn_prune_open(const char *value,size_t length,uint64_t *token){return status([&]{
    require(token);*token=0;auto lease=std::make_shared<Prune>();lease->name=path(value,length);
    lease->parents=std::make_unique<wf::ParentChain>(lease->name,wf::current_sid());(void)directory(lease->name.parent_path());
    lease->handle=wf::Handle(::CreateFileW(wf::api_path(lease->name).c_str(),GENERIC_READ|READ_CONTROL|DELETE,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(lease->handle.get()==INVALID_HANDLE_VALUE){const auto error=::GetLastError();if(error==ERROR_SHARING_VIOLATION)throw Status{5};wf::require(false);}
    lease->initial=wf::inspect(lease->handle.get(),false);lease->check();wf::require(lease->initial.standard.EndOfFile.QuadPart<=2LL*1024*1024*1024);
    std::lock_guard lock(mutex);require(prunes.size()<4096&&next_token!=std::numeric_limits<uint64_t>::max());const auto id=next_token++;prunes.emplace(id,std::move(lease));*token=id;
});}
uint32_t WRN_CALL wrn_prune_check(uint64_t token,const char *value,size_t length){return status([&]{const auto lease=prune(token);std::lock_guard lock(lease->mutex);require(lease->name==path(value,length));lease->check();});}
uint32_t WRN_CALL wrn_prune_remove(uint64_t token){return status([&]{const auto lease=prune(token);std::lock_guard lock(lease->mutex);lease->check();
    FILE_DISPOSITION_INFO disposition{TRUE};wf::require(::SetFileInformationByHandle(lease->handle.get(),FileDispositionInfo,&disposition,sizeof disposition));
    lease->handle=wf::Handle();wf::flush_directory(lease->name.parent_path(),wf::current_sid());lease->parents.reset();
});}
uint32_t WRN_CALL wrn_prune_close(uint64_t token){return status([&]{std::lock_guard lock(mutex);require(prunes.erase(token)==1);});}
uint32_t WRN_CALL wrn_duplicate(uint64_t token,uint64_t *output){return status([&]{
    require(output);*output=0;const auto opened=file(token);opened->check();HANDLE duplicate{};
    wf::require(::DuplicateHandle(::GetCurrentProcess(),opened->handle.get(),::GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS));
    *output=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(duplicate));
});}
uint32_t WRN_CALL wrn_file_info(uint64_t token,WrnInfo *output,size_t size){return status([&]{const auto opened=file(token);opened->check();info(opened->handle.get(),output,size);});}
uint32_t WRN_CALL wrn_handle_info(uint64_t handle,WrnInfo *output,size_t size){return status([&]{require(handle&&handle!=std::numeric_limits<uint64_t>::max());info(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handle)),output,size);});}
uint32_t WRN_CALL wrn_flush(uint64_t token){return status([&]{const auto opened=file(token);opened->check();require(opened->mode!=1);wf::require(::FlushFileBuffers(opened->handle.get()));wf::flush_directory(opened->name.parent_path(),wf::current_sid());});}
uint32_t WRN_CALL wrn_close(uint64_t token){return status([&]{std::shared_ptr<File> closing;{std::lock_guard lock(mutex);const auto found=files.find(token);require(found!=files.end()&&found->second);closing=std::move(found->second);files.erase(found);}});}
uint32_t WRN_CALL wrn_directory(const char *value,size_t length,uint32_t mode){return status([&]{
    require(mode<=2);const auto name=path(value,length);wf::ParentChain parents(name,wf::current_sid());
    if(native_exists(name)){(void)directory(name);if(mode==1)throw Status{4};return;}
    if(!mode)throw Status{2};
    (void)directory(name.parent_path());wf::flush_directory(name.parent_path(),wf::current_sid());
    wf::PrivateSecurity security(wf::current_sid());wf::require(::CreateDirectoryW(wf::api_path(name).c_str(),security.attributes()));
    (void)directory(name);wf::flush_directory(name.parent_path(),wf::current_sid());
});}
uint32_t WRN_CALL wrn_directory_flush(const char *value,size_t length){return status([&]{wf::flush_directory(path(value,length),wf::current_sid());});}
uint32_t WRN_CALL wrn_record_read(const char *value,size_t length,void *output,size_t capacity,uint64_t *written,char *signature,size_t signature_capacity,uint32_t *present){return status([&]{
    require(output&&capacity==1048576&&written&&signature&&signature_capacity==65&&present);*written=0;*present=0;signature[0]='\0';
    const auto record=Journal::read_private_record(text(path(value,length)));if(!record)return;
    require(record->bytes.size()<=capacity&&record->signature.size()==64);std::memcpy(output,record->bytes.data(),record->bytes.size());
    std::memcpy(signature,record->signature.data(),64);signature[64]='\0';*written=record->bytes.size();*present=1;
});}
uint32_t WRN_CALL wrn_record_publish(const char *value,size_t length,uint32_t present,const void *expected,size_t expected_size,const char *signature,size_t signature_size,const void *bytes,size_t size){return status([&]{
    require(present<=1&&expected_size<=1048576&&bytes&&size>0&&size<=1048576);std::optional<Journal::Observation> before;
    if(present){require((expected||!expected_size)&&signature&&signature_size==64);
        const std::string stamp(signature,signature_size);require(stamp.find_first_not_of("0123456789abcdef")==std::string::npos);
        before=Journal::Observation{expected_size?std::string(static_cast<const char *>(expected),expected_size):std::string{},stamp};
    }else require(expected_size==0&&signature_size==0);
    (void)Journal::publish_private_record(text(path(value,length)),before,std::string(static_cast<const char *>(bytes),size));
});}
uint32_t WRN_CALL wrn_pipe(uint64_t *reader,uint64_t *writer){return status([&]{
    require(reader&&writer&&reader!=writer);*reader=0;*writer=0;HANDLE input{},output{};
    std::array<std::uint8_t,16> random{};wsprrypi::native_random(random);
    std::wstring name=L"\\\\.\\pipe\\wsprrypi-recovery-binary-";
    for(const auto byte:random){name+=L"0123456789abcdef"[byte>>4];name+=L"0123456789abcdef"[byte&15];}
    wf::PrivateSecurity security(wf::current_sid());
    input=::CreateNamedPipeW(name.c_str(),PIPE_ACCESS_INBOUND|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,security.attributes());
    wf::require(input!=INVALID_HANDLE_VALUE);
    try{
        // The synchronous CRT endpoints need explicit read-attribute access on
        // the writer for GetNamedPipeInfo. CreatePipe's fixed generic-write
        // handle does not promise that right. Connect the private client before
        // the synchronous server call, which must report PIPE_CONNECTED.
        output=::CreateFileW(name.c_str(),GENERIC_WRITE|READ_CONTROL|FILE_READ_ATTRIBUTES,0,nullptr,OPEN_EXISTING,
            SECURITY_SQOS_PRESENT|SECURITY_ANONYMOUS,nullptr);wf::require(output!=INVALID_HANDLE_VALUE);
        const auto connected=::ConnectNamedPipe(input,nullptr);wf::require(connected||::GetLastError()==ERROR_PIPE_CONNECTED);
        ULONG client{},server{};
        wf::require(::GetNamedPipeClientProcessId(input,&client)&&::GetNamedPipeServerProcessId(output,&server)&&
            client==::GetCurrentProcessId()&&server==::GetCurrentProcessId());
        for(const auto handle:{input,output}){DWORD flags{};wf::require(::GetHandleInformation(handle,&flags)&&!(flags&HANDLE_FLAG_INHERIT));
            (void)stream(reinterpret_cast<uintptr_t>(handle),true);}
    }catch(...){::CloseHandle(input);if(output&&output!=INVALID_HANDLE_VALUE)::CloseHandle(output);throw;}
    *reader=reinterpret_cast<uintptr_t>(input);*writer=reinterpret_cast<uintptr_t>(output);
});}
uint32_t WRN_CALL wrn_generation_acquire(const char *root,size_t length,uint64_t *token){return status([&]{
    require(token);*token=0;const auto name=text(path(root,length));
    std::lock_guard lock(mutex);wf::require(generations.size()<64&&next_token!=std::numeric_limits<uint64_t>::max());
    auto admission=std::make_shared<wsprrypi::windows_generation::Admission>(name);const auto id=next_token++;
    generations.emplace(id,std::move(admission));*token=id;
});}
uint32_t WRN_CALL wrn_generation_close(uint64_t token){return status([&]{std::lock_guard lock(mutex);require(generations.erase(token)==1);});}
uint32_t WRN_CALL wrn_database_acquire(uint64_t generation,const char *root,size_t length,uint64_t *token){return status([&]{
    require(token);*token=0;const auto name=path(root,length);std::lock_guard lock(mutex);
    const auto selected=generations.find(generation);require(selected!=generations.end()&&selected->second);
    require(databases.size()<64&&next_token!=std::numeric_limits<uint64_t>::max());
    auto owned=std::make_shared<Database>(selected->second,name);const auto id=next_token++;databases.emplace(id,std::move(owned));*token=id;
});}
uint32_t WRN_CALL wrn_database_verify(uint64_t token,const char *root,size_t length){return status([&]{database(token)->owner.verify_at(path(root,length));});}
uint32_t WRN_CALL wrn_database_initialize(const char *root,size_t length){return status([&]{wsprrypi::WindowsSqliteOwnerLock::initialize_snapshot(path(root,length));});}
uint32_t WRN_CALL wrn_database_move(uint64_t token,const char *source,size_t source_size,const char *destination,size_t destination_size){return status([&]{
    const auto owned=database(token);const auto from=path(source,source_size),to=path(destination,destination_size);
    owned->owner.verify_at(from);auto original_parent=directory(from.parent_path()),parent=directory(to.parent_path());
    require(wf::normalized_path(parent.get())==wf::path_from_utf8(owned->generation->path));
    if(native_exists(to))throw Status{4};
    if(wf::inspect(original_parent.get(),true).file_id.VolumeSerialNumber!=wf::inspect(parent.get(),true).file_id.VolumeSerialNumber)
        throw Status{6};
    // Establish both directory barriers before any namespace mutation. No
    // implicit Windows copy/replace flag substitutes for the common engine.
    wf::flush_directory(from.parent_path(),wf::current_sid());wf::flush_directory(to.parent_path(),wf::current_sid());
    if(!::MoveFileExW(wf::api_path(from).c_str(),wf::api_path(to).c_str(),MOVEFILE_WRITE_THROUGH)){
        const auto error=::GetLastError();if(error==ERROR_NOT_SAME_DEVICE)throw Status{6};
        if(error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS)throw Status{4};
        wf::require(false);
    }
    owned->owner.verify_at(to);wf::flush_directory(from.parent_path(),wf::current_sid());wf::flush_directory(to.parent_path(),wf::current_sid());
});}
uint32_t WRN_CALL wrn_database_close(uint64_t token){return status([&]{
    std::shared_ptr<Database> closing;{std::lock_guard lock(mutex);const auto found=databases.find(token);require(found!=databases.end());closing=std::move(found->second);databases.erase(found);}
});}
uint32_t WRN_CALL wrn_directory_acquire(const char *root,size_t length,uint64_t *token){return status([&]{
    require(token);*token=0;const auto name=text(path(root,length));
    std::lock_guard lock(mutex);wf::require(directories.size()<64&&next_token!=std::numeric_limits<uint64_t>::max());
    auto admission=std::make_unique<wsprrypi::recovery_windows::DirectoryLock>(name);const auto id=next_token++;
    directories.emplace(id,std::move(admission));*token=id;
});}
uint32_t WRN_CALL wrn_directory_close(uint64_t token){return status([&]{std::lock_guard lock(mutex);const auto found=directories.find(token);
    require(found!=directories.end()&&found->second->current_thread());directories.erase(found);});}
uint32_t WRN_CALL wrn_crypto_start(const char *program,size_t program_size,const char *root,size_t root_size,uint32_t action,uint64_t secret,uint64_t source,uint64_t output,uint64_t *token){return status([&]{
    require(token&&(action==1||action==2));*token=0;const auto executable=path(program,program_size),working=path(root,root_size);
    require(executable.filename()==L"recovery-age.exe");wf::ParentChain parents(executable,wf::current_sid());
    wf::Handle image(::CreateFileW(wf::api_path(executable).c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    wf::require(image.get()!=INVALID_HANDLE_VALUE);const auto metadata=wf::inspect(image.get(),false);
    wf::require(metadata.standard.EndOfFile.QuadPart>0&&metadata.standard.EndOfFile.QuadPart<=16*1024*1024);
    wf::verify_security(image.get(),wf::current_sid(),false,false);
    const auto secret_pipe=stream(secret,true),input=stream(source),destination=stream(output);
    wf::Handle null(::CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    wf::require(null.get()!=INVALID_HANDLE_VALUE);CryptoReservation reservation;auto value=std::make_shared<Crypto>();
    value->child=std::make_unique<wsprrypi::windows_process::OwnedChild>(text(executable),text(working),std::array<HANDLE,3>{input,destination,null.get()},
        std::vector<HANDLE>{secret_pipe},[&](const auto &handles){require(handles.size()==1);return std::vector<std::wstring>{action==1?L"encrypt":L"decrypt",L"--secret-pipe-handle",
            std::to_wstring(reinterpret_cast<uintptr_t>(handles[0]))};});
    *token=reservation.commit(std::move(value));
});}
uint32_t WRN_CALL wrn_crypto_wait(uint64_t token,uint32_t timeout,uint32_t *code){return status([&]{require(code&&timeout>0&&timeout<=60000);const auto value=crypto(token);*code=value->child->wait(std::chrono::milliseconds(timeout));});}
uint32_t WRN_CALL wrn_crypto_close(uint64_t token){return status([&]{std::shared_ptr<Crypto> closing;{std::lock_guard lock(mutex);const auto found=cryptos.find(token);require(found!=cryptos.end()&&found->second);closing=std::move(found->second);cryptos.erase(found);}closing->child->stop();});}

#endif
