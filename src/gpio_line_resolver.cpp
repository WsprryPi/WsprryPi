#include "gpio_line_resolver.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <vector>

namespace
{
    struct ChipEntry
    {
        std::filesystem::path path;
        std::filesystem::path backing_device_path;
        std::string name;
        std::string label;
        std::size_t num_lines = 0;
    };

    std::filesystem::path gpiochip_sysfs_chip_path(
        const std::filesystem::path &chip_path)
    {
        struct stat st
        {
        };
        if (::stat(chip_path.c_str(), &st) != 0 || !S_ISCHR(st.st_mode))
        {
            return {};
        }

        std::ostringstream dev_char;
        dev_char << "/sys/dev/char/"
                 << major(st.st_rdev)
                 << ": "
                 << minor(st.st_rdev);

        std::error_code ec;
        const std::filesystem::path canonical =
            std::filesystem::canonical(dev_char.str(), ec);
        if (ec)
        {
            return {};
        }

        return canonical;
    }

    std::filesystem::path gpiochip_backing_device_path(
        const std::filesystem::path &chip_path)
    {
        return gpiochip_sysfs_chip_path(chip_path);
    }

    int gpiochip_number(const std::filesystem::path &chip_path)
    {
        const std::string filename = chip_path.filename().string();
        constexpr char prefix[] = "gpiochip";
        if (filename.rfind(prefix, 0) != 0)
        {
            return -1;
        }

        try
        {
            return std::stoi(filename.substr(sizeof(prefix) - 1U));
        }
        catch (...)
        {
            return -1;
        }
    }

    bool chip_path_less(
        const std::filesystem::path &lhs,
        const std::filesystem::path &rhs)
    {
        const int lhs_num = gpiochip_number(lhs);
        const int rhs_num = gpiochip_number(rhs);
        if (lhs_num >= 0 && rhs_num >= 0 && lhs_num != rhs_num)
        {
            return lhs_num < rhs_num;
        }

        return lhs < rhs;
    }

    bool resolved_chip_less(
        const ResolvedGPIOLine &lhs,
        const ResolvedGPIOLine &rhs)
    {
        return chip_path_less(lhs.chip_path, rhs.chip_path);
    }

