// SPDX-License-Identifier: MIT
#include "application_control/web_bootstrap.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
template<class F>void refuses(F action){bool refused=false;try{action();}catch(...){refused=true;}check(refused,"Expected listener cache refusal");}
}
int main(){try{
    char pattern[]="/tmp/web-bootstrap-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Temporary private cache parent");
    const auto root=std::filesystem::canonical(temporary);struct Remove{std::filesystem::path path;~Remove(){std::filesystem::remove_all(path);}}remove{root};
    const auto cache=(root/"listeners.json").string();
    const nlohmann::json document{{"schema","web-service-bootstrap/1"},{"device_id",std::string(32,'a')},{"boot",std::string(32,'b')},
        {"role","pi"},{"web_port",32001},{"socket_port",32002},{"http_enabled",true},{"websocket_enabled",true},
        {"http_loopback_only",true},{"socket_loopback_only",true},{"socket_loopback_family","ipv4"}};
    check(valid_web_bootstrap(document),"Closed secret-free listener document");
    for(const auto &bad:std::vector<nlohmann::json>{
        {{"schema","web-service-bootstrap/1"}},nlohmann::json::array()})check(!valid_web_bootstrap(bad),"Missing shape refuses");
    const nlohmann::json invalid_fields{{"schema","web-service-bootstrap/2"},{"role","hybrid-guess"},{"device_id","unbound"},
        {"web_port",true},{"socket_port",32001},{"http_enabled","true"},{"socket_loopback_family","fallback"}};
    for(const auto &[name,value]:invalid_fields.items()){
        auto invalid=document;invalid[name]=value;check(!valid_web_bootstrap(invalid),"Wrong versions, types, role or listener shape refuse");
    }
    auto invalid=document;invalid["authorized"]=true;check(!valid_web_bootstrap(invalid),"No caller-authorization fields");
    write_web_bootstrap_cache(cache,document);check(read_web_bootstrap_cache(cache)==document,"Durable listener cache roundtrip");
    struct stat before{},after{};check(!::stat(cache.c_str(),&before)&&(before.st_mode&0777)==0600,"Cache owner-only mode");
    write_web_bootstrap_cache(cache,document);check(!::stat(cache.c_str(),&after)&&before.st_ino==after.st_ino,"Readmitted same boot does not rewrite cache");
    {std::ofstream output(cache+".writing");output<<"partial";}check(!::chmod((cache+".writing").c_str(),0600),"Private interrupted cache fixture");
    auto next=document;next["boot"]=std::string(32,'c');write_web_bootstrap_cache(cache,next);
    check(read_web_bootstrap_cache(cache)==next&&!std::filesystem::exists(cache+".writing"),"Interrupted unpublished cache file does not block next admitted generation");
    check(!::chmod(cache.c_str(),0644),"Unsafe cache fixture");refuses([&]{read_web_bootstrap_cache(cache);});refuses([&]{write_web_bootstrap_cache(cache,next);});
    check(!::chmod(cache.c_str(),0600),"Restore safe fixture mode");
    std::filesystem::create_symlink(cache,root/"alias.web-listeners.json");refuses([&]{read_web_bootstrap_cache((root/"alias.web-listeners.json").string());});
    std::filesystem::create_symlink(root,root/"linked-parent");refuses([&]{read_web_bootstrap_cache((root/"linked-parent/listeners.json").string());});
    check(read_web_bootstrap_cache(cache)==next,"Unsafe alternate paths cannot change last admitted listener document");
    std::cout<<"PASS: closed listener bootstrap, private durable cache, unchanged reconnects, interrupted publication and unsafe path/type refusal\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
