// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_tls.hpp"
#include <arpa/inet.h>
#include <array>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <chrono>
#include <iostream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace wsprrypi;
namespace {
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("pair TLS check "+std::to_string(checks));}
using Purpose=PiPairTlsPurpose;
using Channel=PiPairTlsChannel;
using Identity=PiPairTlsIdentity;
std::shared_ptr<const PiPairTlsCredential> credential(const Identity &identity,bool client,unsigned char byte=1){
    std::array<unsigned char,32> key{};key.fill(byte);
    auto secret=PiPairSecret(key);
    if(identity.purpose==Purpose::Retirement)secret=pi_pair_retirement_tls_key(secret,std::string(32,'a'),std::string(32,'b'),identity.pair_id,identity.authorization_generation);
    return std::make_shared<PiPairTlsCredential>(PiPairTlsCredential{identity,std::string(32,client?'a':'b'),std::string(32,client?'b':'a'),std::move(secret)});
}
std::pair<int,int> sockets(){
    const auto listener=socket(AF_INET,SOCK_STREAM,0);check(listener>=0);
    struct Close{int fd;~Close(){if(fd>=0)::close(fd);}} cleanup{listener};
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
    check(!bind(listener,reinterpret_cast<const sockaddr *>(&address),sizeof(address))&&!listen(listener,1));
    socklen_t length=sizeof(address);check(!getsockname(listener,reinterpret_cast<sockaddr *>(&address),&length));
    const auto client=socket(AF_INET,SOCK_STREAM,0);check(client>=0);Close client_cleanup{client};
    check(!connect(client,reinterpret_cast<const sockaddr *>(&address),sizeof(address)));
    const auto server=accept(listener,nullptr,nullptr);check(server>=0);client_cleanup.fd=-1;return {client,server};
}
bool establish(Channel &client,Channel &server){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(std::chrono::steady_clock::now()<deadline){
        const auto a=client.handshake(),b=server.handshake();
        if(a==Channel::Handshake::Failed||b==Channel::Handshake::Failed)return false;
        if(a==Channel::Handshake::Ready&&b==Channel::Handshake::Ready)return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("pair TLS fixture deadline");
}
// Independently configured OpenSSL peer deliberately violates each profile
// boundary. It never receives a production channel or a fabricated principal.
struct RawPeer {
    int fd;
    SSL_CTX *context{};
    SSL *ssl{};
    std::string identity;
    RawPeer(int socket,const Identity &id,unsigned scenario):fd(socket),identity(pi_pair_tls_identity(id)){
        context=SSL_CTX_new(TLS_client_method());check(context);
        check(SSL_CTX_set_min_proto_version(context,scenario==0?TLS1_2_VERSION:TLS1_3_VERSION)==1);
        check(SSL_CTX_set_max_proto_version(context,scenario==0?TLS1_2_VERSION:TLS1_3_VERSION)==1);
        check(SSL_CTX_set_ciphersuites(context,scenario==2?"TLS_AES_256_GCM_SHA384":"TLS_AES_128_GCM_SHA256")==1);
        check(SSL_CTX_set1_groups_list(context,(scenario==1||scenario==9)?"X25519":"P-256")==1);
        if(scenario==9)SSL_CTX_set_options(context,SSL_OP_ALLOW_NO_DHE_KEX);
        if(scenario!=3)SSL_CTX_set_psk_use_session_callback(context,[](SSL *ssl,const EVP_MD *,const unsigned char **name,std::size_t *length,SSL_SESSION **out){
            auto &self=*static_cast<RawPeer *>(SSL_get_app_data(ssl));
            const std::array<unsigned char,2> cipher{0x13,0x01};std::array<unsigned char,32> key{};key.fill(1);
            *out=SSL_SESSION_new();if(!*out)return 0;
            if(SSL_SESSION_set_protocol_version(*out,TLS1_3_VERSION)!=1||SSL_SESSION_set_cipher(*out,SSL_CIPHER_find(ssl,cipher.data()))!=1||
                SSL_SESSION_set1_master_key(*out,key.data(),key.size())!=1){SSL_SESSION_free(*out);*out=nullptr;return 0;}
            *name=reinterpret_cast<const unsigned char *>(self.identity.data());*length=self.identity.size();return 1;
        });
        ssl=SSL_new(context);check(ssl);SSL_set_app_data(ssl,this);check(SSL_set_fd(ssl,fd)==1);
        std::string offer="wsprry-management/1";std::string framed(1,static_cast<char>(offer.size()));framed+=offer;
        if(scenario==4)framed+=framed;
        if(scenario==5)identity=pi_pair_tls_identity(Identity{Purpose::Retirement,id.pair_id,id.authorization_generation,""});
        if(scenario==6)identity=std::string(129,'x');
        if(scenario==7)framed="\x03" "bad";
        check(SSL_set_alpn_protos(ssl,reinterpret_cast<const unsigned char *>(framed.data()),framed.size())==0);
        check(fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK)==0);
#ifdef SO_NOSIGPIPE
        const int yes=1;check(setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes))==0);