    bool collapse_equivalent_candidates(
        const std::vector<ResolvedGPIOLine> &candidates,
        ResolvedGPIOLine &resolved_line)
    {
        if (candidates.empty())
        {
            return false;
        }

        std::set<std::filesystem::path> backing_devices;
        for (const ResolvedGPIOLine &candidate : candidates)
        {
            if (candidate.backing_device_path.empty())
            {
                return false;
            }

            backing_devices.insert(candidate.backing_device_path);
        }

        if (backing_devices.size() != 1U)
        {
            return false;
        }

        const auto canonical_it =
            std::min_element(candidates.begin(), candidates.end(), resolved_chip_less);
        if (canonical_it == candidates.end())
        {
            return false;
        }

        resolved_line = *canonical_it;
        std::ostringstream note;
        note << "Multiple gpiochips map to the same backing device '"
             << resolved_line.backing_device_path
             << "'. Using '" << resolved_line.chip_path
             << "' as canonical view.";
        resolved_line.resolution_note = note.str();
        return true;
    }

#if GPIOD_API_MAJOR < 2
    std::string trim_copy(std::string value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return {};
        }

        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1U);
    }

    std::string read_trimmed_text_file(const std::filesystem::path &path)
    {
        std::ifstream input(path);
        if (!input)
        {
            return {};
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        return trim_copy(buffer.str());
    }
#endif

    std::vector<ChipEntry> enumerate_gpiochips()
    {
        std::vector<ChipEntry> entries;
        const std::filesystem::path dev_dir{"/dev"};

        for (const auto &entry : std::filesystem::directory_iterator(dev_dir))
        {
            if (!entry.is_character_file() && !entry.is_symlink())
            {
                continue;
            }

            const std::string filename = entry.path().filename().string();
            if (filename.rfind("gpiochip", 0) != 0)
            {
                continue;
            }

#if GPIOD_API_MAJOR >= 2
            gpiod::chip chip(entry.path());
            const gpiod::chip_info info = chip.get_info();
            entries.push_back(ChipEntry{
                entry.path(),
                gpiochip_backing_device_path(entry.path()),
                info.name(),
                info.label(),
                info.num_lines()});
#else
            ChipEntry chip_entry;
            chip_entry.path = entry.path();
            chip_entry.backing_device_path =
                gpiochip_backing_device_path(chip_entry.path);
            chip_entry.name = filename;

            const std::filesystem::path sysfs_dir =
                gpiochip_sysfs_chip_path(chip_entry.path);
            if (std::filesystem::exists(sysfs_dir))
            {
                chip_entry.label = read_trimmed_text_file(sysfs_dir / "label");

                const std::string num_lines_text =
                    read_trimmed_text_file(sysfs_dir / "ngpio");
                if (!num_lines_text.empty())
                {
                    try
                    {
                        chip_entry.num_lines =
                            static_cast<std::size_t>(std::stoul(num_lines_text));
                    }
                    catch (...)
                    {
                        chip_entry.num_lines = 0;
                    }
                }
            }

            entries.push_back(std::move(chip_entry));
#endif
        }

        std::sort(
            entries.begin(),
            entries.end(),
            [](const ChipEntry &lhs, const ChipEntry &rhs)
            {
                return chip_path_less(lhs.path, rhs.path);
            });

        return entries;
    }

    bool try_resolve_line_on_chip(
        const ChipEntry &entry,
        int bcm,
        ResolvedGPIOLine &resolved_line,
        bool &used_offset_fallback)
    {
        gpiod::chip chip(entry.path);
        const std::string expected_name = "GPIO" + std::to_string(bcm);

        resolved_line = ResolvedGPIOLine{};
        resolved_line.bcm = bcm;
        resolved_line.chip_path = entry.path;
        resolved_line.backing_device_path = entry.backing_device_path;
        resolved_line.chip_name = entry.name;
        resolved_line.chip_label = entry.label;

#if GPIOD_API_MAJOR >= 2
        const int named_offset = chip.get_line_offset_from_name(expected_name);
        if (named_offset >= 0)
        {
            resolved_line.offset =
                gpiod::line::offset(static_cast<unsigned int>(named_offset));
            resolved_line.resolved_by_name = true;
            resolved_line.kernel_name = expected_name;
            used_offset_fallback = false;
            return true;
        }

        if (bcm < 0 || static_cast<std::size_t>(bcm) >= entry.num_lines)
        {
            return false;
        }

        resolved_line.offset =
            gpiod::line::offset(static_cast<unsigned int>(bcm));
        resolved_line.resolved_by_name = false;
        try
        {
            resolved_line.kernel_name = chip.get_line_info(resolved_line.offset).name();
        }
        catch (...)
        {
            resolved_line.kernel_name.clear();
        }

        used_offset_fallback = true;
        return true;
#else
        // libgpiod v1 builds in this tree use the older request/get_line API
        // surface. It does not expose the v2 metadata helpers preferred by the
        // resolver, so the compatibility path only accepts a raw-offset match
        // after verifying that the candidate chip actually exposes that offset.
        if (bcm < 0)
        {
            return false;
        }

        if (entry.num_lines > 0 && static_cast<std::size_t>(bcm) >= entry.num_lines)
        {
            return false;
        }

        try
        {
            (void)chip.get_line(static_cast<unsigned int>(bcm));
        }
        catch (...)
        {
            return false;
        }

        resolved_line.offset =
            gpiod::line::offset(static_cast<unsigned int>(bcm));
        resolved_line.resolved_by_name = false;
        resolved_line.kernel_name.clear();
        used_offset_fallback = true;
        return true;
#endif
    }
} // namespace

