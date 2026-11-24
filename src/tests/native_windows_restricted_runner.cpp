// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
// Explicit hardware-free native test launcher; never installed with the product.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <algorithm>
struct H {HANDLE v{};~H(){if(v&&v!=INVALID_HANDLE_VALUE)CloseHandle(v);} };
void check(bool ok,const char* step){if(!ok)throw std::runtime_error(std::string(step)+":"+std::to_string(GetLastError()));}
std::vector<unsigned char> info(HANDLE token,TOKEN_INFORMATION_CLASS kind){DWORD n{};GetTokenInformation(token,kind,nullptr,0,&n);std::vector<unsigned char>b(n);check(GetTokenInformation(token,kind,b.data(),n,&n),"token query");return b;}
int wmain(int argc,wchar_t**argv){try{
 check(argc==3||argc==4,"workspace and whitelisted executable arguments");std::filesystem::path root=argv[1];check(root.is_absolute()&&root==root.lexically_normal(),"absolute private workspace");
 const std::wstring name=argv[2];
 const std::wstring mode=argc==4?argv[3]:L"";
 const bool controller_peers=name==L"node.exe"&&mode==L"controller-network-pair";
 const bool controller_usb=name==L"node.exe"&&(mode==L"controller-usb-pair"||mode==L"controller-usb-paired-pi");
 const bool node_fixture=controller_peers||controller_usb;
 check((name==L"wsprrypi.exe"&&(mode==L"controller-help"||mode==L"controller-version"||mode==L"controller-refuse"))||
       node_fixture||(name!=L"wsprrypi.exe"&&name!=L"node.exe"&&(mode.empty()||(name==L"native_windows_file_api_test.exe"&&(mode==L"python-binding"||mode==L"python-archive")))),"closed fixture mode");
 const std::vector<std::wstring> allowed{L"native_windows_manifest_test.exe",L"native_windows_dnssd_test.exe",L"wsprrypi.exe",L"native-windows-ntfs-probe.exe",L"native_windows_file_test.exe",L"native_windows_owned_state_test.exe",L"native_windows_identity_test.exe",L"native_windows_owners_test.exe",L"native_windows_recovery_paths_test.exe",L"native_windows_local_transport_test.exe",L"native_windows_process_test.exe",L"native_windows_sqlite_test.exe",L"native_socket_test.exe",L"native_clock_test.exe",L"wtp_host_references_test.exe",L"solar_clock_test.exe",L"native_windows_usb_metadata_test.exe",L"support_request_guard_test.exe",L"privileged_network_policy_test.exe",L"native_windows_network_test.exe",L"lcblog_timestamp_test.exe",L"signal_handler_test.exe",L"installation_lock_test.exe",L"singleton_test.exe",L"native_windows_file_api_test.exe",L"native_windows_job_lock_client_test.exe",L"native_windows_journal_test.exe",L"native_windows_catalog_test.exe",L"native_windows_generation_test.exe",L"native_windows_companion_test.exe",L"native_windows_backup_artifact_test.exe",L"native_windows_evidence_test.exe",L"native_windows_pairing_profile_test.exe",L"native_windows_pair_crypto_test.exe",L"native_windows_maintenance_io_test.exe",L"native_windows_service_contract_test.exe"};
 check(node_fixture||std::find(allowed.begin(),allowed.end(),name)!=allowed.end(),"executable whitelist");
 const auto exe=(root/name).wstring();
 H original,restricted;check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_ASSIGN_PRIMARY|TOKEN_ADJUST_DEFAULT,&original.v),"open own token");
 PSID admin{};check(ConvertStringSidToSidW(L"S-1-5-32-544",&admin),"admin sid");SID_AND_ATTRIBUTES disable{admin,0};
 const auto made=CreateRestrictedToken(original.v,DISABLE_MAX_PRIVILEGE,1,&disable,0,nullptr,0,nullptr,&restricted.v);LocalFree(admin);check(made,"restrict own token");
 auto user=info(original.v,TokenUser);TOKEN_OWNER owner{reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid};check(SetTokenInformation(restricted.v,TokenOwner,&owner,sizeof(owner)),"set own user owner");
 LPWSTR own_text{};check(ConvertSidToStringSidW(owner.Owner,&own_text),"own user sid text");
 const auto dacl_text=L"D:(A;;GA;;;"+std::wstring(own_text)+L")(A;;GA;;;SY)(A;;GA;;;BA)";LocalFree(own_text);
 PSECURITY_DESCRIPTOR descriptor{};check(ConvertStringSecurityDescriptorToSecurityDescriptorW(dacl_text.c_str(),SDDL_REVISION_1,&descriptor,nullptr),"private default descriptor");
 BOOL dacl_present{},dacl_defaulted{};PACL dacl{};
 check(GetSecurityDescriptorDacl(descriptor,&dacl_present,&dacl,&dacl_defaulted)&&dacl_present&&dacl,"private default dacl");
 TOKEN_DEFAULT_DACL defaults{dacl};const auto defaulted=SetTokenInformation(restricted.v,TokenDefaultDacl,&defaults,sizeof(defaults));LocalFree(descriptor);check(defaulted,"set own private default dacl");

 PSID medium{};check(ConvertStringSidToSidW(L"S-1-16-8192",&medium),"medium sid");TOKEN_MANDATORY_LABEL label{{medium,SE_GROUP_INTEGRITY}};
 auto lowered=SetTokenInformation(restricted.v,TokenIntegrityLevel,&label,static_cast<DWORD>(sizeof(label)+GetLengthSid(medium)));LocalFree(medium);check(lowered,"lower integrity");
 auto priv=info(restricted.v,TokenPrivileges);auto*p=reinterpret_cast<TOKEN_PRIVILEGES*>(priv.data());LUID notify{};check(LookupPrivilegeValueW(nullptr,SE_CHANGE_NOTIFY_NAME,&notify),"notify privilege");
 for(DWORD i=0;i<p->PrivilegeCount;++i){auto&v=p->Privileges[i];check(!(v.Attributes&SE_PRIVILEGE_ENABLED)||(v.Luid.LowPart==notify.LowPart&&v.Luid.HighPart==notify.HighPart),"unexpected enabled privilege");}
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};H output,input;
 output.v=CreateFileW((root/L"probe-output.txt").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);check(output.v!=INVALID_HANDLE_VALUE,"new output");
 input.v=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);check(input.v!=INVALID_HANDLE_VALUE,"null input");
 SIZE_T count{};InitializeProcThreadAttributeList(nullptr,1,0,&count);std::vector<unsigned char> attrs(count);
 auto list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());check(InitializeProcThreadAttributeList(list,1,0,&count),"attributes");
 struct Attr{LPPROC_THREAD_ATTRIBUTE_LIST p;~Attr(){DeleteProcThreadAttributeList(p);}} attr{list};HANDLE inherited[]{input.v,output.v};
 check(UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr),"handle whitelist");
 wchar_t system[MAX_PATH]{};check(GetWindowsDirectoryW(system,MAX_PATH)>0,"windows path");
 std::wstring env; // assembled below, including explicit NUL separators
 env.clear();auto add=[&](const std::wstring&s){env+=s;env.push_back(0);};
 add(L"LANG=C");add(L"PATH="+root.wstring()+L";"+system+L"\\System32");const auto system_drive=std::filesystem::path(system).root_name().native();check(system_drive.size()==2&&system_drive[1]==L':',"OS system drive");add(L"SYSTEMDRIVE="+system_drive);add(L"SYSTEMROOT="+std::wstring(system));add(L"TEMP="+root.wstring());add(L"TMP="+root.wstring());add(L"WINDIR="+std::wstring(system));env.push_back(0);
 H job;job.v=CreateJobObjectW(nullptr,nullptr);check(job.v!=nullptr,"private job");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;check(SetInformationJobObject(job.v,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"job limit");
 STARTUPINFOEXW start{};start.StartupInfo.cb=sizeof(start);start.StartupInfo.dwFlags=STARTF_USESTDHANDLES;start.StartupInfo.hStdInput=input.v;start.StartupInfo.hStdOutput=output.v;start.StartupInfo.hStdError=output.v;start.lpAttributeList=list;
 PROCESS_INFORMATION pi{};std::wstring command=L"\""+exe+L"\"";
 if(name==L"native_windows_job_lock_client_test.exe"){command+=L" --library \""+(root/L"wsprrypi-recovery-native.dll").native()+L"\"";}
 if(node_fixture){
  auto argument=[&](const std::filesystem::path &path){command+=L" \""+path.native()+L"\"";};
  if(controller_peers){
   argument(root/L"source"/L"WsprryPi-UI"/L"tests"/L"pico_controller_http_pair_test.js");
   argument(root/L"peer-build");argument(root/L"pico_controller_pair_fixture.exe");
  }else{
   argument(root/L"source"/L"WsprryPi-UI"/L"tests"/L"pico_management_usb_pair_test.js");
   argument(root/L"pico_management_usb_pair_client.exe");argument(root/L"peer-build"/L"peer-session.json");
   if(mode==L"controller-usb-paired-pi")command+=L" --paired-pi";
  }
 }else if(name==L"wsprrypi.exe"){
  if(mode==L"controller-help")command+=L" --help";
  else if(mode==L"controller-version")command+=L" --version";
 }else if(!mode.empty()){
  auto argument=[&](const std::filesystem::path &path){command+=L" \""+path.native()+L"\"";};
  command+=L" --python";argument(root/L"python"/L"python.exe");command+=L" --script";
  argument(root/L"source"/L"src"/L"tests"/(mode==L"python-binding"?L"native_windows_file_binding_test.py":L"native_windows_archive_test.py"));
  command+=L" --library";argument(root/L"wsprrypi-recovery-native.dll");
  if(mode==L"python-archive"){command+=L" --age";argument(root/L"recovery-age.exe");}
 }
 check(CreateProcessAsUserW(restricted.v,exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT|EXTENDED_STARTUPINFO_PRESENT,env.data(),root.c_str(),&start.StartupInfo,&pi),"restricted child");
 H process{pi.hProcess},thread{pi.hThread};if(!AssignProcessToJobObject(job.v,process.v)){TerminateProcess(process.v,1);WaitForSingleObject(process.v,5000);check(false,"assign own child job");}
 H child_token;check(OpenProcessToken(process.v,TOKEN_QUERY,&child_token.v),"query actual child token");
 auto integrity=info(child_token.v,TokenIntegrityLevel);auto sid=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(integrity.data())->Label.Sid;auto rid=*GetSidSubAuthority(sid,*GetSidSubAuthorityCount(sid)-1);check(rid==8192,"child medium integrity");
 auto groups=info(child_token.v,TokenGroups);auto*g=reinterpret_cast<TOKEN_GROUPS*>(groups.data());PSID admin2{};check(ConvertStringSidToSidW(L"S-1-5-32-544",&admin2),"admin sid");bool admin_enabled=false;for(DWORD i=0;i<g->GroupCount;++i)if(EqualSid(g->Groups[i].Sid,admin2))admin_enabled=(g->Groups[i].Attributes&SE_GROUP_ENABLED)!=0;LocalFree(admin2);check(!admin_enabled,"child administrator enabled");
 check(ResumeThread(thread.v)!=DWORD(-1),"resume own child");auto wait=WaitForSingleObject(process.v,node_fixture?600000:60000);if(wait!=WAIT_OBJECT_0){TerminateJobObject(job.v,124);WaitForSingleObject(process.v,5000);throw std::runtime_error("owned child timed out");}
 DWORD code{};check(GetExitCodeProcess(process.v,&code),"child exit");std::cout<<"{\"same_user_restricted_token\":true,\"admin_enabled\":false,\"integrity_rid\":"<<rid<<",\"child_exit\":"<<code<<"}\n";return static_cast<int>(code);
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
