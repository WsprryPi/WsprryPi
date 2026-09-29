#include "wtp_endpoint/identity.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

int main() {
    char path[] = "/tmp/wtp-pi-machine-id-XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0) throw std::runtime_error("Unable to create identity fixture");
    close(fd);
    {
        std::ofstream output(path);
        output << "0123456789abcdef0123456789abcdef\n";
    }
    const auto first = wsprrypi::wtp_pi_device_id(path);
    const auto second = wsprrypi::wtp_pi_device_id(path);
    unlink(path);
    if (first.size() != 32 || first != second)
        throw std::runtime_error("Device identity must be stable and canonical");
    wsprrypi::WtpPiBootIdentity boot;
    const auto prior = boot.new_boot_id();
    const auto next = boot.new_boot_id();
    if (prior.size() != 32 || next.size() != 32 || prior == next)
        throw std::runtime_error("Boot identities must be fresh");
}
