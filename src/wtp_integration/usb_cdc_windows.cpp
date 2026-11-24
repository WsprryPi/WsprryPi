// SPDX-License-Identifier: MIT
#include "usb_cdc_native.hpp"
#include "usb_cdc_windows_metadata.hpp"
#ifdef _WIN32
#include "native_windows_file.hpp"
#include <initguid.h>
#include <devpkey.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <cwchar>
#include <regex>
#include <stdexcept>

namespace wsprrypi {
namespace {
void require(bool value){if(!value)throw std::runtime_error("Windows USB interface metadata is unavailable or inconsistent");}
// The OS interface symbolic link is opened directly. COM numbers and friendly
// names never choose a device, a role, or its physical parent.
constexpr GUID serial_interface{0x86e0d1e0,0x8089,0x11d0,{0x9c,0xe4,0x08,0x00,0x3e,0x30,0x1f,0x73}};
std::string utf8(const std::wstring &text){
    require(!text.empty()&&text.size()<=4096&&std::none_of(text.begin(),text.end(),[](wchar_t value){return value<32||value==127;}));
    const auto size=::WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    require(size>0&&size<=16384);std::string bytes(static_cast<std::size_t>(size),'\0');
    require(::WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),bytes.data(),size,nullptr,nullptr)==size);return bytes;
}
std::vector<unsigned char> property(DEVINST node,const DEVPROPKEY &key,DEVPROPTYPE expected){
    DEVPROPTYPE type{};ULONG length{};
    const auto result=::CM_Get_DevNode_PropertyW(node,&key,&type,nullptr,&length,0);
    require(result==CR_BUFFER_SMALL&&type==expected&&length>0&&length<=65536);
    std::vector<unsigned char> bytes(length);
    require(::CM_Get_DevNode_PropertyW(node,&key,&type,bytes.data(),&length,0)==CR_SUCCESS&&type==expected&&length==bytes.size());return bytes;
}
std::wstring text(DEVINST node,const DEVPROPKEY &key){
    const auto bytes=property(node,key,DEVPROP_TYPE_STRING);require(bytes.size()%sizeof(wchar_t)==0&&bytes.size()>=sizeof(wchar_t));
    const auto *data=reinterpret_cast<const wchar_t *>(bytes.data());const auto size=bytes.size()/sizeof(wchar_t);
    require(data[size-1]==0&&std::find(data,data+size-1,L'\0')==data+size-1);
    const std::wstring value(data,size-1);(void)utf8(value);return value;
}
std::vector<std::wstring> strings(DEVINST node,const DEVPROPKEY &key){
    const auto bytes=property(node,key,DEVPROP_TYPE_STRING_LIST);require(bytes.size()%sizeof(wchar_t)==0&&bytes.size()>=2*sizeof(wchar_t));
    const auto *data=reinterpret_cast<const wchar_t *>(bytes.data());const auto size=bytes.size()/sizeof(wchar_t);
    require(data[size-1]==0&&data[size-2]==0);std::vector<std::wstring> result;std::size_t offset{};
    while(offset+1<size){
        const auto *end=std::find(data+offset,data+size,L'\0');require(end!=data+size);
        require(end!=data+offset);result.emplace_back(data+offset,end);(void)utf8(result.back());
        require(result.size()<=128);offset=static_cast<std::size_t>(end-data)+1;
    }
    require(offset==size-1);return result;
}
std::wstring instance(DEVINST node){
    ULONG size{};require(::CM_Get_Device_ID_Size(&size,node,0)==CR_SUCCESS&&size>0&&size<=4096);
    std::vector<wchar_t> bytes(static_cast<std::size_t>(size)+1);
    require(::CM_Get_Device_IDW(node,bytes.data(),static_cast<ULONG>(bytes.size()),0)==CR_SUCCESS&&bytes[size]==0);
    const std::wstring value(bytes.data(),size);(void)utf8(value);return value;
}
std::optional<DEVINST> physical(DEVINST source){
    DEVINST node=source;
    for(unsigned depth=0;depth<32;++depth){
        if(windows_cdc_metadata::physical(instance(node)))return node;
        DEVINST parent{};const auto result=::CM_Get_Parent(&parent,node,0);
        if(result==CR_NO_SUCH_DEVNODE)return {};
        require(result==CR_SUCCESS);node=parent;
    }
    throw std::runtime_error("Windows USB ancestry exceeds its limit");
}
NativeCdcEndpoint endpoint(DEVINST node,DEVINST parent,const std::wstring &path){
    const auto capabilities=property(parent,DEVPKEY_Device_Capabilities,DEVPROP_TYPE_UINT32);require(capabilities.size()==sizeof(DWORD));
    DWORD flags{};std::memcpy(&flags,capabilities.data(),sizeof flags);
    static_assert(CM_DEVCAP_UNIQUEID==0x10);
    return windows_cdc_endpoint({path,instance(node),instance(parent),text(parent,DEVPKEY_Device_BusReportedDeviceDesc),
        text(node,DEVPKEY_Device_BusReportedDeviceDesc),text(node,DEVPKEY_Device_Service),
        strings(node,DEVPKEY_Device_HardwareIds),strings(node,DEVPKEY_Device_CompatibleIds),strings(parent,DEVPKEY_Device_HardwareIds),
        flags,static_cast<std::uint64_t>(node)});
}
}
std::vector<NativeCdcEndpoint> windows_cdc_inventory(){
    const auto devices=::SetupDiGetClassDevsW(&serial_interface,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    require(devices!=INVALID_HANDLE_VALUE);struct Close{HDEVINFO value;~Close(){::SetupDiDestroyDeviceInfoList(value);}} close{devices};
    std::vector<NativeCdcEndpoint> result;
    for(DWORD index=0;;++index){
        SP_DEVICE_INTERFACE_DATA row{};row.cbSize=sizeof row;
        if(!::SetupDiEnumDeviceInterfaces(devices,nullptr,&serial_interface,index,&row)){
            require(::GetLastError()==ERROR_NO_MORE_ITEMS);break;
        }
        require(index<4096);DWORD length{};
        require(!::SetupDiGetDeviceInterfaceDetailW(devices,&row,nullptr,0,&length,nullptr)&&::GetLastError()==ERROR_INSUFFICIENT_BUFFER&&length<=65536&&length>=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W));
        std::vector<unsigned char> bytes(length);auto *detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(bytes.data());detail->cbSize=sizeof(*detail);
        SP_DEVINFO_DATA info{};info.cbSize=sizeof info;
        require(::SetupDiGetDeviceInterfaceDetailW(devices,&row,detail,length,nullptr,&info)!=0);
        const auto capacity=(bytes.size()-offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W,DevicePath))/sizeof(wchar_t);
        const auto *end=std::find(detail->DevicePath,detail->DevicePath+capacity,L'\0');require(end!=detail->DevicePath+capacity);
        const std::wstring path(detail->DevicePath,static_cast<std::size_t>(end-detail->DevicePath));(void)utf8(path);
        const auto parent=physical(info.DevInst);if(!parent)continue;
        std::wstring product;
        try{product=text(*parent,DEVPKEY_Device_BusReportedDeviceDesc);}catch(const std::exception &){continue;}
        if(product!=L"WsprryPico")continue;
        ULONG status{},problem{};
        require((row.Flags&SPINT_ACTIVE)&&!(row.Flags&SPINT_REMOVED)&&
            ::CM_Get_DevNode_Status(&status,&problem,info.DevInst,0)==CR_SUCCESS&&(status&DN_STARTED)&&!problem);
        // Recognized but incomplete/changed Picos make discovery unavailable.
        result.push_back(endpoint(info.DevInst,*parent,path));
    }
    return result;
}
}
#else
#include <stdexcept>
namespace wsprrypi {std::vector<NativeCdcEndpoint> windows_cdc_inventory(){throw std::runtime_error("Windows USB metadata is unavailable on this host");}}
#endif
