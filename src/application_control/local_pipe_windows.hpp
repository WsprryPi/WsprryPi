// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_file.hpp"
#include "local_protocol.hpp"
#include <bcrypt.h>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <limits>

namespace wsprrypi::local_pipe {
namespace wf=windows_file;
using local_protocol::Code;
using local_protocol::Deadline;
using local_protocol::require;
inline constexpr DWORD client_rights=FILE_READ_DATA|FILE_WRITE_DATA|FILE_READ_ATTRIBUTES|FILE_WRITE_ATTRIBUTES|READ_CONTROL|SYNCHRONIZE;
static_assert(!(client_rights&FILE_CREATE_PIPE_INSTANCE));
inline HANDLE raw(LocalTransportDescriptor value){return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));}
inline LocalTransportDescriptor descriptor(HANDLE value){return static_cast<LocalTransportDescriptor>(reinterpret_cast<std::uintptr_t>(value));}
[[noreturn]] inline void fatal_cancel() noexcept {
    // An uncompleted OVERLAPPED must never reference a destroyed stack/buffer.
    // This controller-only process cannot continue after a failed OS drain.
    ::TerminateProcess(::GetCurrentProcess(),ERROR_OPERATION_ABORTED);std::abort();
}
inline wf::Handle event() {
    const auto value=::CreateEventW(nullptr,TRUE,FALSE,nullptr);require(value!=nullptr,Code::Unavailable);return wf::Handle(value);
}
inline void require_primary_thread() {
    HANDLE token{};
    if(::OpenThreadToken(::GetCurrentThread(),TOKEN_QUERY,TRUE,&token)){
        ::CloseHandle(token);throw LocalControlError(Code::Unauthorized);
    }
    require(::GetLastError()==ERROR_NO_TOKEN,Code::Unauthorized);
}
class Pending {
public:
    explicit Pending(HANDLE file):file_(file),event_(event()){value_.hEvent=event_.get();}
    ~Pending(){cancel();}
    Pending(const Pending &)=delete;
    Pending &operator=(const Pending &)=delete;
    OVERLAPPED *start(){pending_=true;return &value_;}
    void completed(){pending_=false;}
    bool active() const noexcept{return pending_;}
    HANDLE event_handle() const noexcept{return event_.get();}
    DWORD wait(Deadline deadline,HANDLE stop=nullptr) {
        for(;;){
            const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
            if(remaining<=0){cancel();throw LocalControlError(Code::Unavailable);}
            const HANDLE handles[]{event_.get(),stop};
            const auto result=::WaitForMultipleObjects(stop?2:1,handles,FALSE,static_cast<DWORD>(std::min<std::int64_t>(remaining,5000)));
            if(result==WAIT_TIMEOUT)continue;
            if(result!=WAIT_OBJECT_0){cancel();throw LocalControlError(Code::Unavailable);}
            break;
        }
        DWORD count{};const auto ok=::GetOverlappedResult(file_,&value_,&count,FALSE);pending_=false;
        require(ok!=0,Code::Unavailable);return count;
    }
private:
    HANDLE file_;wf::Handle event_;OVERLAPPED value_{};bool pending_{};
    void cancel() noexcept {
        if(!pending_)return;
        (void)::CancelIoEx(file_,&value_);
        if(::WaitForSingleObject(event_.get(),2000)!=WAIT_OBJECT_0)fatal_cancel();
        DWORD count{};(void)::GetOverlappedResult(file_,&value_,&count,FALSE);pending_=false;
    }
};
inline void transfer(HANDLE pipe,void *memory,std::size_t size,bool sending,Deadline deadline,HANDLE stop=nullptr) {
    auto *bytes=static_cast<unsigned char *>(memory);
    while(size) {
        Pending pending(pipe);DWORD count{};
        const auto length=static_cast<DWORD>(std::min<std::size_t>(size,65536));
        const auto ok=sending?::WriteFile(pipe,bytes,length,&count,pending.start()):
            ::ReadFile(pipe,bytes,length,&count,pending.start());
        if(ok)pending.completed();
        else {
            const auto error=::GetLastError();
            if(error!=ERROR_IO_PENDING){pending.completed();throw LocalControlError(Code::Unavailable);}
            count=pending.wait(deadline,stop);
        }
        require(count>0&&count<=length,Code::Unavailable);bytes+=count;size-=count;
    }
}
inline bool alive(HANDLE pipe) noexcept {DWORD available{};return ::PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr)!=0;}
inline std::wstring sid_text(PSID sid) {
    require(sid&&::IsValidSid(sid),Code::Unauthorized);LPWSTR text{};
    require(::ConvertSidToStringSidW(sid,&text)!=0,Code::Unauthorized);
    struct Free {LPWSTR value;~Free(){::LocalFree(value);}} free{text};return text;
}
struct Peer {std::wstring sid;bool elevated_administrator{};};
inline Peer client_peer(HANDLE pipe) {
    require(::ImpersonateNamedPipeClient(pipe)!=0,Code::Unauthorized);
    struct Revert {~Revert(){if(!::RevertToSelf())fatal_cancel();}} revert;
    HANDLE raw_token{};
    require(::OpenThreadToken(::GetCurrentThread(),TOKEN_QUERY,TRUE,&raw_token)!=0,Code::Unauthorized);
    wf::Handle token(raw_token);SECURITY_IMPERSONATION_LEVEL level{};DWORD returned{};
    require(::GetTokenInformation(token.get(),TokenImpersonationLevel,&level,sizeof level,&returned)&&level==SecurityIdentification,Code::Unauthorized);
    auto information=[&](TOKEN_INFORMATION_CLASS kind) {
        DWORD length{};::GetTokenInformation(token.get(),kind,nullptr,0,&length);
        require(length>0&&length<=1048576,Code::Unauthorized);std::vector<unsigned char> bytes(length);
        require(::GetTokenInformation(token.get(),kind,bytes.data(),length,&length)!=0,Code::Unauthorized);return bytes;
    };
    const auto user=information(TokenUser),groups=information(TokenGroups);
    Peer result{sid_text(reinterpret_cast<const TOKEN_USER *>(user.data())->User.Sid),false};
    const auto *members=reinterpret_cast<const TOKEN_GROUPS *>(groups.data());
    require(members->GroupCount<=4096&&offsetof(TOKEN_GROUPS,Groups)+members->GroupCount*sizeof(SID_AND_ATTRIBUTES)<=groups.size(),Code::Unauthorized);
    for(DWORD index=0;index<members->GroupCount;++index) {
        const auto &entry=members->Groups[index];
        if((entry.Attributes&SE_GROUP_ENABLED)&&!(entry.Attributes&SE_GROUP_USE_FOR_DENY_ONLY)&&::IsWellKnownSid(entry.Sid,WinBuiltinAdministratorsSid))
            result.elevated_administrator=true;
    }
    return result;
}
inline LocalPeer principal(const LocalTransportOptions &options,const Peer &peer) {
    if(!options.web_sid.empty()&&peer.sid==options.web_sid&&peer.sid!=options.backend_sid)return LocalPeer::WebService;
    require(peer.sid==options.backend_sid||(options.administrator_sid&&peer.sid==*options.administrator_sid)||
        (options.allow_elevated_administrators&&peer.elevated_administrator),Code::Unauthorized);
    return LocalPeer::Administrator;
}
inline std::wstring channel(LocalChannel value) {
    switch(value){case LocalChannel::Control:return L"control";case LocalChannel::Events:return L"events";
        case LocalChannel::Browser:return L"browser";case LocalChannel::Http:return L"http";
        case LocalChannel::WebService:return L"service";case LocalChannel::Recovery:return L"recovery";}
    throw LocalControlError(Code::Protocol);
}
inline std::wstring sha256(const std::wstring &text) {
    BCRYPT_ALG_HANDLE algorithm{};require(::BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0,Code::Unavailable);
    struct Close {BCRYPT_ALG_HANDLE value;~Close(){::BCryptCloseAlgorithmProvider(value,0);}} close{algorithm};
    DWORD length{},returned{};require(::BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof length,&returned,0)==0&&
        length>0&&length<=1048576,Code::Unavailable);
    std::vector<unsigned char> object(length);BCRYPT_HASH_HANDLE hash{};
    require(::BCryptCreateHash(algorithm,&hash,object.data(),length,nullptr,0,0)==0,Code::Unavailable);
    struct Destroy {BCRYPT_HASH_HANDLE value;~Destroy(){::BCryptDestroyHash(value);}} destroy{hash};
    require(text.size()<=65536&&::BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(text.data())),
        static_cast<ULONG>(text.size()*sizeof(wchar_t)),0)==0,Code::Unavailable);
    std::array<unsigned char,32> bytes{};require(::BCryptFinishHash(hash,bytes.data(),static_cast<ULONG>(bytes.size()),0)==0,Code::Unavailable);
    std::wstring result;for(auto byte:bytes){result+=L"0123456789abcdef"[byte>>4];result+=L"0123456789abcdef"[byte&15];}return result;
}
inline void canonical_sid(const std::wstring &text) {
    PSID sid{};require(!text.empty()&&::ConvertStringSidToSidW(text.c_str(),&sid),Code::Unauthorized);
    struct Free {PSID value;~Free(){::LocalFree(value);}} free{sid};require(sid_text(sid)==text,Code::Unauthorized);
}
inline std::wstring name(const LocalTransportOptions &options) {
    canonical_sid(options.backend_sid);if(!options.web_sid.empty())canonical_sid(options.web_sid);
    if(options.administrator_sid)canonical_sid(*options.administrator_sid);
    auto path=wf::path_from_utf8(options.directory).native();
    const auto length=::LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,path.data(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr,0);
    require(length>0,Code::Unavailable);std::wstring folded(static_cast<std::size_t>(length),L'\0');
    require(::LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,path.data(),static_cast<int>(path.size()),folded.data(),length,nullptr,nullptr,0)==length,Code::Unavailable);
    return L"\\\\.\\pipe\\WsprryPi."+sha256(folded+L"|"+options.backend_sid)+L"."+channel(options.channel);
}
inline std::vector<std::wstring> clients(const LocalTransportOptions &options) {
    std::vector<std::wstring> result;
    for(const auto &value:{options.web_sid,options.administrator_sid.value_or(L""),options.allow_elevated_administrators?std::wstring(L"S-1-5-32-544"):std::wstring()})
        if(!value.empty()&&value!=options.backend_sid&&std::find(result.begin(),result.end(),value)==result.end())result.push_back(value);
    return result;
}
class Security {
public:
    Security(const std::wstring &owner,const std::vector<std::wstring> &clients) {
        canonical_sid(owner);std::wstring sddl=L"O:"+owner+L"D:P(A;;FA;;;"+owner+L")";
        for(const auto &sid:clients){canonical_sid(sid);if(sid!=owner)sddl+=L"(A;;0x00120183;;;"+sid+L")";}
        require(::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&value_,nullptr)!=0,Code::Unavailable);
        attributes_={sizeof(attributes_),value_,FALSE};
    }
    ~Security(){if(value_)::LocalFree(value_);}
    Security(const Security &)=delete;
    Security &operator=(const Security &)=delete;
    SECURITY_ATTRIBUTES *attributes(){return &attributes_;}
