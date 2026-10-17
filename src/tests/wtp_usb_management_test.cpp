// SPDX-License-Identifier: MIT
#include "wtp_integration/usb_management.hpp"
#include "wtp_integration/browser_api.hpp"
#include "privileged_network_policy.hpp"
#include <deque>
#include <iostream>
using namespace wsprrypi;
using Json=nlohmann::json;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("line "+std::to_string(__LINE__)+": " #x); } while(false)
namespace {
struct Device : wtp::ByteStream {
  std::vector<std::uint8_t> input;
  std::deque<std::uint8_t> output;
  std::vector<Json> operations;
  bool bad_boot{}, drop{}, closed{}, duplicate{};
  unsigned mutations{}, acks{};
  std::size_t chunk{3};
  Json target{{"scope","member"},{"device_id",std::string(32,'a')},{"boot_id",std::string(32,'b')}};
  void respond(const Json &request) {
    operations.push_back(request);
    if(drop)return;
    const auto operation=request.at("operation").get<std::string>();
    if(operation=="ack")++acks;
    std::string body="{}";
    if(operation=="hello")body=Json{{"schema","usb-management-discovery/1"},{"product","WsprryPico"}}.dump();
    if(operation=="request") {
      if(request.at("method")!="GET")++mutations;
      if(request.at("path")=="/api/usb/v1/time/sample")body=R"({"sample":"token","monotonic_ns":"1000000000"})";
      if(request.at("path")=="/api/usb/v1/time/submit")body=R"({"accepted":true,"selected":true})";
    }
    auto binding=target;if(bad_boot)binding["boot_id"]=std::string(32,'c');
    auto text=Json{{"schema","usb-management/1"},{"id",request.at("id")},{"status",200},
      {"session",std::string(32,'f')},{"target",binding},{"body",body},{"etag","\"r1\""},{"delivery_required",operation=="request"}}.dump();
    if(duplicate)text.insert(1,"\"id\":0,");
    std::vector<std::uint8_t> frame{'U','M','P','1',std::uint8_t(text.size()>>24),std::uint8_t(text.size()>>16),std::uint8_t(text.size()>>8),std::uint8_t(text.size())};
    frame.insert(frame.end(),text.begin(),text.end());output.insert(output.end(),frame.begin(),frame.end());
  }
  wtp::IoResult write(std::span<const std::uint8_t> bytes) override {
    auto n=std::min(chunk,bytes.size());input.insert(input.end(),bytes.begin(),bytes.begin()+n);
    if(input.size()>=8) {
      const auto length=(std::uint32_t(input[4])<<24)|(std::uint32_t(input[5])<<16)|(std::uint32_t(input[6])<<8)|input[7];
      if(input.size()==8+length){auto request=Json::parse(input.begin()+8,input.end());input.clear();respond(request);}
    }
    return {wtp::IoState::Progress,n};
  }
  wtp::IoResult read(std::span<std::uint8_t> bytes) override {
    auto n=std::min({chunk,bytes.size(),output.size()});
    for(std::size_t i=0;i<n;++i){bytes[i]=output.front();output.pop_front();}
    return {n?wtp::IoState::Progress:wtp::IoState::WouldBlock,n};
  }
  void close() noexcept override {closed=true;}
};
}
int main() {
 try {
  std::uint64_t now{}; Device device; bool cancelled=false;
  UsbManagementClient client(device,[&]{return now;},[&](unsigned n){now+=n;},[&]{return cancelled;});
  CHECK(client.hello(std::string(32,'a')));
  CHECK(client.request("/api/v1/hostname","GET").status==200 && device.acks==1);
  CHECK(device.operations[1].at("target")==device.target && device.operations[2].at("operation")=="ack");
  CHECK(client.request("/api/usb/v1/wifi","DELETE","{}","\"r1\"").status==200 && device.mutations==1);
  unsigned observations{};
  CHECK(client.synchronize([&]{++observations;return HostUtcObservation{true,observations*1000000ULL,1700000000000000000ULL+observations*1000000ULL,200000};}));
  auto last=device.operations[device.operations.size()-2];auto time=Json::parse(last.at("body").get<std::string>());
  CHECK(std::stoull(time.at("uncertainty_ns").get<std::string>())>=1200000ULL && time.at("leap")=="normal");
  CHECK(!client.synchronize([]{return HostUtcObservation{};}));
  CHECK(client.observation().at("time").at("reason")=="host_utc_unqualified");
  const auto before=device.operations.size(); cancelled=true;
  CHECK(client.request("/api/v1/hostname","GET").status==503 && device.closed && device.operations.size()==before);
  // Cancellation of a silent endpoint must end before the ten-second deadline.
  Device silent; cancelled=false;
  UsbManagementClient bounded(silent,[&]{return now;},[&](unsigned n){now+=n;if(now%100000>=7)cancelled=true;},[&]{return cancelled;});
  CHECK(bounded.hello(std::string(32,'a')));silent.drop=true;const auto cancel_start=now;
  cancelled=false;
  CHECK(bounded.request("/api/v1/status","GET").status==503 && now-cancel_start<=7 && silent.closed);
  cancelled=false;
  Device wrong;UsbManagementClient rejected(wrong,[&]{return now;},[&](unsigned n){now+=n;});
  CHECK(!rejected.hello(std::string(32,'d')) && wrong.closed);
  Device replay;UsbManagementClient bound(replay,[&]{return now;},[&](unsigned n){now+=n;});
  CHECK(bound.hello(std::string(32,'a')));replay.bad_boot=true;
  CHECK(bound.request("/api/usb/v1/wifi","PUT","{}","\"r1\"").status==503 && replay.mutations==1 && replay.acks==0);
  CHECK(bound.request("/api/usb/v1/wifi","PUT","{}","\"r1\"").status==409 && replay.mutations==1);
  Device repeated;repeated.duplicate=true;UsbManagementClient strict(repeated,[&]{return now;},[&](unsigned n){now+=n;});
  CHECK(!strict.hello(""));
  Device stalled;stalled.drop=true;UsbManagementClient timeout(stalled,[&]{return now;},[&](unsigned n){now+=n;});
  const auto start=now;CHECK(!timeout.hello("") && now-start==2000 && stalled.closed);
  unsigned writes{};std::string selection="selection1";
  WtpBrowserApi api([]{return Json::object();},[&](auto resource,auto method,auto,auto,auto) {
    if(resource=="member/management")return PicoHttpResponse{200,Json{{"carrier","usb_physical"},{"selection_id",selection}}.dump(),{}};
    if(method!="GET")++writes;
    return PicoHttpResponse{200,"{}","\"r1\""};
  },[](auto){return PicoHttpResponse{};});
  for(const auto resource:{"wifi","hostname"}) {
    const std::string path="/api/v1/member/"+std::string(resource);
    CHECK(classify_privileged_http_operation("GET",path)==PrivilegedOperationClass::protected_operation);
    CHECK(api.handle({"PUT",path,"{}","\"r1\"",false}).status==403);
    CHECK(api.handle({"PUT",path,"{}","\"r1\"",true}).status==409);
    CHECK(api.handle({"GET",path,{},{},false}).status==200);
    CHECK(api.handle({"PUT",path,"{}","\"r1\"",true}).status==200);
    CHECK(api.handle({"PUT",path,"{}","\"r1\"",true}).status==409);
  }
  CHECK(api.handle({"GET","/api/v1/member/wifi",{},{},false}).status==200);selection="selection2";
  CHECK(api.handle({"DELETE","/api/v1/member/wifi","{}","\"r1\"",true}).status==409 && writes==2);
  CHECK(classify_privileged_http_operation("DELETE","/api/v1/member/wifi")==PrivilegedOperationClass::protected_operation);
  CHECK(classify_privileged_http_operation("DELETE","/api/v1/member/hostname")==PrivilegedOperationClass::reject);
  CHECK(classify_privileged_http_operation("GET","/api/v1/member/progress")==PrivilegedOperationClass::protected_operation);
  CHECK(classify_privileged_http_operation("POST","/api/v1/member/progress/apply")==PrivilegedOperationClass::protected_operation);
  CHECK(api.handle({"POST","/api/v1/member/progress/update","{}","\"r1\"",true}).status==409);
  CHECK(api.handle({"GET","/api/v1/member/progress",{},{},false}).status==200);
  CHECK(api.handle({"POST","/api/v1/member/progress/update","{}","\"r1\"",true}).status==200);
  CHECK(api.handle({"POST","/api/v1/member/progress/apply","{}","\"r1\"",true}).status==409);
  std::cout<<"USB management framing, ACK, identity, replay refusal, time bounds, cancellation, deadline and browser revision guards passed\n";
 } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
