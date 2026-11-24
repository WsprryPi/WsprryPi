// SPDX-License-Identifier: MIT
#include "pi_pair_http.hpp"
#include "browser_api.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <stdexcept>
#include "native_socket.hpp"
namespace wsprrypi {
namespace {
void require(bool value){if(!value)throw std::invalid_argument("invalid_pairing_http");}
std::string lower(std::string text){for(auto &c:text)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return text;}
bool safe_value(const std::string &text,std::size_t maximum){return text.size()<=maximum&&std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32&&c<=126;});}
bool path(const std::string &value){return !value.empty()&&value.size()<=256&&value.front()=='/'&&
    value.find_first_not_of("/ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")==std::string::npos&&value.find("//")==std::string::npos;}
bool host(const std::string &value){
    if(value.empty()||!safe_value(value,320))return false;
    std::string name=value,port;
    if(value.front()=='['){
        const auto end=value.find(']');if(end==std::string::npos)return false;
        in6_addr address{};if(inet_pton(AF_INET6,value.substr(1,end-1).c_str(),&address)!=1)return false;
        if(end+1<value.size()){if(value[end+1]!=':')return false;port=value.substr(end+2);if(port.empty())return false;}
    }else {
        const auto colon=value.find(':');if(colon!=std::string::npos){name=value.substr(0,colon);port=value.substr(colon+1);if(port.empty())return false;}
        if(name.empty()||name.size()>253||name.front()=='.'||name.find("..")!=std::string::npos||
            name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-.")!=std::string::npos)return false;
    }
    if(!port.empty()){
        unsigned number{};const auto parsed=std::from_chars(port.data(),port.data()+port.size(),number);
        if(port.front()=='0'||parsed.ec!=std::errc{}||parsed.ptr!=port.data()+port.size()||!number||number>65535)return false;
    }
    return true;
}
bool metadata(const std::string &name){return name=="if-match"||name=="x-wsprrypi-target-context"||name=="x-wsprrypi-operation-id"||
    name=="x-wsprrypi-profile-id"||name=="x-wsprrypi-member-context";}
std::size_t limit(const std::string &path){return path.starts_with("/api/pair/v1/")?4096:32768;}
std::size_t length(const std::string &text){
    std::size_t value{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    require(!text.empty()&&(text.size()==1||text.front()!='0')&&parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size());return value;
}
std::string trim(std::string text){
    const auto first=text.find_first_not_of(" \t");if(first==std::string::npos)return {};
    const auto last=text.find_last_not_of(" \t");return text.substr(first,last-first+1);
}
void validate(const httplib::Request &request){
    require((request.method=="GET"||request.method=="POST"||request.method=="PUT"||
        (request.method=="DELETE"&&request.path.starts_with("/api/control/v1/targets/")))&&path(request.path)&&request.target==request.path&&
        request.get_header_value_count("Host")==1&&host(request.get_header_value("Host")));
    std::set<std::string> seen;std::size_t bytes=0;
    for(const auto &[name,value]:request.headers){
        const auto key=lower(name);require(seen.insert(key).second&&safe_value(value,4096));bytes+=name.size()+value.size()+4;
        require(metadata(key)||key=="host"||key=="content-length"||key=="content-type"||key=="connection");
    }
    require(bytes<=8192&&(!request.has_header("Connection")||lower(request.get_header_value("Connection"))=="close"));
    require(request.get_header_value_count("Content-Length")==1&&length(request.get_header_value("Content-Length"))==request.body.size()&&request.body.size()<=limit(request.path));
    if(request.method=="GET")require(request.body.empty()&&(!request.has_header("Content-Type")||request.get_header_value("Content-Type")=="application/json"));
    else require(request.get_header_value("Content-Type")=="application/json"&&!request.body.empty());
    if(!request.body.empty())(void)strict_browser_json(request.body,request.path.starts_with("/api/pair/v1/")?8:16);
}
}
void PiPairHttpDecoder::header(){
    const auto line=bytes_.find("\r\n");require(line!=std::string::npos&&line<=512);
    const auto first=bytes_.substr(0,line);const auto space=first.find(' '),last=first.rfind(' ');
    require(space!=std::string::npos&&space!=last&&first.substr(last+1)=="HTTP/1.1");
    request_.method=first.substr(0,space);request_.path=first.substr(space+1,last-space-1);request_.target=request_.path;
    std::size_t begin=line+2;
    while(begin<boundary_){
        const auto end=bytes_.find("\r\n",begin),colon=bytes_.find(':',begin);
        require(end!=std::string::npos&&end<=boundary_&&colon!=std::string::npos&&colon<end&&colon>begin);
        const auto name=bytes_.substr(begin,colon-begin);
        require(name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")==std::string::npos);
        const auto key=lower(name);
        require((metadata(key)||key=="host"||key=="content-length"||key=="content-type"||key=="connection")&&request_.get_header_value_count(name)==0);
        request_.headers.emplace(name,trim(bytes_.substr(colon+1,end-colon-1)));begin=end+2;
    }
    require(request_.get_header_value_count("Content-Length")==1);length_=length(request_.get_header_value("Content-Length"));
    require(length_<=limit(request_.path));header_ready_=true;
}
PiPairHttpDecoder::State PiPairHttpDecoder::append(std::span<const std::uint8_t> input){
    if(state_!=State::More){if(!input.empty())state_=State::Refused;return state_;}
    if(input.empty())return state_;
    try{
        require(input.size()<=4096&&bytes_.size()+input.size()<=8192+4+32768);
        bytes_.append(reinterpret_cast<const char *>(input.data()),input.size());
        if(!header_ready_){
            boundary_=bytes_.find("\r\n\r\n");
            if(boundary_==std::string::npos){require(bytes_.size()<=8192);return state_;}
            require(boundary_<=8192);header();
        }
        const auto received=bytes_.size()-boundary_-4;require(received<=length_);
        if(received<length_)return state_;
        request_.body=bytes_.substr(boundary_+4);validate(request_);state_=State::Ready;bytes_.clear();
    }catch(...){state_=State::Refused;bytes_.clear();request_=httplib::Request{};}
    return state_;
}
const httplib::Request &PiPairHttpDecoder::request() const{require(state_==State::Ready);return request_;}
std::string pi_pair_http_request(const std::string &method,const std::string &path,const std::string &host,const std::string &body,const httplib::Headers &extra){
    httplib::Request request;request.method=method;request.path=path;request.target=path;request.body=body;
    request.headers={{"Host",host},{"Content-Length",std::to_string(body.size())},{"Connection","close"}};
    if(method!="GET")request.headers.emplace("Content-Type","application/json");
    for(const auto &[name,value]:extra){require(metadata(lower(name)));request.headers.emplace(name,value);}
    validate(request);std::string wire=method+" "+path+" HTTP/1.1\r\n";
    for(const auto &[name,value]:request.headers)wire+=name+": "+value+"\r\n";
    wire+="\r\n"+body;require(wire.size()-body.size()<=8192);return wire;
}
std::string pi_pair_http_response(const PiPairHttpReply &response){
    const auto &payload=response.payload;
    require(payload.status>=200&&payload.status<=599&&(payload.status<300||payload.status>=400)&&payload.media_type=="application/json"&&payload.body.size()<=32768);
    (void)strict_browser_json(payload.body,16);
    require(safe_value(payload.etag,128)&&safe_value(response.location,256)&&safe_value(response.recovery_resource,128));
    if(!response.location.empty())require(path(response.location)&&response.location.starts_with("/api/"));
    if(!response.recovery_resource.empty())require(path("/"+response.recovery_resource));
    const auto hex=[](const std::string &value,std::size_t size){return value.empty()||
        (value.size()==size&&value.find_first_not_of("0123456789abcdef")==std::string::npos);};
    require(hex(response.context,32)&&hex(response.operation,32)&&hex(response.readback_revision,64));
    std::string wire="HTTP/1.1 "+std::to_string(payload.status)+" Result\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(payload.body.size())+
        "\r\nConnection: close\r\nCache-Control: no-store\r\n";
    if(!payload.etag.empty())wire+="ETag: "+payload.etag+"\r\n";
    if(!response.location.empty())wire+="Location: "+response.location+"\r\n";
    if(!response.recovery_resource.empty())wire+="X-WsprryPi-Recovery-Resource: "+response.recovery_resource+"\r\n";
    if(!response.context.empty())wire+="X-WsprryPi-Target-Context: "+response.context+"\r\n";
    if(!response.operation.empty())wire+="X-WsprryPi-Operation-ID: "+response.operation+"\r\n";
    if(!response.readback_revision.empty())wire+="X-WsprryPi-Readback-Revision: "+response.readback_revision+"\r\n";
    return wire+"\r\n"+payload.body;
}
PiPairHttpResponseDecoder::PiPairHttpResponseDecoder(const std::string &request_path)
    :limit_(limit(request_path)),depth_(request_path.starts_with("/api/pair/v1/")?8:16){require(path(request_path));}
void PiPairHttpResponseDecoder::header(){
    const auto line=bytes_.find("\r\n");require(line!=std::string::npos&&line<=512);
    const auto first=bytes_.substr(0,line);
    require(first.starts_with("HTTP/1.1 ")&&first.size()>=13&&first[12]==' '&&safe_value(first,512));
    unsigned status{};const auto parsed=std::from_chars(first.data()+9,first.data()+12,status);
    require(parsed.ec==std::errc{}&&parsed.ptr==first.data()+12&&status>=200&&status<=599&&(status<300||status>=400));
    httplib::Headers fields;std::set<std::string> seen;
    std::size_t begin=line+2;
    while(begin<boundary_){
        const auto end=bytes_.find("\r\n",begin),colon=bytes_.find(':',begin);
        require(end!=std::string::npos&&end<=boundary_&&colon!=std::string::npos&&colon<end&&colon>begin);
        const auto name=bytes_.substr(begin,colon-begin),key=lower(name),value=trim(bytes_.substr(colon+1,end-colon-1));
        require(seen.insert(key).second&&safe_value(value,4096)&&
            (key=="content-type"||key=="content-length"||key=="connection"||key=="cache-control"||key=="etag"||key=="location"||key=="x-wsprrypi-recovery-resource"||
                key=="x-wsprrypi-target-context"||key=="x-wsprrypi-operation-id"||key=="x-wsprrypi-readback-revision"));
        fields.emplace(name,value);begin=end+2;
    }
    auto field=[&](const char *name){const auto found=fields.find(name);return found==fields.end()?std::string{}:found->second;};
    require(field("Content-Type")=="application/json"&&lower(field("Connection"))=="close"&&field("Cache-Control")=="no-store");
    length_=length(field("Content-Length"));require(length_>0&&length_<=limit_);
    reply_.payload.status=status;reply_.payload.media_type="application/json";
    reply_.payload.etag=field("ETag");reply_.location=field("Location");reply_.recovery_resource=field("X-WsprryPi-Recovery-Resource");
    reply_.context=field("X-WsprryPi-Target-Context");reply_.operation=field("X-WsprryPi-Operation-ID");reply_.readback_revision=field("X-WsprryPi-Readback-Revision");
    // The serializer owns the same closed metadata rules; validate them before
    // receiving any body, without allowing its placeholder to escape.
    reply_.payload.body="{}";(void)pi_pair_http_response(reply_);reply_.payload.body.clear();header_ready_=true;
}
PiPairHttpResponseDecoder::State PiPairHttpResponseDecoder::append(std::span<const std::uint8_t> input){
    if(state_!=State::More){if(!input.empty())state_=State::Refused;return state_;}
    if(input.empty())return state_;
    try{
        require(input.size()<=4096&&bytes_.size()+input.size()<=8192+4+limit_);
        bytes_.append(reinterpret_cast<const char *>(input.data()),input.size());
        if(!header_ready_){
            boundary_=bytes_.find("\r\n\r\n");
            if(boundary_==std::string::npos){require(bytes_.size()<=8192);return state_;}
            require(boundary_<=8192);header();
        }
        const auto received=bytes_.size()-boundary_-4;require(received<=length_);
        if(received<length_)return state_;
        reply_.payload.body=bytes_.substr(boundary_+4);(void)strict_browser_json(reply_.payload.body,static_cast<unsigned>(depth_));
        state_=State::Ready;bytes_.clear();
    }catch(...){state_=State::Refused;bytes_.clear();reply_=PiPairHttpReply{};}
    return state_;
}
const PiPairHttpReply &PiPairHttpResponseDecoder::reply() const{require(state_==State::Ready);return reply_;}
} // namespace wsprrypi
