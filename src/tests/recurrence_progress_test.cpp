// SPDX-License-Identifier: MIT
#include "recurrence_progress.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
template<class F>void refuses(F f){bool refused=false;try{f();}catch(...){refused=true;}assert(refused);}
std::string bytes(const std::string &path){std::ifstream input(path);return {(std::istreambuf_iterator<char>(input)),{}};}
void put(const std::string &path,const std::string &data){std::ofstream out(path);out<<data;out.close();assert(!chmod(path.c_str(),0600));}
}
int main(){
    char pattern[]="/tmp/pi-recurrence-progress-XXXXXX";const auto temporary=mkdtemp(pattern);assert(temporary);
    const std::string root=temporary,self(32,'a'),file=root+"/progress.json",directory=root+"/transaction";
    struct Remove{std::string root;~Remove(){std::filesystem::remove_all(root);}} remove{root};
    RecurrenceListDigests digests;digests.fill(std::string(64,'b'));
    const std::array<std::uint32_t,5> sizes{3,2,2,2,2};
    RecurrenceProgress progress(directory,file,self);
    refuses([&]{progress.recover(false,digests);});assert(progress.held()&&!std::filesystem::exists(file));
    progress.recover(true,digests);const auto initial=progress.snapshot();
    const auto digest=PiManagementJournal::digest(bytes(file));
    PiManagementJournal observe(directory,self,{{"progress",file}});
    const auto signature=observe.inspect("progress")->signature;
    for(unsigned i=0;i<100;++i)assert(progress.snapshot()==initial);
    assert(PiManagementJournal::digest(bytes(file))==digest&&observe.inspect("progress")->signature==signature); // observations never write
    const auto reserve=[&](std::uint64_t first,std::uint64_t last,std::size_t scope,bool skip=false){
        progress.reserve(std::string(32,'c'),first,last,std::string(64,'d'),1,1,1,digests,sizes,scope,skip);
    };
    reserve(100,220,0);auto reserved=progress.snapshot();
    assert(reserved["cursors"]["ordinary"]["index"]==0&&reserved["in_flight"]["successor_cursors"]["ordinary"]["index"]==1);
    refuses([&]{reserve(221,341,0);});
    RecurrenceProgress restarted(directory,file,self);restarted.recover(false,digests);
    assert(restarted.snapshot()==reserved); // unknown acceptance retained, not replayed
    restarted.resolve(true,digests); // caller has positively reconciled output
    progress.recover(false,digests);
    assert(progress.snapshot()["cursors"]["ordinary"]["index"]==1);
    refuses([&]{reserve(220,340,0);}); // backward clock / consumed slot
    reserve(221,341,1);progress.resolve(false,digests);
    assert(progress.snapshot()["cursors"]["day"]["index"]==0); // known refusal
    reserve(342,462,1);progress.resolve(true,digests);
    assert(progress.snapshot()["cursors"]["day"]["index"]==1&&progress.snapshot()["cursors"]["ordinary"]["index"]==1);
    reserve(463,463,0,true);
    assert(progress.snapshot()["in_flight"].is_null()&&progress.snapshot()["cursors"]["ordinary"]["index"]==2);
    reserve(464,584,1);auto edited=digests;edited[1]=std::string(64,'e');progress.resolve(true,edited);
    assert(progress.snapshot()["cursors"]["day"]["index"]==0&&progress.snapshot()["cursors"]["ordinary"]["index"]==2);
    auto envelope=Json::parse(bytes(file));
    envelope["record"]["consumed_through_utc_ns"]="0";put(file,envelope.dump());
    refuses([&]{(void)progress.snapshot();});assert(progress.held());
    refuses([&]{restarted.recover(false,digests);});assert(restarted.held());
    // Prepared decisions roll back; committed decisions recover forward after
    // process loss before publication. Both use the production journal.
    const auto original=initial_recurrence_progress(self,digests);
    const auto wrap=[](const Json &record){return Json{{"record",record},{"sha256",PiManagementJournal::digest(record.dump())}}.dump();};
    put(file,wrap(original));
    auto after=original;after["consumed_through_utc_ns"]="900";
    PiManagementJournal journal(directory,self,{{"progress",file}});
    journal.prepare({{"progress",PiManagementJournal::digest(bytes(file)),wrap(after)}});
    restarted.recover(false,digests);assert(restarted.snapshot()==original);
    journal.prepare({{"progress",PiManagementJournal::digest(bytes(file)),wrap(after)}});journal.commit();
    restarted.recover(false,digests);assert(restarted.snapshot()==after);
    auto malformed=original;malformed["cursors"]["night"]["extra"]=true;
    assert(!valid_recurrence_progress(malformed,self));
    malformed=original;malformed["consumed_through_utc_ns"]="01";assert(!valid_recurrence_progress(malformed,self));
    malformed=original;malformed["cursors"]["night"]["index"]=UINT64_MAX;assert(!valid_recurrence_progress(malformed,self));
    refuses([&]{parse_recurrence_progress("{\"record\":null,"+wrap(original).substr(1),self);});
    assert(!unlink(file.c_str()));refuses([&]{restarted.recover(false,digests);});
    assert(!symlink("missing",file.c_str()));refuses([&]{restarted.recover(true,digests);});
}
