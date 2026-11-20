// SPDX-License-Identifier: MIT
#include "local_entry.hpp"
#include "runtime_options.hpp"
#include "advanced_settings.hpp"
#include <charconv>
#include <filesystem>
#include <iostream>
#include <string_view>
namespace wsprrypi {
std::optional<int> local_command_main(int argc,char *argv[]){
    bool selected=false,mutation_attempted=false;
    for(int index=1;index<argc;++index)if(std::string_view(argv[index])=="--route-observation"||std::string_view(argv[index])=="--settings")selected=true;
    if(!selected)return {};
    try{
        std::optional<std::string> directory,action,expected;
        bool route=false,settings=false,help=false;
        nlohmann::json changes=nlohmann::json::object();
        for(int index=1;index<argc;++index){
            const std::string option=argv[index];
            const auto argument=[&]()->std::string{
                if(index+1>=argc||std::string_view(argv[index+1]).starts_with('-'))
                    throw LocalCommandRefused(LocalRefusal::InvalidRequest);
                return argv[++index];
            };
            if(option=="--route-observation"&&!route&&!settings)route=true;
            else if(option=="--settings"&&!settings&&!route){
                settings=true;
                if(index+1<argc&&!std::string_view(argv[index+1]).starts_with('-'))action=argument();
            }
            else if((option=="--help"||option=="-h")&&!help)help=true;
            else if(option=="--runtime-directory"&&!directory)directory=argument();
            else if(option.starts_with("--runtime-directory=")&&!directory)directory=option.substr(20);
            else if(option=="--expected-revision"&&!expected)expected=argument();
            else if(option=="--set"){
                const auto assignment=argument();const auto equals=assignment.find('=');
                if(equals==std::string::npos)throw LocalCommandRefused(LocalRefusal::InvalidRequest);
                const auto name=assignment.substr(0,equals);const auto field=advanced_field(name);
                if(!field||changes.contains(name))throw LocalCommandRefused(LocalRefusal::InvalidRequest);
                changes[name]=parse_advanced_argument(*field,std::string_view(assignment).substr(equals+1));
            }
            else throw LocalCommandRefused(LocalRefusal::InvalidRequest);
        }
        if(directory&&(!std::filesystem::path(*directory).is_absolute()||directory->find('\0')!=std::string::npos))
            throw LocalCommandRefused(LocalRefusal::InvalidRequest);
        if(route&&(expected||!changes.empty()))throw LocalCommandRefused(LocalRefusal::InvalidRequest);
        if(help){
            if(route)std::cout<<"wsprrypi --route-observation [--runtime-directory <absolute directory>]\n";
            else{
                std::cout<<"wsprrypi --settings read [--runtime-directory <absolute directory>]\n"
                    <<"wsprrypi --settings write --expected-revision <revision> --set '<field>=<value>' [--set ...]\n"
                    <<"A running backend and its local administrator identity are required. No offline settings writes.\n"
                    <<"Listener changes require process restart; startup listener flags still override saved values.\n"
                    <<"Fields (controller mode supports only debug logging and listener ports):\n";
                for(const auto &field:advanced_fields())std::cout<<"  "<<field.name<<'\n';
            }
            return 0;
        }
        LocalRequest request{LocalOperation::RouteRead,nlohmann::json::object()};
        if(settings){
            if(action=="read"&&!expected&&changes.empty())request.operation=LocalOperation::SettingsRead;
            else if(action=="write"&&expected&&!changes.empty()){
                std::uint64_t number{};const auto parsed=std::from_chars(expected->data(),expected->data()+expected->size(),number);
                if(expected->empty()||expected->front()=='0'||parsed.ec!=std::errc{}||parsed.ptr!=expected->data()+expected->size())
                    throw LocalCommandRefused(LocalRefusal::InvalidRequest);
                request={LocalOperation::SettingsWrite,{{"expected_revision",*expected},{"changes",changes}}};
            }else throw LocalCommandRefused(LocalRefusal::InvalidRequest);
        }
        mutation_attempted=request.operation==LocalOperation::SettingsWrite;
        const auto result=local_control_request(administrator_control_options(directory),request);
        if(result.at("ok")!=true){std::cerr<<result.dump()<<'\n';return 2;}
        if(settings&&!valid_advanced_view(result.at("value")))
            throw LocalControlError(mutation_attempted?LocalControlError::Code::UnknownOutcome:LocalControlError::Code::Protocol);
        std::cout<<result.at("value").dump()<<'\n';return 0;
    }catch(const LocalControlError &error){
        std::cerr<<error.what()<<'\n';return error.code()==LocalControlError::Code::Unavailable?3:2;
    }catch(const std::exception &){
        if(mutation_attempted)std::cerr<<LocalControlError(LocalControlError::Code::UnknownOutcome).what()<<'\n';
        else std::cerr<<"Invalid local command; use --settings --help for supported fields and syntax.\n";
        return 2;
    }
}
}
