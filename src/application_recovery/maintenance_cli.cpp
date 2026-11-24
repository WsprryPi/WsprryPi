// SPDX-License-Identifier: MIT
#include "maintenance_cli.hpp"
#include "maintenance_service.hpp"
#include "backup_artifact.hpp"
#include "application_control/wire_bytes.hpp"
#include <array>
#include <charconv>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <map>
#include <openssl/evp.h>
#include <openssl/rand.h>
#ifdef _WIN32
#include "maintenance_io_windows.hpp"
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#endif
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool ok){if(!ok)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
#ifndef _WIN32
struct Fd {int value{-1};~Fd(){if(value>=0)::close(value);}};
#endif
volatile std::sig_atomic_t interrupted{};
#ifndef _WIN32
void interrupt(int signal){interrupted=signal;}
#endif
bool hex(const std::string &value,std::size_t length){return value.size()==length&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string random_id(){unsigned char bytes[16];require(RAND_bytes(bytes,sizeof(bytes))==1);std::string result;for(const auto byte:bytes){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;}
int number(const std::string &value){int result{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);require(!value.empty()&&parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size()&&result>=3);return result;}
#ifndef _WIN32
std::string read_phrase(int descriptor){
    std::string result;char byte{};
    while(true){const auto count=::read(descriptor,&byte,1);if(count<0&&errno==EINTR&&!interrupted)continue;require(count>=0&&!interrupted);
        if(!count||byte=='\n')break;
        require(byte!='\0'&&result.size()<1024);
        result+=byte;
    }
    require(!result.empty());return result;
}
#endif
std::string phrase(const std::map<std::string,std::string> &options,const char *option,const char *prompt){
#ifdef _WIN32
    if(const auto found=options.find(option);found!=options.end())return maintenance_windows::phrase_fd(number(found->second));
    std::cerr<<prompt<<std::flush;auto result=maintenance_windows::phrase_console(interrupted);std::cerr<<'\n';return result;
#else
    if(const auto found=options.find(option);found!=options.end()){
        const int descriptor=number(found->second);struct stat info{};require(!::fstat(descriptor,&info));
        require(S_ISFIFO(info.st_mode)||S_ISSOCK(info.st_mode)||(S_ISREG(info.st_mode)&&info.st_nlink==1&&(info.st_mode&0077)==0&&
            (info.st_uid==0||info.st_uid==::geteuid())));
        return read_phrase(descriptor);
    }
    Fd terminal{::open("/dev/tty",O_RDWR|O_CLOEXEC)};require(terminal.value>=0&&::isatty(terminal.value));
    termios original{};require(!::tcgetattr(terminal.value,&original));auto hidden=original;hidden.c_lflag&=~ECHO;
    struct sigaction handler{},old_int{},old_term{};handler.sa_handler=interrupt;sigemptyset(&handler.sa_mask);
    require(!::sigaction(SIGINT,&handler,&old_int));
    if(::sigaction(SIGTERM,&handler,&old_term)){::sigaction(SIGINT,&old_int,nullptr);require(false);}
    struct Restore {int fd;termios original;struct sigaction interrupt,term;~Restore(){::tcsetattr(fd,TCSAFLUSH,&original);::sigaction(SIGINT,&interrupt,nullptr);::sigaction(SIGTERM,&term,nullptr);}} restore{terminal.value,original,old_int,old_term};
    require(!::tcsetattr(terminal.value,TCSAFLUSH,&hidden));std::cerr<<prompt<<std::flush;
    auto result=read_phrase(terminal.value);std::cerr<<'\n';return result;
#endif
}
#ifndef _WIN32
void write_all(int descriptor,const std::string &bytes){std::size_t offset{};while(offset<bytes.size()){const auto count=::write(descriptor,bytes.data()+offset,bytes.size()-offset);if(count<0&&errno==EINTR)continue;require(count>0);offset+=count;}}
#endif
Json command(const Json &request){auto options=maintenance_control_options();options.channel=LocalChannel::Recovery;
    const auto reply=local_control_request(options,{LocalOperation::RecoveryCommand,request});
    if(reply.at("ok")!=true){std::cerr<<reply.dump()<<'\n';throw LocalCommandRefused(LocalRefusal::InvalidRequest);}return reply.at("value");}
void download(const std::string &id,const std::string &output){
#ifdef _WIN32
    RecoveryBackupArtifact artifact(recovery_paths(),id);maintenance_windows::Export file(output);
#else
    RecoveryBackupArtifact artifact(recovery_paths(),id);const auto destination=std::filesystem::path(output);
    require(destination.is_absolute()&&destination.has_filename()&&destination.filename()!="."&&destination.filename()!="..");
    const auto parent=std::filesystem::canonical(destination.parent_path());Fd directory{::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};require(directory.value>=0);
    struct stat existing{};require(::fstatat(directory.value,destination.filename().c_str(),&existing,AT_SYMLINK_NOFOLLOW)&&errno==ENOENT);
    const auto temporary=".wsprrypi-backup-"+random_id()+".writing";
    Fd file{::openat(directory.value,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600)};require(file.value>=0);
    struct Remove {int fd;std::string name;~Remove(){::unlinkat(fd,name.c_str(),0);}} remove{directory.value,temporary};
#endif
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(),EVP_MD_CTX_free);require(digest&&EVP_DigestInit_ex(digest.get(),EVP_sha256(),nullptr)==1);
    for(std::uint64_t offset=0;offset<artifact.size();){const auto bytes=artifact.read(offset,65536);require(!bytes.empty());
#ifdef _WIN32
        file.write(bytes);
#else
        write_all(file.value,bytes);
#endif
        require(EVP_DigestUpdate(digest.get(),bytes.data(),bytes.size())==1);offset+=bytes.size();}
    std::array<unsigned char,32> bytes{};unsigned size{};require(EVP_DigestFinal_ex(digest.get(),bytes.data(),&size)==1&&size==bytes.size());std::string hash;
    for(const auto byte:bytes){hash+="0123456789abcdef"[byte>>4];hash+="0123456789abcdef"[byte&15];}
    require(hash==artifact.sha256());
#ifdef _WIN32
    file.publish(artifact.size());
#else
    require(!::fsync(file.value));
    require(!::linkat(directory.value,temporary.c_str(),directory.value,destination.filename().c_str(),0));require(!::unlinkat(directory.value,temporary.c_str(),0)&&!::fsync(directory.value));
#endif
}
Json upload(const std::string &input){
#ifdef _WIN32
    recovery_windows::Input file(windows_file::path_from_utf8(input),2ULL*1024*1024*1024,recovery_windows::Input::Access::Read,recovery_windows::Input::Policy::Credential);
    const auto size=static_cast<std::uint64_t>(file.observed().standard.EndOfFile.QuadPart);require(size>0);
#else
    Fd file{::open(input.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};struct stat before{},after{};
    require(file.value>=0&&!::fstat(file.value,&before)&&S_ISREG(before.st_mode)&&before.st_nlink==1&&before.st_size>0&&before.st_size<=2LL*1024*1024*1024);
    const auto size=static_cast<std::uint64_t>(before.st_size);
#endif
    const auto admitted=command({{"action","restore.upload.start"},{"request_id",random_id()},{"size",std::to_string(size)}});
    const auto id=admitted.at("job_id").get<std::string>();require(hex(id,32));std::array<char,65536> buffer{};std::uint64_t offset{};
    while(offset<size){
#ifdef _WIN32
        const auto count=file.read(std::span(buffer).first(static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),size-offset))));require(count>0);
#else
        const auto count=::read(file.value,buffer.data(),std::min<std::uint64_t>(buffer.size(),before.st_size-offset));if(count<0&&errno==EINTR)continue;require(count>0);
#endif
        const auto result=command({{"action","restore.upload.chunk"},{"job_id",id},{"offset",std::to_string(offset)},
            {"bytes",encode_wire_chunk(std::string(buffer.data(),static_cast<std::size_t>(count)))}});
        offset+=count;require(result.at("offset")==std::to_string(offset));
    }
#ifdef _WIN32
    file.unchanged();
#else
    require(!::fstat(file.value,&after)&&before.st_dev==after.st_dev&&before.st_ino==after.st_ino&&before.st_size==after.st_size&&before.st_mtime==after.st_mtime&&before.st_ctime==after.st_ctime);
#endif
    return command({{"action","restore.upload.finish"},{"job_id",id}});
}
}
std::optional<int> maintenance_cli_main(int argc,char *argv[]){
    bool selected=false;for(int index=1;index<argc;++index)if(std::string_view(argv[index])=="--maintenance")selected=true;if(!selected)return {};
    try{
        require(argc>=3&&std::string_view(argv[1])=="--maintenance");const std::string action=argv[2];
        if(action=="help"&&argc==3){std::cout<<"wsprrypi --maintenance jobs | status --job-id ID | backup [--passphrase-fd N --confirmation-fd M]\n"
            <<"  download --job-id ID --output /absolute/new.age | upload --file /absolute/backup.age\n"
            <<"  validate --job-id ID --purpose recover|move [--passphrase-fd N]\n"
            <<"  confirm --job-id ID --preview-digest SHA256 | resume --job-id ID | cancel --job-id ID\n"
            <<"  review | release --review-digest SHA256 [--local] [--remote] (select at least one hold)\n"
#ifdef _WIN32
            <<"Administrator-only local maintenance. Passphrases are prompted without echo or read from private FDs (3 or higher).\n"
#else
            <<"Root-only local maintenance. Passphrases are prompted without echo or read from private FDs (3 or higher).\n"
#endif
            <<"Commands return saved job state; poll status instead of replaying an unconfirmed request.\n";return 0;}
#ifdef _WIN32
        if(recovery_paths().software_backend_command.empty()){
            const auto options=maintenance_control_options();const auto own=windows_file::current_sid();
            SID_IDENTIFIER_AUTHORITY nt=SECURITY_NT_AUTHORITY;PSID administrators{};
            require(::AllocateAndInitializeSid(&nt,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&administrators));
            struct Free {PSID value;~Free(){::FreeSid(value);}} free{administrators};BOOL enabled{};require(::CheckTokenMembership(nullptr,administrators,&enabled));
            require(own==options.backend_sid||(options.administrator_sid&&own==*options.administrator_sid)||(options.allow_elevated_administrators&&enabled));
        }
#else
        require(::geteuid()==0||!recovery_paths().software_backend_command.empty());
#endif
        std::map<std::string,std::string> options;bool local=false,remote=false;
        for(int index=3;index<argc;++index){const std::string option=argv[index];
            if(option=="--local"){require(!local);local=true;}else if(option=="--remote"){require(!remote);remote=true;}
            else{require(index+1<argc&&!options.contains(option));options.emplace(option,argv[++index]);}}
        const auto allowed=[&](std::initializer_list<const char *> names){for(const auto &[key,value]:options){bool found=false;for(const auto *name:names)if(key==name)found=true;require(found);}if(action!="release")require(!local&&!remote);};
        const auto id=[&]{const auto found=options.find("--job-id");require(found!=options.end()&&hex(found->second,32));return found->second;};
        Json result;
        if(action=="jobs"){allowed({});result=command({{"action","list.summary"}});}
        else if(action=="status"||action=="cancel"||action=="resume"){allowed({"--job-id"});result=command({{"action",action=="resume"?"restore.resume":action},{"job_id",id()}});}
        else if(action=="backup"){
            allowed({"--passphrase-fd","--confirmation-fd"});require(options.contains("--passphrase-fd")==options.contains("--confirmation-fd"));
            if(options.contains("--passphrase-fd"))require(number(options.at("--passphrase-fd"))!=number(options.at("--confirmation-fd")));
            auto first=phrase(options,"--passphrase-fd","Backup passphrase: "),second=phrase(options,"--confirmation-fd","Confirm passphrase: ");require(first==second);
            result=command({{"action","backup.create"},{"request_id",random_id()},{"passphrase",first},{"confirmation",second}});std::fill(first.begin(),first.end(),'\0');std::fill(second.begin(),second.end(),'\0');
        }else if(action=="upload"){allowed({"--file"});require(options.contains("--file")&&std::filesystem::path(options.at("--file")).is_absolute());result=upload(options.at("--file"));}
        else if(action=="download"){allowed({"--job-id","--output"});require(options.contains("--output"));download(id(),options.at("--output"));result={{"downloaded",true}};}
        else if(action=="validate"){allowed({"--job-id","--purpose","--passphrase-fd"});const auto job=id();require(options.contains("--purpose")&&(options.at("--purpose")=="recover"||options.at("--purpose")=="move"));
            auto secret=phrase(options,"--passphrase-fd","Backup passphrase: ");result=command({{"action","restore.validate"},{"job_id",job},{"purpose",options.at("--purpose")},{"passphrase",secret}});std::fill(secret.begin(),secret.end(),'\0');}
        else if(action=="confirm"){allowed({"--job-id","--preview-digest"});require(options.contains("--preview-digest")&&hex(options.at("--preview-digest"),64));result=command({{"action","restore.confirm"},{"job_id",id()},{"preview_digest",options.at("--preview-digest")}});}
        else if(action=="review"){allowed({});result=command({{"action","review"},{"review",{{"action","read"}}}});}
        else if(action=="release"){allowed({"--review-digest"});require((local||remote)&&options.contains("--review-digest")&&hex(options.at("--review-digest"),64));result=command({{"action","review"},{"review",{{"action","release"},{"digest",options.at("--review-digest")},{"release_local",local},{"release_remote",remote}}}});}
        else require(false);
        std::cout<<result.dump()<<'\n';return 0;
    }catch(const LocalControlError &error){std::cerr<<error.what()<<". Read jobs or status before retrying.\n";return 2;}
    catch(...){std::cerr<<"Maintenance command refused or unavailable. Read jobs/status; passphrases and private state are not logged.\n";return interrupted?128+interrupted:2;}
}
}
