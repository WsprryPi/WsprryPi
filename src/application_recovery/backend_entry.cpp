// SPDX-License-Identifier: MIT
#include "backend_entry.hpp"
#include "candidate_builder.hpp"
#include "private_json.hpp"
#include "application_control/local_transport.hpp"
#include "application_control/runtime_options.hpp"
#include <iostream>
#include <string_view>
namespace wsprrypi {
std::optional<int> recovery_backend_entry(int argc,char *argv[]){
    bool selected=false;
    for(int index=1;index<argc;++index)if(std::string_view(argv[index])=="--recovery-backend")selected=true;
    if(!selected)return {};
    try{
        std::string action,id;std::optional<std::string> runtime;bool role=false;
        for(int index=1;index<argc;++index){
            const std::string option=argv[index];
            const auto argument=[&]{if(index+1>=argc)throw LocalCommandRefused(LocalRefusal::InvalidRequest);return std::string(argv[++index]);};
            if(option=="--recovery-backend"&&!role){role=true;action=argument();}
            else if(option=="--job-id"&&id.empty())id=argument();
            else if(option=="--runtime-directory"&&!runtime)runtime=argument();
            else throw LocalCommandRefused(LocalRefusal::InvalidRequest);
        }
        if((action=="build-candidate"||action=="seal-candidate")&&id.size()==32&&id.find_first_not_of("0123456789abcdef")==std::string::npos){
            std::cout<<(action=="build-candidate"?RecoveryCandidateBuilder::build(id):RecoveryCandidateBuilder::seal(id)).dump()<<'\n';return 0;
        }
        LocalRequest request{LocalOperation::RecoveryObserve,nlohmann::json::object()};
        if(action=="review"&&id.empty()){
            std::string bytes;char value{};
            while(std::cin.get(value)){if(bytes.size()>=8192)throw LocalCommandRefused(LocalRefusal::InvalidRequest);bytes+=value;}
            request={LocalOperation::RecoveryReview,parse_recovery_json(bytes,8192)};
        }else if((action=="capture"||action=="validate"||action=="prepare")&&id.size()==32&&id.find_first_not_of("0123456789abcdef")==std::string::npos)
            request={action=="capture"?LocalOperation::RecoveryCapture:action=="validate"?LocalOperation::RecoveryValidate:LocalOperation::RecoveryPrepare,{{"job_id",id}}};
        else if(action!="observe"||!id.empty())throw LocalCommandRefused(LocalRefusal::InvalidRequest);
        const auto reply=local_control_request(administrator_control_options(runtime),request);
        if(reply.at("ok")!=true){std::cout<<reply.dump()<<'\n';return 2;}
        std::cout<<reply.at("value").dump()<<'\n';return 0;
    }catch(const LocalControlError &error){std::cerr<<error.what()<<'\n';return error.code()==LocalControlError::Code::Unavailable?3:2;}
    catch(...){std::cerr<<"Invalid internal recovery command or unavailable backend.\n";return 2;}
}
}