private:
    PSECURITY_DESCRIPTOR value_{};SECURITY_ATTRIBUTES attributes_{};
};
inline void verify(HANDLE pipe,const std::wstring &owner,const std::vector<std::wstring> &clients) {
    require(wf::file_owner_sid(pipe)==owner,Code::Unauthorized);
    PSECURITY_DESCRIPTOR descriptor{};PACL dacl{};
    const auto result=::GetSecurityInfo(pipe,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&dacl,nullptr,&descriptor);
    struct Free {PSECURITY_DESCRIPTOR value;~Free(){if(value)::LocalFree(value);}} free{descriptor};
    SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision{};
    require(result==ERROR_SUCCESS&&descriptor&&::IsValidSecurityDescriptor(descriptor)&&dacl&&::IsValidAcl(dacl)&&
        ::GetSecurityDescriptorControl(descriptor,&control,&revision)&&(control&SE_DACL_PROTECTED),Code::Unauthorized);
    for(DWORD index=0;index<dacl->AceCount;++index) {
        void *raw{};require(::GetAce(dacl,index,&raw)!=0,Code::Unauthorized);
        const auto *header=static_cast<const ACE_HEADER *>(raw);require(header->AceFlags==0,Code::Unauthorized);
        if(header->AceType==ACCESS_DENIED_ACE_TYPE)continue;
        require(header->AceType==ACCESS_ALLOWED_ACE_TYPE,Code::Unauthorized);
        const auto *ace=static_cast<const ACCESS_ALLOWED_ACE *>(raw);
        const auto sid=sid_text(reinterpret_cast<PSID>(const_cast<DWORD *>(&ace->SidStart)));
        require(sid==owner||(std::find(clients.begin(),clients.end(),sid)!=clients.end()&&!(ace->Mask&~client_rights)),Code::Unauthorized);
    }
}
inline wf::Handle create(const std::wstring &path,Security &security,unsigned count,bool first) {
    require_primary_thread();
    wf::Handle pipe(::CreateNamedPipeW(path.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|(first?FILE_FLAG_FIRST_PIPE_INSTANCE:0),
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,count,65536,65536,5000,security.attributes()));
    require(pipe.get()!=INVALID_HANDLE_VALUE,Code::Unavailable);return pipe;
}
inline void begin_connect(HANDLE pipe,Pending &pending) {
    if(::ConnectNamedPipe(pipe,pending.start())){pending.completed();return;}
    const auto error=::GetLastError();
    if(error==ERROR_PIPE_CONNECTED){pending.completed();return;}
    if(error!=ERROR_IO_PENDING){pending.completed();throw LocalControlError(Code::Unavailable);}
}
inline wf::Handle open(const std::wstring &path,Deadline deadline) {
    require_primary_thread();
    while(true) {
        wf::Handle pipe(::CreateFileW(path.c_str(),client_rights,0,nullptr,OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
        if(pipe.get()!=INVALID_HANDLE_VALUE)return pipe;
        const auto error=::GetLastError();require(error==ERROR_PIPE_BUSY,Code::Unavailable);
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
        require(remaining>0,Code::Unavailable);
        require(::WaitNamedPipeW(path.c_str(),static_cast<DWORD>(std::min<std::int64_t>(remaining,5000)))!=0,Code::Unavailable);
    }
}
struct AuthenticatedClient {Peer peer;LocalPeer principal;};
inline AuthenticatedClient authenticate_client(HANDLE pipe,const LocalTransportOptions &options,Deadline deadline,HANDLE stop) {
    std::array<unsigned char,65> preface{};transfer(pipe,preface.data(),preface.size(),false,deadline,stop);
    require(preface[0]==1,Code::Protocol);
    const std::string nonce(reinterpret_cast<char *>(preface.data()+1),32),binding(reinterpret_cast<char *>(preface.data()+33),32);
    require(local_protocol::identity(nonce)&&local_protocol::identity(binding),Code::Protocol);
    const auto peer=client_peer(pipe);const auto role=principal(options,peer);
    const auto reverse_name=name(options)+L".auth."+std::wstring(nonce.begin(),nonce.end());
    auto reverse=open(reverse_name,deadline);verify(reverse.get(),peer.sid,{options.backend_sid});
    auto bytes=binding;transfer(reverse.get(),bytes.data(),bytes.size(),true,deadline,stop);
    unsigned char accepted{};transfer(reverse.get(),&accepted,1,false,deadline,stop);require(accepted==1,Code::Unauthorized);
    return {peer,role};
}
// Keep the reverse endpoint alive until the main greeting proves that its ACK
// was consumed; DisconnectNamedPipe must not discard an unread authentication ACK.
inline wf::Handle authenticate_server(HANDLE pipe,const LocalTransportOptions &options,Deadline deadline) {
    verify(pipe,options.backend_sid,clients(options));
    const auto nonce=local_protocol::token(),binding=local_protocol::token();
    const auto reverse_name=name(options)+L".auth."+std::wstring(nonce.begin(),nonce.end());
    const auto own=wf::current_sid();Security security(own,{options.backend_sid});
    auto reverse=create(reverse_name,security,1,true);Pending connection(reverse.get());begin_connect(reverse.get(),connection);
    std::array<unsigned char,65> preface{};preface[0]=1;
    std::memcpy(preface.data()+1,nonce.data(),32);std::memcpy(preface.data()+33,binding.data(),32);
    transfer(pipe,preface.data(),preface.size(),true,deadline);
    if(connection.active())connection.wait(deadline);
    std::array<char,32> proof{};transfer(reverse.get(),proof.data(),proof.size(),false,deadline);
    require(client_peer(reverse.get()).sid==options.backend_sid&&std::string(proof.data(),proof.size())==binding,Code::Unauthorized);
    unsigned char accepted=1;transfer(reverse.get(),&accepted,1,true,deadline);return reverse;
}
} // namespace wsprrypi::local_pipe
#endif
