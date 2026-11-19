// SPDX-License-Identifier: MIT
#include "pi_pair_tls.hpp"
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <array>
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <fcntl.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
namespace wsprrypi {
namespace {
using Clock=std::chrono::steady_clock;
constexpr std::string_view management="Wsprry-Management/1/",retirement="Wsprry-Retirement/1/";
constexpr std::string_view session_context="Wsprry-Pair/1/external-psk";
bool id(std::string_view value){return value.size()==32&&value.find_first_not_of("0123456789abcdef")==std::string_view::npos;}
bool generation(std::string_view value){
    std::uint64_t number{};const auto result=std::from_chars(value.data(),value.data()+value.size(),number);
    return !value.empty()&&value.front()!='0'&&result.ec==std::errc{}&&result.ptr==value.data()+value.size()&&number;
}
const char *alpn(PiPairTlsPurpose purpose){
    switch(purpose){case PiPairTlsPurpose::Management:return "wsprry-management/1";
        case PiPairTlsPurpose::Retirement:return "wsprry-retirement/1";}
    throw std::invalid_argument("pairing_tls_unavailable");
}
void require(bool value){if(!value)throw std::runtime_error("pairing_tls_unavailable");}
bool valid_credential(const PiPairTlsCredential &value,const PiPairTlsIdentity &identity){
    return value.identity==identity&&id(value.self_id)&&id(value.peer_id)&&value.self_id!=value.peer_id;
}
struct SocketBio {int fd;bool wrote{};};
// Per-socket suppression preserves the application's signal policy. As in the
// existing TLS adapter, EAGAIN/EINTR is retryable and no BIO owns the descriptor.
BIO_METHOD *socket_method(){
    static BIO_METHOD *method=[] {
        auto *value=BIO_meth_new(BIO_TYPE_SOURCE_SINK,"Pi management nonblocking socket");require(value);
        require(BIO_meth_set_create(value,[](BIO *bio){BIO_set_init(bio,1);return 1;})==1&&
            BIO_meth_set_destroy(value,[](BIO *){return 1;})==1&&
            BIO_meth_set_read(value,[](BIO *bio,char *data,int size){
                BIO_clear_retry_flags(bio);
                const auto &socket=*static_cast<SocketBio *>(BIO_get_data(bio));
                const auto count=recv(socket.fd,data,size,0);
                if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))BIO_set_retry_read(bio);
                return static_cast<int>(count);
            })==1&&BIO_meth_set_write(value,[](BIO *bio,const char *data,int size){
                BIO_clear_retry_flags(bio);
#ifdef MSG_NOSIGNAL
                constexpr int flags=MSG_NOSIGNAL;
#else
                constexpr int flags=0;
#endif
                auto &socket=*static_cast<SocketBio *>(BIO_get_data(bio));
                const auto count=send(socket.fd,data,size,flags);if(count>0)socket.wrote=true;
                if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))BIO_set_retry_write(bio);
                return static_cast<int>(count);
            })==1&&BIO_meth_set_ctrl(value,[](BIO *,int command,long,void *)->long{return command==BIO_CTRL_FLUSH?1:0;})==1);
        return value;
    }();return method;
}
}
std::optional<PiPairTlsIdentity> parse_pi_pair_tls_identity(std::string_view value){
    if(value.size()>128)return {};
    PiPairTlsPurpose purpose;
    if(value.starts_with(management)){purpose=PiPairTlsPurpose::Management;value.remove_prefix(management.size());}
    else if(value.starts_with(retirement)){purpose=PiPairTlsPurpose::Retirement;value.remove_prefix(retirement.size());}
    else return {};
    const auto slash=value.find('/');if(slash==std::string_view::npos||!id(value.substr(0,slash)))return {};
    PiPairTlsIdentity result{purpose,std::string(value.substr(0,slash)),{}, {}};value.remove_prefix(slash+1);
    if(purpose==PiPairTlsPurpose::Management){
        const auto next=value.find('/');if(next==std::string_view::npos||!generation(value.substr(next+1)))return {};
        result.epoch=value.substr(next+1);value=value.substr(0,next);
    }
    if(!generation(value))return {};
    result.authorization_generation=value;return result;
}
std::string pi_pair_tls_identity(const PiPairTlsIdentity &value){
    const auto encoded=std::string(value.purpose==PiPairTlsPurpose::Management?management:retirement)+value.pair_id+"/"+
        value.authorization_generation+(value.purpose==PiPairTlsPurpose::Management?"/"+value.epoch:"");
    const auto parsed=parse_pi_pair_tls_identity(encoded);if(!parsed||*parsed!=value)throw std::invalid_argument("pairing_tls_unavailable");
    return encoded;
}
struct PiPairTlsChannel::Impl {
    int fd;
    SocketBio socket_bio;
    Lookup lookup;
    SSL_CTX *context{};
    SSL *ssl{};
    std::optional<PiPairTlsIdentity> client;
    std::optional<PiPairTlsPurpose> selected;
    std::shared_ptr<const PiPairTlsCredential> credential;
    std::string encoded;
    Handshake state{Handshake::Pending};
    const Clock::time_point born{Clock::now()};
    Clock::time_point io_deadline{born+std::chrono::seconds(24)};
    Impl(int socket,Lookup owner,std::optional<PiPairTlsIdentity> outgoing):fd(socket),socket_bio{socket,false},lookup(std::move(owner)),client(std::move(outgoing)){}
    ~Impl(){if(ssl)SSL_free(ssl);if(context)SSL_CTX_free(context);if(fd>=0)::close(fd);}
    static Impl &owner(SSL *ssl){return *static_cast<Impl *>(SSL_get_app_data(ssl));}
    static SSL_SESSION *session(SSL *ssl,const PiPairTlsCredential &credential){
        static constexpr std::array<unsigned char,2> cipher{0x13,0x01};
        const auto selected=SSL_CIPHER_find(ssl,cipher.data());if(!selected)return nullptr;
        auto value=SSL_SESSION_new();if(!value)return nullptr;
        if(SSL_SESSION_set_protocol_version(value,TLS1_3_VERSION)!=1||SSL_SESSION_set_cipher(value,selected)!=1||
            SSL_SESSION_set1_id_context(value,reinterpret_cast<const unsigned char *>(session_context.data()),session_context.size())!=1||
            SSL_SESSION_set1_master_key(value,credential.key.bytes().data(),credential.key.bytes().size())!=1){SSL_SESSION_free(value);return nullptr;}
        return value;
    }
    static int use(SSL *ssl,const EVP_MD *md,const unsigned char **identity,std::size_t *length,SSL_SESSION **out) noexcept {
        *out=nullptr;
        try{
            auto &self=owner(ssl);if(!self.client||(md&&!EVP_MD_is_a(md,"SHA256")))return 0;
            self.credential=self.lookup(*self.client);if(!self.credential||!valid_credential(*self.credential,*self.client))return 0;
            // The offered identity is immutable for this channel. Retain its
            // original storage even if OpenSSL calls again after HelloRetry.
            *out=session(ssl,*self.credential);if(!*out)return 0;
            *identity=reinterpret_cast<const unsigned char *>(self.encoded.data());*length=self.encoded.size();return 1;
        }catch(...){return 0;}
    }
    static int find(SSL *ssl,const unsigned char *identity,std::size_t length,SSL_SESSION **out) noexcept {
        *out=nullptr;
        try{
            auto &self=owner(ssl);const auto parsed=parse_pi_pair_tls_identity({reinterpret_cast<const char *>(identity),length});
            if(!parsed||(self.selected&&*self.selected!=parsed->purpose))return 0;
            self.credential=self.lookup(*parsed);if(!self.credential||!valid_credential(*self.credential,*parsed))return 0;
            *out=session(ssl,*self.credential);return *out?1:0;
        }catch(...){return 0;}
    }
    static int select(SSL *ssl,const unsigned char **out,unsigned char *length,
        const unsigned char *in,unsigned int size,void *) noexcept {
        if(size<2||static_cast<unsigned int>(in[0])+1!=size)return SSL_TLSEXT_ERR_ALERT_FATAL;
        const std::string_view offer(reinterpret_cast<const char *>(in+1),size-1);
        auto &self=owner(ssl);
        if(offer==alpn(PiPairTlsPurpose::Management))self.selected=PiPairTlsPurpose::Management;
        else if(offer==alpn(PiPairTlsPurpose::Retirement))self.selected=PiPairTlsPurpose::Retirement;
        else return SSL_TLSEXT_ERR_ALERT_FATAL;
        if(self.credential&&self.credential->identity.purpose!=*self.selected)return SSL_TLSEXT_ERR_ALERT_FATAL;
        *out=in+1;*length=in[0];return SSL_TLSEXT_ERR_OK;
    }
    void prepare(){
        require(fd>=0&&bool(lookup));const auto flags=fcntl(fd,F_GETFL);require(flags>=0&&fcntl(fd,F_SETFL,flags|O_NONBLOCK)==0);
        const auto descriptor_flags=fcntl(fd,F_GETFD);require(descriptor_flags>=0&&!fcntl(fd,F_SETFD,descriptor_flags|FD_CLOEXEC));
#ifdef SO_NOSIGPIPE
        const int yes=1;require(setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes))==0);
