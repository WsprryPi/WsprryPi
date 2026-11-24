// SPDX-License-Identifier: MIT
#include "application_recovery/worker_process.hpp"
#include "application_control/local_transport.hpp"
#include <csignal>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
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
        check(worker.alive());
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
    for(const auto *module:{"__init__.py","archive.py","durable.py","jobs.py","publication.py","evidence.py","lifecycle.py","proxy.py","private_acl.py","state_owner.py"})
        std::filesystem::copy_file(std::filesystem::path(paths.worker).parent_path()/module,dead_directory/module);
    const auto dead=dead_directory/"worker.py";{std::ofstream output(dead);output<<"import os\nos._exit(0)\n";}::chmod(dead.c_str(),0600);paths.worker=dead.string();
    {
        bool refused=false;try{RecoveryWorkerProcess worker(paths,std::string(32,'a'),"controller");}
        catch(const LocalControlError &error){refused=error.code()==LocalControlError::Code::Unavailable;}
        check(refused);
    }
    const std::string startup=
        "import json,sys\n"
        "assert json.loads(sys.stdin.buffer.readline()) == {'action':'list.summary'}\n"
        "print(json.dumps({'ok':True,'value':{'schema':'recovery-job-list/1','active':None,'jobs':[]}}),flush=True)\n";
    for(const auto &value:std::vector<nlohmann::json>{
        {{"schema","recovery-job-list/0"},{"active",nullptr},{"jobs",nlohmann::json::array()}},
        {{"schema","recovery-job-list/1"},{"active",nullptr},{"jobs",nlohmann::json::array()},{"extra",true}},
        {{"schema","recovery-job-list/1"},{"active","invalid"},{"jobs",nlohmann::json::array()}}}){
        {std::ofstream output(dead);output<<"import os,sys,time\nwith os.fdopen(4,'rb') as source: source.read()\nsys.stdin.buffer.readline()\nprint("<<
            nlohmann::json(nlohmann::json{{"ok",true},{"value",value}}.dump()).dump()<<",flush=True)\ntime.sleep(30)\n";}
        bool refused=false;try{RecoveryWorkerProcess worker(paths,std::string(32,'a'),"controller");}
        catch(const LocalControlError &error){refused=error.code()==LocalControlError::Code::Unavailable;}
        check(refused);
    }
    // Initialization has its own bounded readiness phase. The first public
    // command retains its ordinary deadline after a slow, verified startup.
    {std::ofstream output(dead);output<<"import os,time\nwith os.fdopen(4,'rb') as source: source.read()\ntime.sleep(6)\n"<<startup<<
        "for line in sys.stdin.buffer:\n"
        " assert json.loads(line) == {'action':'list.summary'}\n"
        " print(json.dumps({'ok':True,'value':{'schema':'recovery-job-list/1','active':None,'jobs':[]}}),flush=True)\n";}
    {
        const auto began=std::chrono::steady_clock::now();RecoveryWorkerProcess worker(paths,std::string(32,'a'),"controller");
        check(std::chrono::steady_clock::now()-began>=std::chrono::seconds(5));
        check(worker.request({{"action","list.summary"}})["ok"]==true);
    }
    {std::ofstream output(dead);output<<"import os\nwith os.fdopen(4,'rb') as source: source.read()\n"<<startup<<"os._exit(0)\n";}
    {
        RecoveryWorkerProcess worker(paths,std::string(32,'a'),"controller");::usleep(100000);
        for(unsigned attempt=0;attempt<2;++attempt){bool unknown=false;try{worker.request({{"action","list"}});}catch(const LocalControlError &error){unknown=error.code()==LocalControlError::Code::UnknownOutcome;}check(unknown);}
        check(!worker.alive());
    }
    // A descendant retaining the response pipe must also be killed when the
    // worker exits, without needing a subsequent request to discover EOF.
    const auto heartbeat=dead_directory/"heartbeat";
    {std::ofstream output(dead);output<<
        "import os,time\nfrom pathlib import Path\n"
        "with os.fdopen(4,'rb') as source: source.read()\n"<<startup<<
        "if os.fork(): os._exit(0)\n"
        "while True:\n Path(__file__).with_name('heartbeat').write_text(str(time.monotonic_ns()))\n time.sleep(.01)\n";}
    {
        RecoveryWorkerProcess worker(paths,std::string(32,'a'),"controller");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!std::filesystem::exists(heartbeat)){check(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        while(worker.alive()){check(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const auto stopped=std::filesystem::last_write_time(heartbeat);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        check(std::filesystem::last_write_time(heartbeat)==stopped);
    }
    std::cout<<"PASS verified worker startup, malformed readiness refusal, delayed initialization, private pipe, bounded upload, duplicate chunks, busy admission, interruption recovery, child reaping and no replay after process loss\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
