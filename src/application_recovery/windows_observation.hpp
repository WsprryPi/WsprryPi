// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_file.hpp"
#include "json.hpp"
namespace wsprrypi::recovery_windows {
inline std::int64_t unix_nanoseconds(std::int64_t ticks){
    constexpr std::int64_t epoch=116444736000000000;
    windows_file::require(ticks>=epoch+std::numeric_limits<std::int64_t>::min()/100&&
        ticks<=epoch+std::numeric_limits<std::int64_t>::max()/100);
    return (ticks-epoch)*100;
}
// Matches native_files.py observation exactly; never truncate FILE_ID_128 to a
// JSON number or a POSIX inode. The five-field receipt shape stays unchanged.
inline nlohmann::json observation(const windows_file::Snapshot &info){
    windows_file::require(info.standard.EndOfFile.QuadPart>=0);std::string identity;
    for(const auto byte:info.file_id.FileId.Identifier){identity+="0123456789abcdef"[byte>>4];identity+="0123456789abcdef"[byte&15];}
    return nlohmann::json::array({info.file_id.VolumeSerialNumber,identity,info.standard.EndOfFile.QuadPart,
        unix_nanoseconds(info.basic.LastWriteTime.QuadPart),unix_nanoseconds(info.basic.ChangeTime.QuadPart)});
}
}
#endif
