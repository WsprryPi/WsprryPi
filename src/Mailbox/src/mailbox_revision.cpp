#include "mailbox_revision.hpp"

#include "bcm_model.hpp"

#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace mailbox_detail
{
    uint32_t RevisionResolver::memoryFlagFromCpuInfo(const char *path)
    {
        if (!cached_revision_)
            cached_revision_ = readRevision(path);

        return memoryFlagFromRevision(*cached_revision_);
    }

    uint32_t RevisionResolver::readRevision(const char *path)
    {
        std::ifstream input(path);
        if (!input)
        {
            throw std::runtime_error(
                std::string("Mailbox::get_mem_flag(): cannot open ") + path);
        }

        const auto trim = [](std::string_view value) {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return std::string_view{};
            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        };

        std::string line;
        while (std::getline(input, line))
        {
            const auto colon = line.find(':');
            if (colon == std::string::npos ||
                trim(std::string_view(line).substr(0, colon)) != "Revision")
            {
                continue;
            }

            const auto value = trim(std::string_view(line).substr(colon + 1));
            uint32_t revision = 0;
            const auto result = std::from_chars(
                value.data(), value.data() + value.size(), revision, 16);
            if (value.empty() || result.ec != std::errc{} ||
                result.ptr != value.data() + value.size() || revision == 0)
            {
                throw std::runtime_error(
                    std::string("Mailbox::get_mem_flag(): invalid Revision in ") + path);
            }
            return revision;
        }

        if (input.bad())
        {
            throw std::runtime_error(
                std::string("Mailbox::get_mem_flag(): cannot read ") + path);
        }

        throw std::runtime_error(
            std::string("Mailbox::get_mem_flag(): Revision not found in ") + path);
    }

    uint32_t RevisionResolver::memoryFlagFromRevision(uint32_t revision)
    {
        const BCMChip processor = (revision & 0x800000U)
                                      ? static_cast<BCMChip>((revision & 0xF000U) >> 12U)
                                      : BCMChip::BCM_HOST_PROCESSOR_BCM2835;

        switch (processor)
        {
        case BCMChip::BCM_HOST_PROCESSOR_BCM2835:
            return 0x0C;
        case BCMChip::BCM_HOST_PROCESSOR_BCM2836:
        case BCMChip::BCM_HOST_PROCESSOR_BCM2837:
        case BCMChip::BCM_HOST_PROCESSOR_BCM2711:
            return 0x04;
        }
        throw std::runtime_error(
            std::string("Mailbox::get_mem_flag(): unknown chipset ") +
            std::string(to_string(processor)));
    }
}
