// SPDX-License-Identifier: MIT
#include "native_windows_fixture.hpp"
#include "application_recovery/backup_artifact.hpp"
#include "application_recovery/windows_observation.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <cassert>
#include <future>
#include <iostream>
namespace wf=wsprrypi::windows_file;
using namespace wsprrypi;
namespace {
template<class F>void refuses(F action){bool refused=false;try{action();}catch(const std::exception &){refused=true;}assert(refused);}
void directory(const std::filesystem::path &path){wf::PrivateSecurity security(wf::current_sid());assert(::CreateDirectoryW(path.c_str(),security.attributes()));}
void write(const std::filesystem::path &path,const std::string &data){
    wf::PrivateSecurity security(wf::current_sid());wf::Handle file(::CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE|READ_CONTROL,
        FILE_SHARE_READ,security.attributes(),CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));assert(file.get()!=INVALID_HANDLE_VALUE);
    DWORD count{};assert(::WriteFile(file.get(),data.data(),static_cast<DWORD>(data.size()),&count,nullptr)&&count==data.size()&&::FlushFileBuffers(file.get()));
}
nlohmann::json stamp(const std::filesystem::path &path){
    wf::Handle file(::CreateFileW(path.c_str(),GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    assert(file.get()!=INVALID_HANDLE_VALUE);return recovery_windows::observation(wf::inspect(file.get(),false));
}
}
int main(){try{
    NativeWindowsFixture fixture;const std::string id(32,'a');
    const auto jobs=fixture.root/L"jobs",job=jobs/id,file=job/L"backup.age";directory(jobs);directory(job);
    std::string data;for(unsigned index=0;index<200000;++index)data+=static_cast<char>(index&255);write(file,data);
    RecoveryPaths paths;paths.root=NativeWindowsFixture::utf8(fixture.root);
    paths.maintenance_sid=wf::current_sid();
    const auto record=NativeWindowsFixture::utf8(job/L"job.json");
    auto receipt=nlohmann::json{{"size",std::to_string(data.size())},{"sha256",PiManagementJournal::digest(data)},{"stamp",stamp(file)}};
    const auto publish=[&]{PiManagementJournal::publish_private_record(record,PiManagementJournal::read_private_record(record),
        nlohmann::json{{"schema","recovery-job/1"},{"job_id",id},{"kind","backup"},{"state","ready"},{"archive",receipt}}.dump());};
    publish();
    {auto foreign=paths;foreign.maintenance_sid=L"S-1-5-18";refuses([&]{RecoveryBackupArtifact wrong_owner(foreign,id);});}
    {
        RecoveryBackupArtifact artifact(paths,id);assert(artifact.size()==data.size()&&artifact.sha256()==receipt["sha256"].get<std::string>());
        assert(artifact.read(0,4)==data.substr(0,4)&&artifact.read(data.size(),65536).empty());
        refuses([&]{artifact.read(data.size()+1,1);});refuses([&]{artifact.read(0,0);});refuses([&]{artifact.read(0,65537);});
        std::vector<std::future<void>> readers;
        for(unsigned index=0;index<4;++index)readers.push_back(std::async(std::launch::async,[&,index]{
            for(unsigned step=0;step<16;++step){const auto offset=index*16384+step*127;assert(artifact.read(offset,4096)==data.substr(offset,4096));}}));
        for(auto &reader:readers)reader.get();
        assert(!::DeleteFileW(file.c_str()));
        wf::Handle writer(::CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));assert(writer.get()==INVALID_HANDLE_VALUE);
    }
    assert(::DeleteFileW(file.c_str()));write(file,data);refuses([&]{RecoveryBackupArtifact stale(paths,id);});
    receipt["stamp"]=stamp(file);publish();{RecoveryBackupArtifact current(paths,id);assert(current.read(65530,32)==data.substr(65530,32));}
    const auto alias=job/L"alias";assert(::CreateHardLinkW(alias.c_str(),file.c_str(),nullptr));refuses([&]{RecoveryBackupArtifact linked(paths,id);});assert(::DeleteFileW(alias.c_str()));
    receipt["stamp"]=stamp(file);receipt["size"]="1";publish();refuses([&]{RecoveryBackupArtifact wrong_size(paths,id);});
    std::cout<<"Native encrypted download receipts, full file identity, bounded independent-offset reads, concurrent readers, replacement/writer exclusion, stale receipt and hard-link refusals passed\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
