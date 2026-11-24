// SPDX-License-Identifier: MIT
#ifdef _WIN32
#include "usb_cdc_native.hpp"
#include "native_windows_file.hpp"
#include <array>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <map>
#include <system_error>
namespace wsprrypi {
namespace {
namespace wf=windows_file;
void checked(BOOL result){if(!result)throw std::system_error(static_cast<int>(::GetLastError()),std::system_category(),"Windows CDC operation");}
void require(bool value){if(!value)throw std::runtime_error("Windows CDC endpoint or operation changed");}
struct Operation {
    wf::Handle event{::CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    OVERLAPPED overlapped{};std::array<std::uint8_t,UsbCdcStream::io_chunk_bytes> bytes{};
    DWORD size{},count{};bool pending{},ready{};
    Operation(){checked(event.get()!=nullptr);overlapped.hEvent=event.get();}
    void reset(){require(!pending);checked(::ResetEvent(event.get()));overlapped={};overlapped.hEvent=event.get();ready=false;count=0;size=0;}
    bool completed(HANDLE port){
        if(!pending)return ready;
        if(::GetOverlappedResult(port,&overlapped,&count,FALSE)){pending=false;ready=true;require(count<=size);return true;}
        const auto error=::GetLastError();if(error==ERROR_IO_INCOMPLETE)return false;
        pending=false;throw std::system_error(static_cast<int>(error),std::system_category(),"Windows CDC completion");
    }
    void cancel(HANDLE port) noexcept {
        if(pending){
            (void)::CancelIoEx(port,&overlapped);
            if(::WaitForSingleObject(event.get(),2000)!=WAIT_OBJECT_0){
                // A malfunctioning driver cannot retain references to freed
                // OVERLAPPED state/buffers in a continuing controller process.
                ::TerminateProcess(::GetCurrentProcess(),ERROR_OPERATION_ABORTED);std::abort();
            }
            DWORD ignored{};(void)::GetOverlappedResult(port,&overlapped,&ignored,FALSE);pending=false;
        }
        ready=false;count=0;size=0;
    }
};
struct Port {
    wf::Handle handle;CdcIdentity identity;Operation input,output;
    Port(wf::Handle value,CdcIdentity id):handle(std::move(value)),identity(std::move(id)){}
    ~Port(){input.cancel(handle.get());output.cancel(handle.get());}
};
std::wstring endpoint_path(const std::string &path){
    require(path.size()<=4096&&path.starts_with("\\\\?\\")&&std::all_of(path.begin(),path.end(),[](unsigned char c){return c>=32&&c<127&&c!='"'&&c!='/';}));
    return {path.begin(),path.end()};
}
}
struct WindowsCdcSystem::Impl {
    std::map<int,std::unique_ptr<Port>> ports;int next{};
    Port &port(int fd){const auto found=ports.find(fd);require(found!=ports.end());return *found->second;}
};
WindowsCdcSystem::WindowsCdcSystem():impl_(std::make_unique<Impl>()){}
WindowsCdcSystem::~WindowsCdcSystem()=default;
CdcIdentity WindowsCdcSystem::inspect(const std::string &path){return inspect_native_cdc(path);}
int WindowsCdcSystem::open(const std::string &path,int flags){
    require(!flags&&impl_->next<std::numeric_limits<int>::max());const auto identity=inspect(path);
    wf::Handle handle(::CreateFileW(endpoint_path(identity.path).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));
    checked(handle.get()!=INVALID_HANDLE_VALUE);checked(::SetHandleInformation(handle.get(),HANDLE_FLAG_INHERIT,0));
    COMMPROP properties{};properties.wPacketLength=sizeof properties;checked(::GetCommProperties(handle.get(),&properties));
    require(inspect(path)==identity);const auto fd=impl_->next++;
    impl_->ports.emplace(fd,std::make_unique<Port>(std::move(handle),identity));return fd;
}
void WindowsCdcSystem::verify(int fd,const CdcIdentity &identity){
    const auto &port=impl_->port(fd);require(port.identity==identity&&inspect(identity.path)==identity);
    COMMPROP properties{};properties.wPacketLength=sizeof properties;checked(::GetCommProperties(port.handle.get(),&properties));
}
void WindowsCdcSystem::exclusive(int fd){(void)impl_->port(fd);} // CreateFile share mode is zero.
void WindowsCdcSystem::unexclusive(int fd){(void)impl_->port(fd);} // Released only by close.
void WindowsCdcSystem::raw(int fd){
    auto &port=impl_->port(fd);DCB settings{};settings.DCBlength=sizeof settings;checked(::GetCommState(port.handle.get(),&settings));
    settings.BaudRate=CBR_115200;settings.ByteSize=8;settings.Parity=NOPARITY;settings.StopBits=ONESTOPBIT;
    settings.fBinary=TRUE;settings.fParity=FALSE;settings.fOutxCtsFlow=FALSE;settings.fOutxDsrFlow=FALSE;
    settings.fDtrControl=DTR_CONTROL_DISABLE;settings.fDsrSensitivity=FALSE;settings.fTXContinueOnXoff=TRUE;
    settings.fOutX=FALSE;settings.fInX=FALSE;settings.fErrorChar=FALSE;settings.fNull=FALSE;
    settings.fRtsControl=RTS_CONTROL_DISABLE;settings.fAbortOnError=FALSE;checked(::SetCommState(port.handle.get(),&settings));
    DCB actual{};actual.DCBlength=sizeof actual;checked(::GetCommState(port.handle.get(),&actual));
    require(actual.BaudRate==CBR_115200&&actual.ByteSize==8&&actual.Parity==NOPARITY&&actual.StopBits==ONESTOPBIT&&actual.fBinary&&
        !actual.fParity&&!actual.fOutxCtsFlow&&!actual.fOutxDsrFlow&&actual.fDtrControl==DTR_CONTROL_DISABLE&&!actual.fDsrSensitivity&&
        actual.fTXContinueOnXoff&&!actual.fOutX&&!actual.fInX&&!actual.fErrorChar&&!actual.fNull&&actual.fRtsControl==RTS_CONTROL_DISABLE&&!actual.fAbortOnError);
    COMMTIMEOUTS timeouts{};timeouts.ReadIntervalTimeout=MAXDWORD;timeouts.WriteTotalTimeoutConstant=2000;
    checked(::SetCommTimeouts(port.handle.get(),&timeouts));COMMTIMEOUTS observed{};checked(::GetCommTimeouts(port.handle.get(),&observed));
    require(observed.ReadIntervalTimeout==MAXDWORD&&!observed.ReadTotalTimeoutMultiplier&&!observed.ReadTotalTimeoutConstant&&
        !observed.WriteTotalTimeoutMultiplier&&observed.WriteTotalTimeoutConstant==2000);
}
void WindowsCdcSystem::dtr(int fd,bool asserted){checked(::EscapeCommFunction(impl_->port(fd).handle.get(),asserted?SETDTR:CLRDTR));}
void WindowsCdcSystem::discard(int fd){
    auto &port=impl_->port(fd);port.input.cancel(port.handle.get());port.output.cancel(port.handle.get());
    checked(::PurgeComm(port.handle.get(),PURGE_RXABORT|PURGE_RXCLEAR|PURGE_TXABORT|PURGE_TXCLEAR));
}
CdcPoll WindowsCdcSystem::poll(int fd,bool writing){
    try{
        auto &port=impl_->port(fd);auto &operation=writing?port.output:port.input;
        if(operation.pending&&!operation.completed(port.handle.get()))return {0,0,0};
        if(operation.ready)return {1,writing?CdcPollWritable:CdcPollReadable,0};
        DWORD errors{};COMSTAT status{};checked(::ClearCommError(port.handle.get(),&errors,&status));require(!errors);
        return writing?CdcPoll{1,CdcPollWritable,0}:status.cbInQue?CdcPoll{1,CdcPollReadable,0}:CdcPoll{0,0,0};
    }catch(...){return {-1,0,EIO};}
}
CdcTransfer WindowsCdcSystem::read(int fd,std::span<std::uint8_t> bytes){
    try{
        auto &port=impl_->port(fd);auto &operation=port.input;require(!bytes.empty()&&bytes.size()<=operation.bytes.size());
        if(operation.pending&&!operation.completed(port.handle.get()))return {-1,EAGAIN};
        if(!operation.ready){
            operation.reset();operation.size=static_cast<DWORD>(bytes.size());
            if(::ReadFile(port.handle.get(),operation.bytes.data(),operation.size,&operation.count,&operation.overlapped))operation.ready=true;
            else {const auto error=::GetLastError();if(error!=ERROR_IO_PENDING)throw std::system_error(static_cast<int>(error),std::system_category());operation.pending=true;return {-1,EAGAIN};}
        }
        require(operation.count<=operation.size);const auto count=std::min<std::size_t>(bytes.size(),operation.count);
        std::copy_n(operation.bytes.begin(),count,bytes.begin());
        operation.count-=static_cast<DWORD>(count);
        if(operation.count)std::move(operation.bytes.begin()+count,operation.bytes.begin()+count+operation.count,operation.bytes.begin());
        else operation.reset();
        return {static_cast<std::ptrdiff_t>(count),0};
    }catch(...){return {-1,EIO};}
}
CdcTransfer WindowsCdcSystem::write(int fd,std::span<const std::uint8_t> bytes){
    try{
        auto &port=impl_->port(fd);auto &operation=port.output;require(!bytes.empty()&&bytes.size()<=operation.bytes.size());
        if(operation.pending&&!operation.completed(port.handle.get()))return {-1,EAGAIN};
        if(operation.ready){
            require(bytes.size()>=operation.size&&std::equal(operation.bytes.begin(),operation.bytes.begin()+operation.size,bytes.begin()));
            const auto count=operation.count;require(count>0&&count<=operation.size);operation.reset();return {static_cast<std::ptrdiff_t>(count),0};
        }
        operation.reset();operation.size=static_cast<DWORD>(bytes.size());std::copy(bytes.begin(),bytes.end(),operation.bytes.begin());
        if(::WriteFile(port.handle.get(),operation.bytes.data(),operation.size,&operation.count,&operation.overlapped)){
            require(operation.count>0&&operation.count<=operation.size);const auto count=operation.count;operation.reset();return {static_cast<std::ptrdiff_t>(count),0};
        }
        const auto error=::GetLastError();if(error!=ERROR_IO_PENDING)throw std::system_error(static_cast<int>(error),std::system_category());
        operation.pending=true;return {-1,EAGAIN};
    }catch(...){return {-1,EIO};}
}
int WindowsCdcSystem::close(int fd) noexcept {
    const auto found=impl_->ports.find(fd);if(found==impl_->ports.end())return EINVAL;
    impl_->ports.erase(found);return 0;
}
}
#endif
