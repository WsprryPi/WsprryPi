#ifndef MAILBOX_REVISION_HPP
#define MAILBOX_REVISION_HPP
#pragma once

#include <cstdint>
#include <optional>

namespace mailbox_detail
{
    class RevisionResolver
    {
    public:
        [[nodiscard]] uint32_t memoryFlagFromCpuInfo(const char *path);

    private:
        [[nodiscard]] static uint32_t readRevision(const char *path);
        [[nodiscard]] static uint32_t memoryFlagFromRevision(uint32_t revision);

        std::optional<uint32_t> cached_revision_;
    };
}

#endif // MAILBOX_REVISION_HPP
