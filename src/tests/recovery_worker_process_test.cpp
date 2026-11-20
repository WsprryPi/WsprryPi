// SPDX-License-Identifier: MIT
#include "application_recovery/worker_process.hpp"
#include "application_control/local_transport.hpp"
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
void check(bool value){if(!value)throw std::runtime_error("Recovery worker process assertion failed");}
int main(int argc,char **argv){try{
    check(argc==3);char temporary[]="/tmp/recovery-worker-XXXXXX";check(::mkdtemp(temporary));
    const auto root=std::filesystem::canonical(temporary);
    struct Cleanup {std::filesystem::path root;~Cleanup(){std::filesystem::remove_all(root);}} cleanup{root};
    RecoveryPaths paths;paths.root=root.string();paths.worker=std::filesystem::canonical("application_recovery/worker.py").string();
    paths.backend_command={"/usr/bin/false"};
    paths.python=std::filesystem::canonical(argv[1]).string();paths.age=std::filesystem::canonical(argv[2]).string();
    // Prove the broker is safe even when the embedding caller has not globally
    // ignored SIGPIPE. Worker requests after process loss must be unknown.
    ::signal(SIGPIPE,SIG_DFL);
    {
        RecoveryWorkerProcess worker(paths,std::string(32,'a'),"controller");
        auto result=worker.request({{"action","list"}});check(result["ok"]==true&&result["value"]["jobs"].empty());
        const auto job=worker.request({{"action","restore.upload.start"},{"request_id",std::string(32,'b')},{"size","8"}});
        check(job["ok"]==true&&job["value"]["state"]=="uploading");const auto id=job["value"]["job_id"];
        const auto chunk=nlohmann::json{{"action","restore.upload.chunk"},{"job_id",id},{"offset","0"},{"bytes","MTIzNDU2Nzg="}};
        check(worker.request(chunk)["value"]["offset"]=="8");check(worker.request(chunk)["value"]["offset"]=="8");
        check(worker.request({{"action","restore.upload.chunk"},{"job_id",id},{"offset","0"},{"bytes","OTIzNDU2Nzg="}})["ok"]==false);
        check(worker.request({{"action","backup.create"},{"request_id",std::string(32,'c')},{"passphrase","secret"},{"confirmation","secret"}})["error"]=="busy");
        check(worker.request({{"action","list"}})["value"]["jobs"].size()==1);
        // Destruction kills/reaps this worker and its owned group, leaving the
        // upload incomplete; a new worker must report interruption, not apply it.
    }
    {
        RecoveryWorkerProcess worker(paths,std::string(32,'a'),"controller");
        auto result=worker.request({{"action","list"}});check(result["ok"]==true&&result["value"]["active"].is_null());
        check(result["value"]["jobs"].size()==1&&result["value"]["jobs"][0]["state"]=="failed"&&result["value"]["jobs"][0]["error"]=="interrupted");
        check(worker.request({{"action","list"},{"extra",true}})["ok"]==false);
    }
    // A private child that dies before reading a request cannot kill the caller
    // or cause an automatic mutation replay on a replacement child.
    const auto dead_directory=root/"dead";std::filesystem::create_directory(dead_directory);::chmod(dead_directory.c_str(),0700);
    for(const auto *module:{"__init__.py","archive.py","durable.py","jobs.py","publication.py","evidence.py","lifecycle.py","proxy.py"})
        std::filesystem::copy_file(std::filesystem::path(paths.worker).parent_path()/module,dead_directory/module);
    const auto dead=dead_directory/"worker.py";{std::ofstream output(dead);output<<"import os\nos._exit(0)\n";}::chmod(dead.c_str(),0600);paths.worker=dead.string();
    {
        RecoveryWorkerProcess worker(paths,std::string(32,'a'),"controller");::usleep(100000);
        for(unsigned attempt=0;attempt<2;++attempt){bool unknown=false;try{worker.request({{"action","list"}});}catch(const LocalControlError &error){unknown=error.code()==LocalControlError::Code::UnknownOutcome;}check(unknown);}
    }
    std::cout<<"PASS private worker pipe, bounded upload, duplicate chunks, busy admission, interruption recovery, child reaping and no replay after process loss\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