bool resolve_gpio_line(
    int bcm,
    ResolvedGPIOLine &resolved_line,
    std::string &error_message)
{
    error_message.clear();
    resolved_line = ResolvedGPIOLine{};

    if (bcm < 0 || bcm > 27)
    {
        error_message =
            "BCM GPIO " + std::to_string(bcm) + " is out of supported range 0-27.";
        return false;
    }

    std::vector<ChipEntry> entries;
    try
    {
        entries = enumerate_gpiochips();
    }
    catch (const std::exception &e)
    {
        error_message = std::string("Failed to enumerate /dev/gpiochip*: ") + e.what();
        return false;
    }

    if (entries.empty())
    {
        error_message = "No /dev/gpiochip* devices were found.";
        return false;
    }

    std::vector<ResolvedGPIOLine> named_candidates;
    std::vector<ResolvedGPIOLine> fallback_candidates;

    for (const ChipEntry &entry : entries)
    {
        try
        {
            ResolvedGPIOLine candidate;
            bool used_offset_fallback = false;
            if (!try_resolve_line_on_chip(entry, bcm, candidate, used_offset_fallback))
            {
                continue;
            }

            if (used_offset_fallback)
            {
                fallback_candidates.push_back(candidate);
            }
            else
            {
                named_candidates.push_back(candidate);
            }
        }
        catch (const std::exception &)
        {
            continue;
        }
    }

    if (named_candidates.size() == 1U)
    {
        resolved_line = named_candidates.front();
        return true;
    }

    if (named_candidates.size() > 1U)
    {
        if (collapse_equivalent_candidates(named_candidates, resolved_line))
        {
            return true;
        }

        std::ostringstream oss;
        oss << "BCM GPIO " << bcm
            << " resolved by kernel line name on multiple gpiochips:";
        for (const ResolvedGPIOLine &candidate : named_candidates)
        {
            oss << " " << candidate.chip_path;
            if (!candidate.backing_device_path.empty())
            {
                oss << "(device=" << candidate.backing_device_path << ")";
            }
        }
        oss << ". Chip selection is ambiguous.";
        error_message = oss.str();
        return false;
    }

    if (fallback_candidates.size() == 1U)
    {
        resolved_line = fallback_candidates.front();
        return true;
    }

    if (fallback_candidates.empty())
    {
        std::ostringstream oss;
        oss << "BCM GPIO " << bcm
            << " was not exposed by kernel line name and does not fit any detected gpiochip.";
        error_message = oss.str();
        return false;
    }

    if (collapse_equivalent_candidates(fallback_candidates, resolved_line))
    {
        return true;
    }

    std::ostringstream oss;
    oss << "BCM GPIO " << bcm
        << " could only be mapped by raw offset on multiple gpiochips:";
    for (const ResolvedGPIOLine &candidate : fallback_candidates)
    {
        oss << " " << candidate.chip_path;
        if (!candidate.backing_device_path.empty())
        {
            oss << "(device=" << candidate.backing_device_path << ")";
        }
    }
    oss << ". Refusing ambiguous raw-offset mapping.";
    error_message = oss.str();
    return false;
}

std::string describe_resolved_gpio_line(const ResolvedGPIOLine &resolved_line)
{
    std::ostringstream oss;
    oss << "BCM " << resolved_line.bcm
        << " -> " << resolved_line.chip_path
        << " offset " << static_cast<unsigned int>(resolved_line.offset)
        << " (" << (resolved_line.resolved_by_name ? "kernel-name" : "raw-offset") << ")";
    if (!resolved_line.kernel_name.empty())
    {
        oss << " line-name=\"" << resolved_line.kernel_name << "\"";
    }
    if (!resolved_line.chip_name.empty())
    {
        oss << " chip-name=" << resolved_line.chip_name;
    }
    if (!resolved_line.chip_label.empty())
    {
        oss << " chip-label=\"" << resolved_line.chip_label << "\"";
    }
    if (!resolved_line.backing_device_path.empty())
    {
        oss << " backing-device=" << resolved_line.backing_device_path;
    }
    if (!resolved_line.resolution_note.empty())
    {
        oss << " note=\"" << resolved_line.resolution_note << "\"";
    }
    return oss.str();
}