#endif
        SSL_set_connect_state(ssl);
    }
    ~RawPeer(){if(ssl)SSL_free(ssl);if(context)SSL_CTX_free(context);::close(fd);}
};
}
int main(){try{
    const Identity normal{Purpose::Management,std::string(32,'c'),"1","1"};
    const Identity retire{Purpose::Retirement,std::string(32,'c'),"1",""};
    for(const auto &identity:{normal,retire}){
        const auto encoded=pi_pair_tls_identity(identity);check(parse_pi_pair_tls_identity(encoded)==identity);
        for(const auto suffix:{"/","\n","/2"})check(!parse_pi_pair_tls_identity(encoded+suffix));
        check(!parse_pi_pair_tls_identity(encoded+std::string(1,'\0')));
    }
    check(!parse_pi_pair_tls_identity("Wsprry-Management/2/"+std::string(32,'c')+"/1/1"));
    check(!parse_pi_pair_tls_identity("Wsprry-Management/1/"+std::string(32,'c')+"/01/1"));
    check(!parse_pi_pair_tls_identity("Wsprry-Management/1/"+std::string(32,'c')+"/1/0"));
    check(!parse_pi_pair_tls_identity("Wsprry-Retirement/1/"+std::string(32,'c')+"/18446744073709551616"));
    check(!parse_pi_pair_tls_identity(std::string(129,'x')));
    for(const auto &identity:{normal,retire}){
        const auto [a,b]=sockets();unsigned client_lookups{},server_lookups{};
        Channel client(a,[&](const Identity &requested){++client_lookups;check(requested==identity);return credential(requested,true);},identity);
        Channel server(b,[&](const Identity &requested){++server_lookups;check(requested==identity);return credential(requested,false);});
        check(!client.principal()&&!server.principal());const auto connected=establish(client,server);
        if(!connected)std::cerr<<"TLS fixture failure categories client="<<static_cast<int>(client.failure())<<" server="<<static_cast<int>(server.failure())
            <<" reasons="<<client.library_reason()<<','<<server.library_reason()<<'\n';
        check(connected);
        check(client_lookups>=2&&server_lookups>=2);
        check(client.principal()&&server.principal()&&client.principal()->identity()==identity&&server.principal()->identity()==identity);
        check(client.principal()->peer_id()==server.principal()->self_id());
        const std::array<std::uint8_t,4> message{1,2,3,4};std::array<std::uint8_t,4> received{};
        check(client.write(message).count==message.size());
        auto result=server.read(received);for(unsigned i=0;result.state==wtp::IoState::WouldBlock&&i<100;++i){
            std::this_thread::sleep_for(std::chrono::milliseconds(1));result=server.read(received);
        }
        check(result.state==wtp::IoState::Progress&&received==message);
        server.close();check(!server.principal());bool closed=false;
        for(unsigned i=0;i<100&&!closed;++i){
            const auto result=client.write(message);closed=result.state==wtp::IoState::Failed||result.state==wtp::IoState::Closed;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(closed&&!client.principal());client.close();check(!client.principal());
    }
    // Wrong key, generation, cross-purpose owner and revocation during Finished
    // all fail without producing a server machine principal.
    {
        // A deliberately small real socket buffer forces an incomplete TLS
        // record. The channel must not call that ciphertext handoff WouldBlock.
        int pair[2]{};check(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
        Channel client(pair[0],[&](const Identity &requested){return credential(requested,true);},normal);
        Channel server(pair[1],[&](const Identity &requested){return credential(requested,false);});check(establish(client,server));
        const int small=1024;check(!setsockopt(pair[0],SOL_SOCKET,SO_SNDBUF,&small,sizeof(small)));
        const std::array<std::uint8_t,32768> bytes{};
        const auto wrote=client.write(bytes);
        check(wrote.state==wtp::IoState::Failed&&wrote.count==0&&!client.principal());
        std::array<std::uint8_t,32768> ciphertext{};
        check(recv(pair[1],ciphertext.data(),ciphertext.size(),MSG_PEEK|MSG_DONTWAIT)>0);
    }
    for(unsigned scenario=0;scenario<4;++scenario){
        const auto [a,b]=sockets();unsigned lookups{};
        Channel client(a,[&](const Identity &requested){return credential(requested,true);},scenario==2?retire:normal);
        Channel server(b,[&](const Identity &requested)->std::shared_ptr<const PiPairTlsCredential>{
            ++lookups;
            if(scenario==2&&requested.purpose!=Purpose::Management)return {};
            if(scenario==3&&lookups>1)return {};
            auto identity=requested;if(scenario==1)identity.authorization_generation="2";
            return credential(identity,false,scenario==0?2:1);
        });
        check(!establish(client,server));check(!server.principal());
    }
    for(const unsigned scenario:{0,1,2,3,4,5,6,7,9}){
        const auto [a,b]=sockets();RawPeer client(a,normal,scenario);unsigned lookups{};
        Channel server(b,[&](const Identity &requested){++lookups;return credential(requested,false);});
        auto state=Channel::Handshake::Pending;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(state==Channel::Handshake::Pending&&std::chrono::steady_clock::now()<deadline){
            (void)SSL_do_handshake(client.ssl);state=server.handshake();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(state==Channel::Handshake::Failed&&!server.principal());
        if(scenario==6)check(lookups==0);
    }
    {
        const auto [a,b]=sockets();RawPeer client(a,normal,8);
        Channel server(b,[&](const Identity &requested){return credential(requested,false);});
        auto state=Channel::Handshake::Pending;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while((state==Channel::Handshake::Pending||!SSL_is_init_finished(client.ssl))&&std::chrono::steady_clock::now()<deadline){
            (void)SSL_do_handshake(client.ssl);state=server.handshake();
            if(state==Channel::Handshake::Failed)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(state==Channel::Handshake::Ready&&SSL_is_init_finished(client.ssl)&&server.principal()&&server.principal()->identity()==normal);
        std::size_t written{};const unsigned char message=73;check(SSL_write_ex(client.ssl,&message,1,&written)==1&&written==1);
        std::array<unsigned char,1> received{};auto result=server.read(received);
        for(unsigned i=0;result.state==wtp::IoState::WouldBlock&&i<100;++i){
            std::this_thread::sleep_for(std::chrono::milliseconds(1));result=server.read(received);
        }
        check(result.state==wtp::IoState::Progress&&received[0]==message);
    }
    {
        const auto [a,b]=sockets();
        Channel client(a,[&](const Identity &requested){return credential(requested,true);},normal);
        Channel server(b,[&](const Identity &requested){return credential(requested,false);});check(establish(client,server));
        const auto start=std::chrono::steady_clock::now();auto next=start;unsigned bytes=0;bool expired=false;
        const std::array<unsigned char,1> byte{1};std::array<unsigned char,1> received{};
        while(std::chrono::steady_clock::now()-start<std::chrono::seconds(12)){
            if(std::chrono::steady_clock::now()>=next){(void)server.write(byte);next+=std::chrono::milliseconds(100);}
            const auto result=client.read(received);if(result.state==wtp::IoState::Progress)bytes+=result.count;
            if(result.state==wtp::IoState::Failed){expired=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(expired&&bytes>3&&!client.principal());
        check(std::chrono::steady_clock::now()-start>=std::chrono::milliseconds(7500));
    }
    std::cout<<"PASS actual TCP Pi external-PSK TLS profiles, I/O, Finished recheck and principal isolation: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
