// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32
#include "native_windows_directory_owner.hpp"
namespace wsprrypi::recovery_windows {
class DirectoryLock:public WindowsDirectoryOwner {
public:
    explicit DirectoryLock(const std::string &path)
        :WindowsDirectoryOwner(windows_file::path_from_utf8(path),Mobility::Removable){}
};
}
#endif
