// SPDX-License-Identifier: MIT
#include "pi_pair_guarded_http.hpp"
#include "management_resolver.hpp"
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <stdexcept>
#include <sstream>
namespace wsprrypi {
namespace {
using Time=std::chrono::steady_clock::time_point;
namespace ns=native_socket;
void require(bool value){if(!value)throw std::runtime_error("guarded_http_unavailable");}
std::pair<std::string,int> endpoint(ns::Handle fd,bool remote){
    sockaddr_storage address{};ns::Length length=sizeof(address);require(!(remote?getpeername(fd,reinterpret_cast<sockaddr *>(&address),&length):getsockname(fd,reinterpret_cast<sockaddr *>(&address),&length)));
    std::array<char,INET6_ADDRSTRLEN> text{};
    if(address.ss_family==AF_INET){const auto &v4=reinterpret_cast<const sockaddr_in &>(address);require(inet_ntop(AF_INET,&v4.sin_addr,text.data(),text.size()));return {text.data(),ntohs(v4.sin_port)};}
    require(address.ss_family==AF_INET6);const auto &v6=reinterpret_cast<const sockaddr_in6 &>(address);require(inet_ntop(AF_INET6,&v6.sin6_addr,text.data(),text.size()));return {text.data(),ntohs(v6.sin6_port)};
}
bool private_peer(const std::string &text){in_addr a{};in6_addr b{};return private_management_address(text,{})||
    (inet_pton(AF_INET,text.c_str(),&a)==1&&(ntohl(a.s_addr)>>24)==127)||(inet_pton(AF_INET6,text.c_str(),&b)==1&&IN6_IS_ADDR_LOOPBACK(&b));}
bool guarded_path(const std::string &path){return path.starts_with("/api/pair/")||path=="/api/control/v1/host/pairing"||
    path.starts_with("/api/control/v1/host/pairing/")||path=="/api/control/v1/targets"||path.starts_with("/api/control/v1/targets/");}
// Classification only, never route authority. Match the library's one-pass
// percent decoding so encoded prefixes cannot escape the absolute deadline.
std::string decoded_path(const std::string &target){
    const auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
    std::string result;
    for(std::size_t i=0;i<target.size()&&target[i]!='?';++i){
        if(target[i]=='%'&&i+2<target.size()&&hex(target[i+1])>=0&&hex(target[i+2])>=0){result+=static_cast<char>(hex(target[i+1])*16+hex(target[i+2]));i+=2;}
        else result+=target[i];
    }
    return result;
}
class GuardedStream final:public httplib::Stream {
public:
    GuardedStream(ns::Handle fd,const std::atomic<socket_t> &listener,PiPairAdmission &budget,PiPairGuardedHttpServer::Clock clock,
        time_t read_sec,time_t read_usec,time_t write_sec,time_t write_usec)
        :fd_(fd),listener_(listener),budget_(budget),clock_(std::move(clock)),read_wait_(std::chrono::seconds(read_sec)+std::chrono::microseconds(read_usec)),
         write_wait_(std::chrono::seconds(write_sec)+std::chrono::microseconds(write_usec)),born_(clock_()),last_(born_),remote_(endpoint(fd,true)),local_(endpoint(fd,false)){}
    ~GuardedStream() override{if(connection_)connection_->cancel();}
    void next(){line_.clear();header_.clear();classified_=false;guarded_=false;headers_done_=false;writing_=false;body_=0;request_=clock_();}
    bool guarded() const noexcept{return guarded_;}
    bool is_readable() const override{return begin_<end_&&live();}
    bool wait_readable() const override{return is_readable()||wait(ns::readable,read_wait_);}
    bool wait_writable() const override{return wait(ns::writable,write_wait_);}
    ssize_t read(char *output,std::size_t length) override{
        try{
            if(!length)return 0;
            if(begin_==end_){
                if(!wait_readable())return -1;
                const auto count=ns::receive(fd_,buffer_.data(),buffer_.size());
                if(count<=0)return count;
                begin_=0;end_=static_cast<std::size_t>(count);
            }
            require(live());const auto count=std::min(length,end_-begin_);
            // Inspect before handing bytes to the library. Secret work is not
            // admitted merely by accepting the TCP descriptor.
            for(std::size_t i=0;i<count;++i)observe(buffer_[begin_+i]);
            std::memcpy(output,buffer_.data()+begin_,count);begin_+=count;return static_cast<ssize_t>(count);
        }catch(...){failed_=true;return -1;}
    }
    ssize_t write(const char *input,std::size_t length) override{
        if(!length)return 0;
        if(guarded_&&!writing_){writing_=true;deadline_=std::min(request_+std::chrono::seconds(24),clock_()+std::chrono::seconds(8));}
        if(!wait_writable())return -1;
        return ns::send(fd_,input,length);
    }
    void get_remote_ip_and_port(std::string &ip,int &port) const override{ip=remote_.first;port=remote_.second;}
    void get_local_ip_and_port(std::string &ip,int &port) const override{ip=local_.first;port=local_.second;}
    socket_t socket() const override{return fd_;}
    time_t duration() const override{return std::chrono::duration_cast<std::chrono::milliseconds>(clock_()-born_).count();}
private:
    bool live() const{
        const auto now=clock_();if(now<last_){failed_=true;return false;}last_=now;
        return !failed_&&listener_.load()!=ns::invalid&&(!connection_||!connection_->cancelled())&&
            (classified_?(!guarded_||now<deadline_):now-request_<std::chrono::seconds(8));
    }
    bool wait(short event,std::chrono::microseconds timeout) const{
        const auto end=clock_()+timeout;
        while(live()&&clock_()<end){ns::PollItem source{fd_,event,0};const auto count=ns::poll(&source,1,2);
            if(count>0)return live()&&(source.revents&(event|ns::failed));
            if(count<0&&!ns::interrupted(ns::error()))return false;}
        return false;
    }
    void observe(char byte){
        if(!classified_){
            line_+=byte;require(line_.size()<=8192);
            if(byte=='\n'){
                std::istringstream fields(line_);std::string method,target,version,extra;
                require(bool(fields>>method>>target>>version)&&!(fields>>extra));const auto path=decoded_path(target);
                guarded_=guarded_path(path);
                if(guarded_){
                    require(line_==method+" "+target+" HTTP/1.1\r\n"&&version=="HTTP/1.1");
                    require(private_peer(remote_.first)&&budget_.allow_request(remote_.first));connection_=budget_.try_incoming();require(connection_.has_value());
                    deadline_=std::min(request_+std::chrono::seconds(24),clock_()+std::chrono::seconds(8));
                    const bool pairing=path.starts_with("/api/pair/")||path=="/api/control/v1/host/pairing"||path.starts_with("/api/control/v1/host/pairing/");
                    body_limit_=pairing?4096:32768;header_=line_;
                }
                classified_=true;
            }
            return;
        }
        if(!guarded_)return;
        if(headers_done_){require(++body_<=body_limit_);return;}
        if(byte=='\n')require(!header_.empty()&&header_.back()=='\r');
        header_+=byte;require(header_.size()<=8192);
        if(header_.ends_with("\r\n\r\n")){
            auto lower=header_;for(auto &c:lower)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
            bool length_seen=false;std::size_t begin=line_.size();
            while(begin+2<lower.size()){
                const auto end=lower.find("\r\n",begin),colon=lower.find(':',begin);require(end!=std::string::npos&&colon!=std::string::npos&&colon<end&&colon>begin);
                const auto name=lower.substr(begin,colon-begin);
                require(name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_")==std::string::npos&&name!="transfer-encoding"&&name!="content-encoding"&&name!="expect");
                if(name=="content-length"){
                    require(!length_seen);length_seen=true;auto value=lower.substr(colon+1,end-colon-1);
                    const auto first=value.find_first_not_of(" \t"),last=value.find_last_not_of(" \t");require(first!=std::string::npos);value=value.substr(first,last-first+1);
                    std::size_t count{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),count);
                    require((value.size()==1||value.front()!='0')&&parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size()&&count<=body_limit_);
                }
                begin=end+2;
            }
            headers_done_=true;header_.clear();
        }
    }
    ns::Handle fd_;
    const std::atomic<socket_t> &listener_;
    PiPairAdmission &budget_;
    PiPairGuardedHttpServer::Clock clock_;
    std::chrono::microseconds read_wait_,write_wait_;
    const Time born_;
    mutable Time last_;
    Time request_{born_},deadline_{};
    std::pair<std::string,int> remote_,local_;
    std::array<char,4096> buffer_{};
    std::size_t begin_{},end_{},body_{},body_limit_{};
    std::string line_,header_;
    bool classified_{},guarded_{},headers_done_{},writing_{};
    mutable bool failed_{};
    std::optional<PiPairAdmission::Connection> connection_;
};
}
PiPairGuardedHttpServer::PiPairGuardedHttpServer(PiPairAdmission &budget,Clock clock):admission_(budget),clock_(std::move(clock)){
    require(bool(clock_));new_task_queue=[] {return new httplib::ThreadPool(CPPHTTPLIB_THREAD_POOL_COUNT,8);};
    set_post_routing_handler([](const httplib::Request &request,httplib::Response &response){
        if(guarded_path(request.path)){
            response.headers.erase("Connection");response.headers.erase("Keep-Alive");response.headers.erase("Cache-Control");
            response.set_header("Connection","close");response.set_header("Cache-Control","no-store");
        }
    });
}
bool PiPairGuardedHttpServer::process_and_close_socket(socket_t fd){
    struct Close {ns::Handle fd;~Close(){ns::shutdown(fd);ns::close(fd);}} close{fd};
    try{
        require(ns::configure_stream(fd));
        GuardedStream stream(fd,svr_sock_,admission_,clock_,read_timeout_sec_,read_timeout_usec_,write_timeout_sec_,write_timeout_usec_);
        std::string remote,local;int remote_port{},local_port{};stream.get_remote_ip_and_port(remote,remote_port);stream.get_local_ip_and_port(local,local_port);
        bool result=false;
        for(std::size_t count=0;count<keep_alive_max_count_&&svr_sock_.load()!=ns::invalid;++count){
            stream.next();bool closed=false;result=process_request(stream,remote,remote_port,local,local_port,false,closed,nullptr);
            if(!result||closed||stream.guarded())break;
        }
        return result;
    }catch(...){return false;}
}
} // namespace wsprrypi
