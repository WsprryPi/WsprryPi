// SPDX-License-Identifier: MIT
#include "application_control/http_domain.hpp"
#include "application_control/http_contract.hpp"
#include "application_control/wire_bytes.hpp"
#include <iostream>
#include <stdexcept>
using namespace wsprrypi;
namespace {
void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
template<class F>void refuses(F action){bool refused=false;try{action();}catch(...){refused=true;}check(refused,"Expected bounded HTTP resource refusal");}
std::unique_ptr<httplib::Response> response(std::size_t size,bool &finished,bool &success){
    auto result=std::make_unique<httplib::Response>();result->status=200;
    result->set_content_provider(size,"application/octet-stream",[](std::size_t offset,std::size_t count,httplib::DataSink &sink){
        check(count<=65536,"Provider never receives an unbounded read");
        const std::string bytes(count,static_cast<char>('a'+offset%26));return sink.write(bytes.data(),bytes.size());
    },[&](bool complete){finished=true;success=complete;});
    return result;
}
}
int main(){try{
    std::string binary(65536,'\0');for(std::size_t index=0;index<binary.size();++index)binary[index]=static_cast<char>(index);
    check(decode_wire_chunk(encode_wire_chunk(binary))==binary&&decode_wire_chunk("").empty(),"Binary chunks preserve all byte values within the streaming bound");
    for(const auto *bad:{"=","====","AB==","AA=A","AA==\n"})refuses([&]{decode_wire_chunk(bad);});
    refuses([&]{encode_wire_chunk(binary+"x");});
    for(const auto *bad:{"","00","-1","1.0","18446744073709551616"})refuses([&]{wire_offset(bad);});
    check(wire_offset("18446744073709551615")==UINT64_MAX,"Canonical offsets retain the full unsigned range");
    check(!browser_http_operation("GET","/etc/passwd")&&!browser_http_operation("POST","/version"),"No arbitrary proxy routes or methods");
    check(browser_http_operation("PUT","/config")->maximum_body==1024*1024&&
        browser_http_operation("POST","/api/pair/v1/hello")->maximum_body==4096&&
        browser_http_operation("POST","/api/control/v1/targets")->maximum_body==32768,"Bounds follow existing persistence, pair and browser contracts");
    check(!browser_http_operation("GET","/api/support-bundles/../../etc/passwd"),"Artifact route cannot select a filesystem path");
    const std::string recovery="/api/recovery/jobs/"+std::string(32,'a');
    check(browser_http_operation("GET",recovery+"/download")->maximum_resource==2ULL*1024*1024*1024&&
        browser_http_operation("POST",recovery+"/chunk")->maximum_body==128*1024&&
        browser_http_operation("GET","/api/support-bundles/"+std::string(32,'a')+"/download")->maximum_resource==128ULL*1024*1024,
        "Only the closed recovery download receives the larger encrypted archive bound");
    check(!browser_http_operation("GET",recovery+"/../../state.sqlite3")&&!browser_http_operation("GET","/api/recovery/jobs/"+std::string(31,'a')+"g/download")&&
        !browser_http_operation("PUT",recovery+"/download"),"Recovery routes cannot select paths or unsupported actions");
    bool finished=false,success=false;
    {
        BrowserHttpResource resource(response(128ULL*1024*1024,finished,success));
        check(resource.metadata()["size"]=="134217728","Maximum artifact described without materialization");
        check(resource.read(0,65536).size()==65536&&!finished,"One bounded streamed chunk is not a completed download");
        refuses([&]{resource.read(0,65537);});
    }
    check(finished&&!success,"Dropped web connection cannot report successful download");
    finished=false;success=false;
    {
        BrowserHttpResource resource(response(131073,finished,success));
        for(std::uint64_t offset=0;offset<131073;){const auto bytes=resource.read(offset,65536);offset+=bytes.size();}
        check(resource.read(131073,65536).empty(),"EOF requires no allocation or provider read");
        check(!finished,"Backend read completion waits for browser delivery acknowledgement");
        resource.complete(true);check(finished&&success,"Complete sequential delivery acknowledges exactly once");
        refuses([&]{resource.complete(true);});refuses([&]{resource.read(0,1);});refuses([&]{resource.metadata();});
    }
    finished=false;success=true;
    {
        BrowserHttpResource resource(response(131073,finished,success));
        (void)resource.read(65536,65536);resource.complete(true);
    }
    check(finished&&!success,"A successful partial range does not claim full archive download");
    finished=false;success=true;
    {
        BrowserHttpResource resource(response(1,finished,success));(void)resource.read(0,1);resource.complete(false);
    }
    check(finished&&!success,"Failed outer HTTP write remains failed after successful backend reads");
    refuses([&]{BrowserHttpResource resource(response(128ULL*1024*1024+1,finished,success));});
    std::cout<<"PASS: closed HTTP routes, bounded streaming, deferred delivery acknowledgement, partial ranges, disconnects and one-shot completion\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
