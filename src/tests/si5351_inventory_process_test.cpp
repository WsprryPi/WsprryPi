#include "si5351_inventory_process.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <thread>
#include <future>
#include <poll.h>
#include <sys/stat.h>
#include <cstdlib>
#include <unistd.h>

using namespace std::chrono_literals;
void require(bool value, const char* stage) {
    if (!value) { std::cerr << "stage=" << stage << "; expected=acknowledged transition; hang_guard=5s\n"; std::abort(); }
}
struct Gate {
    std::string directory;
    int ready = -1, release = -1;
    Gate() {
        char name[] = "/tmp/wsprrypi-inventory-gate-XXXXXX";
        require(::mkdtemp(name), "inventory-gate-root"); directory = name;
        require(::mkfifo((directory+"/ready").c_str(),0600)==0 &&
                ::mkfifo((directory+"/release").c_str(),0600)==0,"inventory-gate-create");
        ready = ::open((directory+"/ready").c_str(),O_RDWR|O_NONBLOCK);
        release = ::open((directory+"/release").c_str(),O_RDWR|O_NONBLOCK);
        require(ready>=0 && release>=0,"inventory-gate-open");
    }
    void entered() {
        pollfd descriptor{ready,POLLIN,0};
        require(::poll(&descriptor,1,5000)==1,"inventory-worker-entry");
        char value = 0; require(::read(ready,&value,1)==1 && value=='R',"inventory-worker-ack");
    }
    void finish() { require(::write(release,"x",1)==1,"inventory-worker-release"); }
    ~Gate() { ::close(ready); ::close(release); std::filesystem::remove_all(directory); }
};
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "descriptor") {
        errno = 0;
        return ::fcntl(std::stoi(argv[2]), F_GETFD) == -1 && errno == EBADF ? 0 : 5;
    }
    if (argc == 3 && std::string(argv[1]) == "gate") {
        const std::string root(argv[2]);
        const int ready = ::open((root+"/ready").c_str(),O_WRONLY);
        if (ready<0 || ::write(ready,"R",1)!=1) return 6;
        ::close(ready);
        const int release = ::open((root+"/release").c_str(),O_RDONLY|O_NONBLOCK);
        pollfd descriptor{release,POLLIN,0};
        char value = 0;
        if (release<0 || ::poll(&descriptor,1,10000)!=1 || ::read(release,&value,1)!=1 || value!='x') return 7;
        ::close(release); std::cout << "ready"; return 0;
    }
    if (argc == 2) {
        const std::string mode(argv[1]);
        if (mode == "large") { std::cout << std::string(5000, 'x'); return 0; }
        if (mode == "fail") return 4;
        if (mode == "success") { std::cout << "ready"; return 0; }
    }
    const std::string self = std::filesystem::canonical(argv[0]).string();
    auto result = si5351_inventory_process::run({self, "success"});
    assert(result.error.empty() && result.output == "ready");
    const int inherited = ::open("/dev/null", O_RDONLY);
    assert(inherited >= 3);
    assert(si5351_inventory_process::run({self, "descriptor", std::to_string(inherited)}).error.empty());
    ::close(inherited);
    assert(!si5351_inventory_process::run({self, "large"}).error.empty());
    assert(!si5351_inventory_process::run({self, "fail"}).error.empty());
    assert(!si5351_inventory_process::run({"/no/such/inventory-worker"}).error.empty());
    {
        Gate gate;
        result = si5351_inventory_process::run_for_test({self,"gate",gate.directory},100ms,[&] { gate.entered(); });
        require(result.error.find("timed out")!=std::string::npos && result.output.empty(),"inventory-operation-timeout");
        // Reap/reuse is an observed state, not a guessed post-kill delay.
        const auto deadline = std::chrono::steady_clock::now()+5s;
        do {
            result = si5351_inventory_process::run({self,"success"});
            if (result.error.find("still stopping")==std::string::npos) break;
            ::poll(nullptr,0,1); // Pacing only; the next result decides completion.
        } while (std::chrono::steady_clock::now()<deadline);
        require(result.error.empty() && result.output=="ready","inventory-reaped-and-reusable");
    }
    assert(!si5351_inventory_process::run({}).error.empty());
    ::close(STDIN_FILENO);
    assert(si5351_inventory_process::run({self, "success"}).output == "ready");
    Gate gate;
    std::promise<void> entered;
    auto entry = entered.get_future();
    std::promise<void> checked;
    auto refusal_checked = checked.get_future();
    std::promise<si5351_inventory_process::Reply> completed;
    auto completion = completed.get_future();
    std::thread worker([&] {
        completed.set_value(si5351_inventory_process::run_for_test({self,"gate",gate.directory},2s,[&] {
            gate.entered(); entered.set_value();
            require(refusal_checked.wait_for(5s)==std::future_status::ready,"inventory-refusal-release");
        }));
    });
    require(entry.wait_for(5s)==std::future_status::ready,"inventory-concurrent-entry");
    result = si5351_inventory_process::run({self, "success"});
    require(result.output.empty() && result.error.find("already running")!=std::string::npos,
            "inventory-concurrent-refusal");
    gate.finish();
    checked.set_value();
    require(completion.wait_for(5s)==std::future_status::ready,"inventory-worker-completion");
    const auto finished = completion.get();
    require(finished.error.empty() && finished.output=="ready","inventory-released-result");
    worker.join();
    assert(si5351_inventory_process::run({self, "success"}).output == "ready");
    std::cout << "Bounded Si5351 inventory subprocess tests passed\n";
}
