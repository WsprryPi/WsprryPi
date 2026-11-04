#include "mcp2221a_protocol.hpp"
#ifdef MCP2221A_DEVICE_INTEGRATION
#include "si5351_device.hpp"
#endif
#include <deque>
#include <iostream>
#include <stdexcept>
using namespace mcp2221a;
namespace {
int checks = 0;
void expect(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
struct Reply { Report value{}; std::ptrdiff_t length = 64; };
class Fake final : public Channel {
public:
    bool open(const std::string& selected) override { ++opens; return !selected.empty() && open_ok; }
    bool close() override { ++closes; return close_ok; }
    std::ptrdiff_t exchange(const Report& request, Report& response, Deadline deadline) override {
        expect(deadline > std::chrono::steady_clock::now(), "deadline supplied");
        sent.push_back(request);
        if (replies.empty()) throw std::runtime_error("unscripted request");
        const auto reply = replies.front(); replies.pop_front(); response = reply.value; return reply.length;
    }
    std::string diagnostic() const override { return "injected HID failure"; }
    void reply(std::uint8_t command, std::uint8_t state = 0, std::uint8_t count = 0) {
        Report result{}; result[0] = command; result[8] = state; result[9] = count; result[11] = count;
        replies.push_back({result});
    }
    void startup() { reply(0x10); reply(0x10); replies.back().value[3] = 0x20; replies.back().value[4] = 117; }
    std::deque<Reply> replies; std::vector<Report> sent;
    bool open_ok = true, close_ok = true;
    int opens = 0, closes = 0;
};
const std::string selected = identity("serial", 0);
void identity_tests() {
    Endpoint item{identity("", 0x12340000), 42, 0x12340000}, result;
    std::string error;
    expect(resolve({{item}, {}}, item.identity, result, error), "topology resolves");
    expect(!resolve({{item,item}, {}}, item.identity, result, error), "duplicate rejected");
    expect(!resolve({{}, {}}, item.identity, result, error), "missing rejected");
    expect(!resolve({{item}, {}}, "", result, error), "empty rejected");
    expect(!resolve({{item}, "denied"}, item.identity, result, error), "discovery failure rejected");
    auto serial = item; serial.identity = identity("new serial", item.location);
    expect(!resolve({{serial}, {}}, item.identity, result, error), "serial appearing invalidates topology");
    serial = item; ++serial.generation;
    expect(!(serial == item), "detach reattach invalidates binding");
    auto invalid = item; invalid.reports64 = false;
    expect(!resolve({{invalid}, {}}, item.identity, result, error), "report capability mismatch rejected");
    expect(identity("", 0).empty(), "missing location is not invented");
}
void transactions() {
    auto channel = std::make_shared<Fake>(); channel->startup();
    Transport transport(selected, 0x60, channel);
    expect(transport.open(), "startup");
    expect(transport.cancelActive() && channel->sent.size() == 2, "idle cancellation sends no command");
    expect(channel->sent[0][2] == 0 && channel->sent[1][2] == 0, "no idle cancellation");
    expect(channel->sent[1][3] == 0x20 && channel->sent[1][4] == 117, "100 kHz speed");
    std::uint8_t bytes[9] = {26,1,2,3,4,5,6,7,8};
    channel->reply(0x90); channel->reply(0x10,0x41); channel->reply(0x10,0,9);
    expect(transport.write(bytes,9) == 9, "burst completes after busy status");
    expect(channel->sent[2][1] == 9 && channel->sent[2][3] == 0xc0 && channel->sent[2][12] == 8, "burst mapping");
    channel->reply(0x90); channel->reply(0x10,0,1);
    expect(transport.write(bytes,1) == 1, "pointer write completes with STOP");
    channel->reply(0x91); channel->reply(0x40); channel->replies.back().value[2] = 0x54;
    channel->reply(0x40); channel->replies.back().value[2] = 0x55; channel->replies.back().value[3] = 1; channel->replies.back().value[4] = 0x11;
    channel->reply(0x10,0,1);
    std::uint8_t value = 0;
    expect(transport.read(&value,1) == 1 && value == 0x11, "read returned only after completion");
    expect(channel->sent[7][0] == 0x91 && channel->sent[7][3] == 0xc1, "read address mapping");
    expect(transport.close(), "close success");
    for (const auto& request : channel->sent) expect(request[2] != 0x10, "idle cleanup never cancels");
}
#ifdef MCP2221A_DEVICE_INTEGRATION
void device_transactions() {
    auto channel=std::make_shared<Fake>(); channel->startup();
    auto transport=std::make_shared<Transport>(selected,0x60,channel);
    Si5351Device::Config config; config.adapter_identity=selected; config.optimize_register_writes=true;
    Si5351Device device(config,transport);
    expect(device.open(),"production Si5351 device opens typed HID transport");
    channel->reply(0x90); channel->reply(0x10,0,2);
    expect(device.initialize(),"startup output inhibition uses completed I2C write");
    expect(channel->sent[2][4]==3 && channel->sent[2][5]==255,"all outputs disabled before programming");
    channel->reply(0x90); channel->reply(0x10,0,1);
    channel->reply(0x91); channel->reply(0x40); channel->replies.back().value[2]=0x55;
    channel->replies.back().value[3]=1; channel->replies.back().value[4]=255;
    channel->reply(0x10,0,1);
    std::uint8_t readback=0;
    expect(device.readRegister(3,readback) && readback==255,"production pointer STOP/read path");
    std::vector<Si5351Device::RegisterWrite> writes;
    for (std::uint8_t reg=26; reg<34; ++reg) writes.push_back({reg,static_cast<std::uint8_t>(reg+1)});
    channel->reply(0x90); channel->reply(0x10,0,9);
    expect(device.writeRegisters(writes),"production parameter block maps to one completed HID write");
    const auto& burst=channel->sent[channel->sent.size()-2];
    expect(burst[1]==9 && burst[4]==26 && burst[12]==34,"production burst register bounds");
    channel->reply(0x90); channel->replies.back().length=63;
    expect(!device.disableAllOutputs() && device.getLastError().find("unconfirmed")!=std::string::npos,"lost HID response exposes unconfirmed inhibition");
    expect(!device.enableOutput(Si5351Device::Output::CLK0),"fault cannot re-enable output");
    expect(!device.close(),"uncertain active transfer remains failed in cleanup");
}
#endif

void failures() {
    for (int fault=0; fault<12; ++fault) {
        auto channel=std::make_shared<Fake>(); channel->startup(); Transport transport(selected,0x60,channel);
        expect(transport.open(), "fault fixture startup");
        channel->reply(0x90);
        if (fault == 0) channel->replies.back().length = 63;
        if (fault == 1) channel->replies.back().value[0] = 0x91;
        if (fault == 2) channel->replies.back().length = -1;
        if (fault == 3) channel->replies.back().value[1] = 1;
        if (fault == 10) channel->replies.back().length = 0;
        if (fault == 11) channel->replies.back().length = 65;
        if (fault >= 4) {
            channel->reply(0x10,0,2);
            if (fault == 4) channel->replies.back().value[8] = 0x25;
            if (fault == 5) channel->replies.back().value[8] = 0x62;
            if (fault == 6) channel->replies.back().value[11] = 1;
            if (fault == 7) channel->replies.back().value[8] = 0xfe;
            if (fault == 8) channel->replies.back().value[20] = 0x40;
            if (fault == 9) channel->replies.back().value[1] = 0xff;
        }
        const std::uint8_t bytes[] = {3,0xff};
        expect(transport.write(bytes,2) == -1, "fault rejected");
        const auto sent=channel->sent.size();
        expect(transport.write(bytes,2) == -1 && channel->sent.size() == sent, "fault is latched without retry");
        const auto error=transport.diagnostic(); channel->close_ok=false;
        if (fault >= 4) channel->reply(0x10);
        expect(!transport.close() && transport.diagnostic().find(error) == 0 && transport.diagnostic().find("cleanup failed") != std::string::npos, "primary failure survives close failure");
        expect(!transport.open() && channel->opens == 1, "faulted session cannot reopen automatically");
        expect(!transport.close(), "repeated close retains cleanup failure");
    }
    {
        auto channel=std::make_shared<Fake>(); channel->reply(0x10,0x62); Transport transport(selected,0x60,channel);
        expect(!transport.open() && channel->sent.size()==1 && channel->closes==1, "initial STOP timeout refuses speed and cancel");
    }
    {
        auto channel=std::make_shared<Fake>(); channel->startup(); channel->replies.back().value[3]=0x21;
        Transport transport(selected,0x60,channel); expect(!transport.open(), "speed refusal");
    }
    {
        auto channel=std::make_shared<Fake>(); channel->open_ok=false;
        Transport transport(selected,0x60,channel);
        expect(!transport.open() && channel->sent.empty() && channel->closes==0,"denied HID access performs no transactions");
    }
    {
        auto channel=std::make_shared<Fake>(); Transport transport(selected,0x80,channel);
        expect(!transport.open() && channel->opens==0,"invalid address rejected before opening HID");
    }
    for (const std::size_t size : {0u,10u}) {
        auto channel=std::make_shared<Fake>(); channel->startup(); Transport transport(selected,0x60,channel);
        expect(transport.open(),"write size fixture");
        std::uint8_t bytes[10]{};
        expect(transport.write(bytes,size)==-1 && channel->sent.size()==2,"unsupported write length refuses before HID mutation");
    }
    for (const bool stop_timeout : {false, true}) {
        auto channel=std::make_shared<Fake>(); channel->startup(); Transport transport(selected,0x60,channel); expect(transport.open(),"cancel fixture");
        channel->reply(0x90);
        for (int poll=0; poll<100; ++poll) channel->reply(0x10,0x41);
        std::uint8_t bytes[2]={3,255}; expect(transport.write(bytes,2)==-1,"busy poll count bounded");
        const auto primary=transport.diagnostic();
        channel->reply(0x10,0x41); channel->reply(0x10); channel->replies.back().value[2]=0x10;
        channel->reply(0x10,stop_timeout ? 0x62 : 0);
        expect(transport.close() != stop_timeout,"cancel acknowledgment is not engine recovery");
        expect(transport.diagnostic().find(primary)==0,"cancellation preserves primary failure");
        expect(channel->sent[channel->sent.size()-2][2]==0x10,"active cancel report");
        if (stop_timeout) expect(transport.diagnostic().find("0x62") != std::string::npos, "cancel reports actual STOP timeout state");
    }
    for (const auto length : {0,2,127}) {
        auto channel=std::make_shared<Fake>(); channel->startup(); Transport transport(selected,0x60,channel); expect(transport.open(),"read fixture");
        channel->reply(0x91); channel->reply(0x40); channel->replies.back().value[2]=0x55; channel->replies.back().value[3]=length;
        std::uint8_t value=0xee; expect(transport.read(&value,1)==-1 && value==0xee,"partial read preserves caller value");
        channel->reply(0x10); expect(transport.close(), "partial read closes without idle cancel");
    }
}
}
int main() {
    try {
        identity_tests(); transactions(); failures();
#ifdef MCP2221A_DEVICE_INTEGRATION
        device_transactions();
#endif
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "MCP2221A protocol checks passed: " << checks << '\n';
}
