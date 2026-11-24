// SPDX-License-Identifier: MIT
#include "state_entry.hpp"
#include "application_state.hpp"
#include "migration_source.hpp"
#include "state_migration.hpp"
#include "route_maintenance.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_endpoint/runtime.hpp"
#include "singleton.hpp"
#include "controller_instance_lock.hpp"
#include "controller_identity.hpp"
#include "version.hpp"
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#include "native_windows_file.hpp"
#else
#include <sys/stat.h>
#endif
namespace wsprrypi {
namespace {
std::string argument(int &index,int argc,char *argv[]){
    if(++index>=argc||!argv[index][0]||std::string_view(argv[index]).starts_with('-'))
        throw std::invalid_argument("Missing state option argument");
    return argv[index];
}
#ifdef _WIN32
std::filesystem::path disk_path(const std::string &path){return windows_file::path_from_utf8(path);}
std::string path_text(const std::filesystem::path &path){const auto bytes=path.u8string();return {bytes.begin(),bytes.end()};}
void absolute(const std::string &path){(void)disk_path(path);}
bool present(const std::string &path){
    if(::GetFileAttributesW(windows_file::api_path(disk_path(path)).c_str())!=INVALID_FILE_ATTRIBUTES)return true;
    const auto error=::GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)throw std::runtime_error("Cannot inspect existing device state");return false;
}
#else
std::filesystem::path disk_path(const std::string &path){return std::filesystem::path(path);}
std::string path_text(const std::filesystem::path &path){return path.string();}
void absolute(const std::string &path){
    if(path.empty()||!std::filesystem::path(path).is_absolute()||path.find('\0')!=std::string::npos)
        throw std::invalid_argument("State paths must be absolute");
}
bool present(const std::string &path){struct stat info{};if(!::lstat(path.c_str(),&info))return true;if(errno!=ENOENT)throw std::runtime_error("Cannot inspect existing device state");return false;}
#endif
}
BackendStateArguments backend_state_arguments(int argc,char *argv[]){
    BackendStateArguments result;result.arguments.push_back(argv[0]);
    for(int i=1;i<argc;++i){
        const std::string option=argv[i];
        if(option=="--state-directory"||option.starts_with("--state-directory=")){
            if(result.directory)throw std::invalid_argument("State directory was specified more than once");
            result.directory=option=="--state-directory"?argument(i,argc,argv):option.substr(18);
            absolute(*result.directory);
        }else if(option=="--runtime-directory"||option.starts_with("--runtime-directory=")){
            if(result.runtime_directory)throw std::invalid_argument("Runtime directory was specified more than once");
            result.runtime_directory=option=="--runtime-directory"?argument(i,argc,argv):option.substr(20);
            absolute(*result.runtime_directory);
        }else result.arguments.push_back(argv[i]);
    }
    result.arguments.push_back(nullptr);return result;
}
std::optional<int> state_command_main(int argc,char *argv[],unsigned short singleton_port){
    for(int index=1;index<argc;++index)if(std::string_view(argv[index])=="--storage-format"){
        if(argc==2){std::cout<<"application-state/1\n";return 0;}
        std::cerr<<"--storage-format must be used alone.\n";return 2;
    }
    bool selected=false;
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--initialize-state"||std::string_view(argv[i])=="--migrate-state"||std::string_view(argv[i])=="--check-state"||std::string_view(argv[i])=="--route-maintenance")selected=true;
    if(!selected)return {};
    try{
        bool initialize=false,migrate=false,check=false,controller=false,help=false,listeners=false,companions=false,route_state=false;
        std::optional<std::string> directory,source,route;
        for(int i=1;i<argc;++i){
            const std::string option=argv[i];
            if(option=="--initialize-state"&&!initialize&&!migrate&&!check&&!route_state)initialize=true;
            else if(option=="--migrate-state"&&!initialize&&!migrate&&!check&&!route_state)migrate=true;
            else if(option=="--check-state"&&!initialize&&!migrate&&!check&&!route_state)check=true;
            else if(option=="--route-maintenance"&&!initialize&&!migrate&&!check&&!route_state)route_state=true;
            else if(option=="--configure-route"&&!route)route=argument(i,argc,argv);
            else if(option=="--controller-only"&&!controller)controller=true;
            else if(option=="--listener-bootstrap"&&!listeners)listeners=true;
            else if(option=="--companion-namespace"&&!companions)companions=true;
            else if((option=="--help"||option=="-h")&&!help)help=true;
            else if(option=="--state-directory"&&!directory)directory=argument(i,argc,argv);
            else if(option.starts_with("--state-directory=")&&!directory)directory=option.substr(18);
            else if(option=="--ini-source"&&!source)source=argument(i,argc,argv);
            else if(option.starts_with("--ini-source=")&&!source)source=option.substr(13);
            else throw std::invalid_argument("Unsupported or repeated state command option");
        }
        if(!migrate&&source)throw std::invalid_argument("Only migration imports an INI source");
        if((route&&!route_state)||(route_state&&controller)||(route&&*route!="gpio4"&&*route!="gpio20"))
            throw std::invalid_argument("Route maintenance permits only the existing Pi GPIO4/GPIO20 restoration operation");
        if(listeners&&!check)throw std::invalid_argument("Listener bootstrap is an installer check-state operation");
        if(companions&&(!check||listeners))throw std::invalid_argument("Companion namespace requires an exclusive installer check-state operation");
        if(help){
            std::cout<<"wsprrypi --initialize-state --state-directory <absolute directory> [--controller-only]\n"
                <<"wsprrypi --migrate-state --state-directory <absolute directory> --ini-source <absolute file> [--controller-only]\n"
                <<"wsprrypi --check-state --state-directory <absolute directory> [--controller-only] [--listener-bootstrap]\n"
                <<"wsprrypi --route-maintenance --state-directory <absolute directory> [--configure-route gpio4|gpio20]\n"
                <<"Initialization creates only unconfirmed saved state. Migration preserves the original source backup.\n";
            return 0;
        }
        if(!directory||(migrate&&!source))throw std::invalid_argument("A state directory and, for migration, an INI source are required");
        absolute(*directory);if(source)absolute(*source);
        // Same application admission as the backend, before touching any source
        // recovery owner. No installed service is stopped by this command.
        SingletonProcess admission(singleton_port);
        std::unique_ptr<InstallationLock> controller_admission;
        if(controller)controller_admission=acquire_controller_installation_lock();
        else if(!admission())throw std::runtime_error("The backend is running or exclusive local admission is unavailable");
        if(controller){
            configure_native_controller_identity();
            if(initialize)initialize_native_controller_identity(*directory);
        }
        set_hardware_platform_detection_enabled(false);
        const auto identity=wtp_pi_device_id();const auto role=controller?StateRole::Controller:StateRole::Pi;
        if(route_state){
            auto database=open_application_state(*directory,identity,StateRole::Pi);validate_application_storage(database);
            std::cout<<route_maintenance_state(database,route).dump()<<'\n';
        }else if(check){
            auto database=open_application_state(*directory,identity,role);validate_application_storage(database);
            if(listeners){
                const auto saved=configuration_from_snapshot(database->snapshot(),role);
                std::cout<<nlohmann::json{{"schema","listener-bootstrap/1"},{"device_id",identity},{"role",controller?"controller":"pi"},
                    {"web_port",saved.configuration.at("Operation").at("Web Port")},
                    {"socket_port",saved.configuration.at("Operation").at("Socket Port")}}.dump()<<'\n';
            }else if(companions)std::cout<<database->directory()+"/configuration"<<'\n';
            else std::cout<<"Application state validated.\n";
        }else if(initialize){
            if(!present(*directory)){
                const auto recurrence=private_device_state_directory(PrivateDeviceDomain::Recurrence,identity);
                const auto retirement=private_device_state_directory(PrivateDeviceDomain::ControllerRetirements,identity);
                const auto pairing=private_device_state_directory(PrivateDeviceDomain::Pairing,identity);
                if(present(recurrence)||present(retirement)||present(retirement+".established")||present(pairing)||wtp_pi_has_prior_output_history())
                    throw std::runtime_error("Existing device-owned state requires migration or restore");
            }
            auto database=initialize_application_state(*directory,identity,role,
                [&](const auto &name,const auto &installation){return initial_application_records(name,identity,role,installation);},validate_application_storage);
            std::cout<<"Saved state initialized; operator setup and native output admission remain separate.\n";
        }else{
            const auto input=disk_path(*source);
            const auto original=path_text(std::filesystem::canonical(input.parent_path())/input.filename());
            std::unique_ptr<StateMigrationSource> owner;
            auto database=migrate_application_state(*directory,identity,role,original,[&]{
                if(!owner)owner=std::make_unique<StateMigrationSource>(original,identity,role);
                return owner->capture();
            },validate_application_storage);
            if(owner)for(const auto &warning:owner->warnings())std::cerr<<warning<<'\n';
            std::cout<<"Migration completed; SQLite is authoritative and the original INI backup is retained.\n";
        }
        return 0;
    }catch(const std::exception &error){std::cerr<<"State operation failed: "<<error.what()<<'\n';return 2;}
}
} // namespace wsprrypi
