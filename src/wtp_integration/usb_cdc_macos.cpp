// SPDX-License-Identifier: MIT
#include "usb_cdc_native.hpp"
#ifdef __APPLE__
#include "usb_cdc_descriptors.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/IOUSBHostFamilyDefinitions.h>
#include <IOKit/usb/USBSpec.h>
#include <array>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <sys/stat.h>
#include <utility>

namespace wsprrypi {
namespace {
void require(bool value){if(!value)throw std::runtime_error("macOS USB interface metadata is unavailable or inconsistent");}
struct Object {
    io_object_t value{};
    explicit Object(io_object_t object=0):value(object){}
    ~Object(){if(value)::IOObjectRelease(value);}
    Object(const Object &)=delete;
    Object(Object &&other) noexcept:value(std::exchange(other.value,0)){}
};
struct Property {
    CFTypeRef value{};
    ~Property(){if(value)::CFRelease(value);}
};
std::string text(io_registry_entry_t entry,const char *name) {
    const auto key=::CFStringCreateWithCString(kCFAllocatorDefault,name,kCFStringEncodingUTF8);require(key);
    Property value{::IORegistryEntryCreateCFProperty(entry,key,kCFAllocatorDefault,0)};::CFRelease(key);
    require(value.value&&::CFGetTypeID(value.value)==::CFStringGetTypeID());
    std::array<char,4097> bytes{};
    const auto size=::CFStringGetLength(static_cast<CFStringRef>(value.value));require(size>0&&size<=4096);
    CFIndex used{};
    require(::CFStringGetBytes(static_cast<CFStringRef>(value.value),CFRangeMake(0,size),kCFStringEncodingUTF8,0,false,
        reinterpret_cast<UInt8 *>(bytes.data()),4096,&used)==size&&used>0&&used<=4096);
    std::string result(bytes.data(),static_cast<std::size_t>(used));
    require(std::none_of(result.begin(),result.end(),[](unsigned char c){return c<32||c==127;}));return result;
}
unsigned number(io_registry_entry_t entry,const char *name) {
    const auto key=::CFStringCreateWithCString(kCFAllocatorDefault,name,kCFStringEncodingUTF8);require(key);
    Property value{::IORegistryEntryCreateCFProperty(entry,key,kCFAllocatorDefault,0)};::CFRelease(key);
    require(value.value&&::CFGetTypeID(value.value)==::CFNumberGetTypeID());
    std::int64_t number{};require(::CFNumberGetValue(static_cast<CFNumberRef>(value.value),kCFNumberSInt64Type,&number)&&number>=0&&number<=65535);
    return static_cast<unsigned>(number);
}
std::uint64_t registry_id(io_registry_entry_t entry) {
    std::uint64_t value{};require(::IORegistryEntryGetRegistryEntryID(entry,&value)==KERN_SUCCESS&&value);return value;
}
Object ancestor(io_registry_entry_t source,bool device) {
    require(::IOObjectRetain(source)==KERN_SUCCESS);Object current(source);
    for(unsigned depth=0;depth<32;++depth) {
        if(::IOObjectConformsTo(current.value,device?kIOUSBHostDeviceClassName:kIOUSBHostInterfaceClassName)||
            ::IOObjectConformsTo(current.value,device?"IOUSBDevice":"IOUSBInterface"))return current;
        io_registry_entry_t parent{};require(::IORegistryEntryGetParentEntry(current.value,kIOServicePlane,&parent)==KERN_SUCCESS);
        ::IOObjectRelease(current.value);current.value=parent;
    }
    throw std::runtime_error("USB registry ancestry exceeds its limit");
}
std::vector<std::uint8_t> configuration(io_registry_entry_t device,unsigned value) {
    IOCFPlugInInterface **plugin{};SInt32 score{};
    require(::IOCreatePlugInInterfaceForService(device,kIOUSBDeviceUserClientTypeID,kIOCFPlugInInterfaceID,&plugin,&score)==kIOReturnSuccess&&plugin);
    struct Plugin {IOCFPlugInInterface **value;~Plugin(){(*value)->Release(value);}} cleanup_plugin{plugin};
    IOUSBDeviceInterface **interface{};
    require((*plugin)->QueryInterface(plugin,::CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),reinterpret_cast<LPVOID *>(&interface))==S_OK&&interface);
    struct Device {IOUSBDeviceInterface **value;~Device(){(*value)->Release(value);}} cleanup_device{interface};
    UInt8 count{};require((*interface)->GetNumberOfConfigurations(interface,&count)==kIOReturnSuccess&&count&&count<=16);
    for(UInt8 index=0;index<count;++index) {
        IOUSBConfigurationDescriptorPtr descriptor{};
        // GetConfigurationDescriptorPtr reads OS-cached descriptor data and
        // explicitly does not require USBDeviceOpen (Apple IOUSBLib.h).
        require((*interface)->GetConfigurationDescriptorPtr(interface,index,&descriptor)==kIOReturnSuccess&&descriptor);
        const auto bytes=reinterpret_cast<const std::uint8_t *>(descriptor);
        require(bytes[0]>=9&&bytes[1]==2);
        if(bytes[5]!=value)continue;
        const auto size=unsigned(bytes[2])|(unsigned(bytes[3])<<8);require(size>=9&&size<=65535);
        return {bytes,bytes+size};
    }
    throw std::runtime_error("Active USB configuration descriptor is unavailable");
}
Object control_interface(io_registry_entry_t device,unsigned control) {
    io_iterator_t iterator{};require(::IORegistryEntryCreateIterator(device,kIOServicePlane,kIORegistryIterateRecursively,&iterator)==KERN_SUCCESS);
    Object children(iterator),result;
    unsigned count=0;
    while(const auto child=::IOIteratorNext(iterator)) {
        Object row(child);require(++count<=4096);
        if(!::IOObjectConformsTo(child,kIOUSBHostInterfaceClassName)&&!::IOObjectConformsTo(child,"IOUSBInterface"))continue;
        if(number(child,kUSBInterfaceNumber)!=control)continue;
        require(!result.value&&number(child,kUSBInterfaceClass)==2&&number(child,kUSBInterfaceSubClass)==2&&
            number(child,kUSBInterfaceProtocol)==0);
        result.value=std::exchange(row.value,0);
    }
    require(result.value);return result;
}
NativeCdcEndpoint endpoint(io_registry_entry_t serial) {
    const auto data=ancestor(serial,false),device=ancestor(data.value,true);
    require(text(device.value,kUSBProductString)=="WsprryPico");
    const auto active=number(data.value,kUSBConfigurationValue);
    const auto binding=cdc_descriptor_binding(configuration(device.value,active),number(data.value,kUSBInterfaceNumber));
    const auto control=control_interface(device.value,binding.control_interface);
    const auto callout=std::filesystem::canonical(text(serial,kIOCalloutDeviceKey)).string();
    const auto dialin=std::filesystem::canonical(text(serial,kIODialinDeviceKey)).string();
    struct stat info{};require(!::stat(callout.c_str(),&info)&&S_ISCHR(info.st_mode));
    struct stat dialin_info{};require(!::stat(dialin.c_str(),&dialin_info)&&S_ISCHR(dialin_info.st_mode));
    const auto vendor=number(device.value,kUSBVendorID),product=number(device.value,kUSBProductID);
    require(vendor&&product);
    const auto instance="iokit:"+std::to_string(registry_id(device.value));
    const auto path=instance+"/control:"+std::to_string(registry_id(control.value))+"/data:"+std::to_string(registry_id(data.value));
    const auto usb_serial=text(device.value,kUSBSerialNumberString);require(usb_serial.size()<=128);
    return {{callout,path,usb_serial,static_cast<std::uint64_t>(info.st_rdev),static_cast<std::uint16_t>(vendor),
        static_cast<std::uint16_t>(product),binding.control_interface},instance,"WsprryPico",text(control.value,kUSBInterfaceString),
        {{dialin,static_cast<std::uint64_t>(dialin_info.st_rdev)}}};
}
}
std::vector<NativeCdcEndpoint> macos_cdc_inventory() {
    io_iterator_t iterator{};
    require(::IOServiceGetMatchingServices(kIOMainPortDefault,::IOServiceMatching(kIOSerialBSDServiceValue),&iterator)==KERN_SUCCESS);
    Object services(iterator);std::vector<NativeCdcEndpoint> result;
    unsigned scanned=0;
    while(const auto serial=::IOIteratorNext(iterator)) {
        Object row(serial);require(++scanned<=4096);
        try {
            const auto interface=ancestor(serial,false),device=ancestor(interface.value,true);
            if(text(device.value,kUSBProductString)!="WsprryPico")continue;
        }catch(const std::exception &) {continue;} // Non-USB or unrelated serial service.
        // A recognized Pico with incomplete metadata makes discovery unavailable;
        // it must not disappear behind an apparently successful empty inventory.
        result.push_back(endpoint(serial));
    }
    return result;
}
}
#else
#include <stdexcept>
namespace wsprrypi {
std::vector<NativeCdcEndpoint> macos_cdc_inventory(){throw std::runtime_error("macOS USB metadata is unavailable on this host");}
}
#endif
