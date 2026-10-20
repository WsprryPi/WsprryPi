#include "Mailbox/src/mailbox_revision.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <unistd.h>

namespace
{
    class TemporaryCpuInfo
    {
    public:
        TemporaryCpuInfo()
        {
            std::string pattern =
                (std::filesystem::temp_directory_path() / "wsprrypi-cpuinfo-XXXXXX").string();
            const int fd = mkstemp(pattern.data());
            assert(fd >= 0);
            assert(::close(fd) == 0);
            path_ = std::move(pattern);
        }

        ~TemporaryCpuInfo()
        {
            std::error_code error;
            std::filesystem::remove(path_, error);
        }

        const std::string &path() const { return path_; }

        void write(const std::string &contents) const
        {
            std::ofstream output(path_, std::ios::trunc);
            assert(output);
            output << contents;
            assert(output.good());
        }

        void remove() const
        {
            assert(std::filesystem::remove(path_));
        }

    private:
        std::string path_;
    };

    void expectRuntimeError(
        mailbox_detail::RevisionResolver &resolver,
        const std::string &path,
        const std::string &message_fragment)
    {
        try
        {
            (void)resolver.memoryFlagFromCpuInfo(path.c_str());
            assert(false && "expected std::runtime_error");
        }
        catch (const std::runtime_error &error)
        {
            assert(std::string(error.what()).find(message_fragment) != std::string::npos);
        }
    }
}

int main()
{
    {
        TemporaryCpuInfo cpuinfo;
        cpuinfo.remove();
        mailbox_detail::RevisionResolver resolver;
        expectRuntimeError(resolver, cpuinfo.path(), "cannot open");
        cpuinfo.write("Revision\t: 0002\n");
        assert(resolver.memoryFlagFromCpuInfo(cpuinfo.path().c_str()) == 0x0CU);
    }

    {
        TemporaryCpuInfo cpuinfo;
        cpuinfo.write("Hardware\t: BCM2835\n");
        mailbox_detail::RevisionResolver resolver;
        expectRuntimeError(resolver, cpuinfo.path(), "Revision not found");
        cpuinfo.write("Revision : 0002\n");
        assert(resolver.memoryFlagFromCpuInfo(cpuinfo.path().c_str()) == 0x0CU);
    }

    for (const std::string malformed : {"", "0000", "not-hex", "a02082 trailing", "100000000"})
    {
        TemporaryCpuInfo cpuinfo;
        cpuinfo.write("Revision : " + malformed + "\n");
        mailbox_detail::RevisionResolver resolver;
        expectRuntimeError(resolver, cpuinfo.path(), "invalid Revision");
        cpuinfo.write("Revision : 0002\n");
        assert(resolver.memoryFlagFromCpuInfo(cpuinfo.path().c_str()) == 0x0CU);
    }

    {
        TemporaryCpuInfo cpuinfo;
        cpuinfo.write("Revision : 0002\n");
        mailbox_detail::RevisionResolver resolver;
        assert(resolver.memoryFlagFromCpuInfo(cpuinfo.path().c_str()) == 0x0CU);
        cpuinfo.write("Revision : 803000\n");
        assert(resolver.memoryFlagFromCpuInfo(cpuinfo.path().c_str()) == 0x0CU);
    }

    for (const std::string revision : {"800000", "801000", "802000", "803000"})
    {
        TemporaryCpuInfo cpuinfo;
        cpuinfo.write("Revision\t: " + revision + "\n");
        mailbox_detail::RevisionResolver resolver;
        const uint32_t expected = revision == "800000" ? 0x0CU : 0x04U;
        assert(resolver.memoryFlagFromCpuInfo(cpuinfo.path().c_str()) == expected);
    }

    {
        TemporaryCpuInfo cpuinfo;
        cpuinfo.write("Revision : 804000\n");
        mailbox_detail::RevisionResolver resolver;
        expectRuntimeError(resolver, cpuinfo.path(), "unknown chipset");
    }
}
