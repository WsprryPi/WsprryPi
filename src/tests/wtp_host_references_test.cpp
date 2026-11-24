// SPDX-License-Identifier: MIT
#include "wtp_settings_json.hpp"
#include "application_recovery/definition_adaptation.hpp"
#include <cassert>
#include <iostream>
template<class F>bool refuses(F action){try{action();return false;}catch(const std::exception &){return true;}}
int main(){
    WtpSettings tls;tls.transport="network";tls.hostname="pico.local";tls.tcp_port=18443;tls.device_id=std::string(32,'a');
    for(const auto &path:{std::string("/private/controller/client.key"),std::string("C:\\ProgramData\\WsprryPi\\client.key"),std::string("D:/private/\xce\xa9/client.key")}){
        tls.tls_ca=path;tls.tls_certificate=path;tls.tls_key=path;
        validate_wtp_settings(tls,true);
        const auto record=wtp_settings_json(tls);assert(parse_wtp_settings(record,true)==tls);
        assert(parse_management_profile_settings(management_profile_settings_json(tls),true)==tls);
#ifdef _WIN32
        const bool native=path[0]!='/';
#else
        const bool native=path[0]=='/';
#endif
        assert(refuses([&]{validate_native_wtp_settings(tls,true);})!=native);
    }
    for(const auto *path:{"relative.key","C:relative.key","C:\\","C:\\a\\..\\secret.key","C:\\a\\secret.key:stream",
        "\\\\server\\share\\secret.key","\\\\?\\C:\\secret.key","C:\\a\\NUL","C:\\a\\CON .key","C:\\a\\COM1.key","C:\\a\\secret.key ","C:\\a\\secret.key."}){
        tls.tls_ca=path;tls.tls_certificate=path;tls.tls_key=path;
        assert(refuses([&]{validate_wtp_settings(tls,true);}));
    }
    WtpSettings usb;usb.usb_serial="0123456789ABCDEF";usb.vendor_id=0xcafe;usb.product_id=0x4012;
    for(const auto *path:{"/dev/serial/by-id/pico-if04","\\\\?\\usb#opaque-interface"}){
        usb.path=path;validate_wtp_settings(usb,true);
        assert(parse_wtp_settings(wtp_settings_json(usb),true)==usb);
#ifdef _WIN32
        const bool native=usb.path.starts_with("\\\\?\\");
#else
        const bool native=usb.path.starts_with("/dev/");
#endif
        assert(refuses([&]{validate_native_wtp_settings(usb,true);})!=native);
    }
    for(const auto *path:{"COM1","relative","\\\\.\\COM1","\\\\?\\","\\\\?\\C:\\private.key","\\\\?\\GLOBALROOT\\Device\\other","/dev/../other"}){
        usb.path=path;assert(refuses([&]{validate_wtp_settings(usb,true);}));
    }
    usb.path=wsprrypi::recovery_unresolved_usb_endpoint(std::string(32,'a'));
    validate_native_wtp_settings(usb,true);
    assert(parse_management_profile_settings(management_profile_settings_json(usb),true)==usb);
#ifdef _WIN32
    assert(usb.path=="\\\\?\\wsprrypi-unresolved-"+std::string(32,'a'));
#else
    assert(usb.path=="/dev/wsprrypi-unresolved-"+std::string(32,'a'));
#endif
    for(const auto &id:{std::string{},std::string(31,'a'),std::string(32,'A'),std::string(32,'/')})
        assert(refuses([&]{(void)wsprrypi::recovery_unresolved_usb_endpoint(id);}));
    tls.tls_ca="C:/"+std::string(510,'x');tls.tls_certificate="/client.crt";tls.tls_key="/client.key";
    assert(refuses([&]{validate_wtp_settings(tls,true);}));
    auto closed=management_profile_settings_json(usb);closed["unexpected"]="value";
    assert(refuses([&]{parse_management_profile_settings(closed,true);}));
    std::cout<<"PASS host-independent stored references, native admission, exact round trips, opaque USB identity references, unsafe aliases, bounds, and closed input\n";
}