#endif
        context=SSL_CTX_new(TLS_method());require(context);
        require(SSL_CTX_set_min_proto_version(context,TLS1_3_VERSION)==1&&SSL_CTX_set_max_proto_version(context,TLS1_3_VERSION)==1&&
            SSL_CTX_set_ciphersuites(context,"TLS_AES_128_GCM_SHA256")==1&&SSL_CTX_set1_groups_list(context,"P-256")==1);
        SSL_CTX_set_options(context,SSL_OP_NO_TICKET);SSL_CTX_clear_options(context,SSL_OP_ALLOW_NO_DHE_KEX);
        SSL_CTX_set_session_cache_mode(context,SSL_SESS_CACHE_OFF);
        // OpenSSL 3.0 also checks a local session-id context for external PSK
        // when VERIFY_PEER is set. Bind that library metadata on both the
        // context and fresh callback session; caches/tickets remain disabled.
        require(SSL_CTX_set_session_id_context(context,reinterpret_cast<const unsigned char *>(session_context.data()),session_context.size())==1);
        require(SSL_CTX_set_num_tickets(context,0)==1&&SSL_CTX_set_max_early_data(context,0)==1);
        SSL_CTX_set_verify(context,SSL_VERIFY_PEER,nullptr);
        SSL_CTX_set_psk_use_session_callback(context,use);SSL_CTX_set_psk_find_session_callback(context,find);
        SSL_CTX_set_alpn_select_cb(context,select,nullptr);
        ssl=SSL_new(context);require(ssl);SSL_set_app_data(ssl,this);
        SSL_set_mode(ssl,SSL_MODE_ENABLE_PARTIAL_WRITE);
        auto *bio=BIO_new(socket_method());require(bio);
        BIO_set_data(bio,&socket_bio);SSL_set_bio(ssl,bio,bio);
        if(client){
            encoded=pi_pair_tls_identity(*client);selected=client->purpose;
            const std::string offer=alpn(client->purpose);std::string framed(1,static_cast<char>(offer.size()));framed+=offer;
            require(SSL_set_alpn_protos(ssl,reinterpret_cast<const unsigned char *>(framed.data()),framed.size())==0);SSL_set_connect_state(ssl);
        }else SSL_set_accept_state(ssl);
    }
    Failure admitted(){
        if(!credential||SSL_version(ssl)!=TLS1_3_VERSION||SSL_session_reused(ssl)!=1)return Failure::Authentication;
        if(!selected||credential->identity.purpose!=*selected)return Failure::Purpose;
        if(SSL_get_negotiated_group(ssl)!=NID_X9_62_prime256v1)return Failure::KeyExchange;
        if(SSL_get_early_data_status(ssl)!=SSL_EARLY_DATA_NOT_SENT)return Failure::EarlyData;
        const auto cipher=SSL_get_current_cipher(ssl);if(!cipher||SSL_CIPHER_get_protocol_id(cipher)!=0x1301)return Failure::Cipher;
        const unsigned char *protocol{};unsigned int size{};SSL_get0_alpn_selected(ssl,&protocol,&size);
        if(!size||std::string_view(reinterpret_cast<const char *>(protocol),size)!=alpn(*selected))return Failure::Purpose;
        const auto latest=lookup(credential->identity);
        return latest&&valid_credential(*latest,credential->identity)&&latest->self_id==credential->self_id&&latest->peer_id==credential->peer_id&&
            CRYPTO_memcmp(latest->key.bytes().data(),credential->key.bytes().data(),32)==0?Failure::None:Failure::Authorization;
    }
    wtp::IoResult io(std::span<std::uint8_t> output,std::span<const std::uint8_t> input,bool writing){
        if(state!=Handshake::Ready||Clock::now()>=io_deadline){state=Handshake::Failed;return {wtp::IoState::Failed,0};}
        if((writing?input.size():output.size())==0)return {wtp::IoState::WouldBlock,0};
        ERR_clear_error();std::size_t count{};socket_bio.wrote=false;
        const auto result=writing?SSL_write_ex(ssl,input.data(),input.size(),&count):SSL_read_ex(ssl,output.data(),output.size(),&count);
        if(result==1&&count)return {wtp::IoState::Progress,count};
        const auto error=SSL_get_error(ssl,result);
        if(error==SSL_ERROR_WANT_READ||error==SSL_ERROR_WANT_WRITE){
            if(!writing||!socket_bio.wrote)return {wtp::IoState::WouldBlock,0};
            // ByteStream WouldBlock promises no handoff. OpenSSL can instead
            // have partially sent a record before WANT_WRITE. That outcome is
            // uncertain, so terminate this exchange rather than report a
            // reversible no-progress observation or retry different bytes.
            state=Handshake::Failed;return {wtp::IoState::Failed,0};
        }
        state=Handshake::Failed;return {error==SSL_ERROR_ZERO_RETURN?wtp::IoState::Closed:wtp::IoState::Failed,0};
    }
};
PiPairTlsChannel::PiPairTlsChannel(int fd,Lookup lookup,std::optional<PiPairTlsIdentity> client)
    :impl_(nullptr){
    struct Guard{int fd;~Guard(){if(fd>=0)::close(fd);}} guard{fd};
    impl_=std::make_unique<Impl>(fd,std::move(lookup),std::move(client));guard.fd=-1;impl_->prepare();
}
PiPairTlsChannel::~PiPairTlsChannel()=default;
PiPairTlsChannel::Handshake PiPairTlsChannel::handshake(){
    if(!impl_)return Handshake::Failed;
    auto &self=*impl_;if(self.state!=Handshake::Pending)return self.state;
    if(Clock::now()-self.born>=std::chrono::seconds(10)){failure_=Failure::Deadline;close();return Handshake::Failed;}
    ERR_clear_error();const auto result=SSL_do_handshake(self.ssl);
    if(result!=1){const auto error=SSL_get_error(self.ssl,result);
        if(error==SSL_ERROR_WANT_READ||error==SSL_ERROR_WANT_WRITE)return Handshake::Pending;
        library_reason_=ERR_GET_REASON(ERR_peek_last_error());
        failure_=Failure::Transport;close();return Handshake::Failed;
    }
    try{
        failure_=self.admitted();if(failure_!=Failure::None){close();return Handshake::Failed;}
        principal_=PiPairPrincipal(self.credential->identity,self.credential->self_id,self.credential->peer_id);
        self.io_deadline=std::min(self.born+std::chrono::seconds(24),Clock::now()+std::chrono::seconds(8));
        self.state=Handshake::Ready;return self.state;
    }catch(...){failure_=Failure::Authorization;close();return Handshake::Failed;}
}
const PiPairPrincipal *PiPairTlsChannel::principal() const noexcept{return impl_&&impl_->state==Handshake::Ready&&principal_?&*principal_:nullptr;}
wtp::IoResult PiPairTlsChannel::read(std::span<std::uint8_t> value){if(!impl_)return {wtp::IoState::Closed,0};return impl_->io(value,{},false);}
wtp::IoResult PiPairTlsChannel::write(std::span<const std::uint8_t> value){if(!impl_)return {wtp::IoState::Closed,0};return impl_->io({},value,true);}
void PiPairTlsChannel::close() noexcept{principal_.reset();impl_.reset();}
} // namespace wsprrypi
