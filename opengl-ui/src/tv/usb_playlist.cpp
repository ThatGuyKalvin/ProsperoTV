// ProsperoTV - Bounded playlists on normal USB mounts.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/usb_playlist.hpp"
#include "tv/backup.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace ptv
{
bool usb_playlist_path(std::string_view path)
{
    if (path.size() < 15 || path.size() > 138 || path.substr(0, 8) != "/mnt/usb" || path[8] < '0' ||
        path[8] > '7' || path[9] != '/')
        return false;
    const auto name = path.substr(10);
    if (name.empty() || name.front() == '.' || name.find_first_of("/\\") != name.npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return false;
    std::string suffix(
        name.substr(name.find_last_of('.') == name.npos ? 0 : name.find_last_of('.')));
    for (auto &c : suffix)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return suffix == ".m3u" || suffix == ".m3u8";
}
std::vector<std::string> usb_playlists(const std::string &mount_root)
{
    std::vector<std::string> out;
    for (const auto &drive : usb_drives(mount_root))
    {
        DIR *dir = opendir(drive.c_str());
        if (!dir)
            continue;
        unsigned examined = 0;
        while (auto *entry = readdir(dir))
        {
            if (++examined > 4096 || out.size() >= 64)
                break;
            const std::string logical =
                "/mnt/" + drive.substr(drive.find_last_of('/') + 1) + "/" + entry->d_name;
            struct stat st
            {
            };
            if (usb_playlist_path(logical) &&
                lstat((drive + "/" + entry->d_name).c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
                st.st_size > 0 &&
                static_cast<std::uint64_t>(st.st_size) <= iptv::http::kMaxListBytes)
                out.push_back(logical);
        }
        closedir(dir);
    }
    std::sort(out.begin(), out.end());
    return out;
}
iptv::http::FetchResult read_usb_playlist(const std::string &path, iptv::http::ListSink sink,
                                          const iptv::http::RequestControl *control,
                                          const std::string &mount_root)
{
    using iptv::http::Status;
    iptv::http::FetchResult result;
    if (!sink.write || !usb_playlist_path(path))
        return result;
    const std::string drive = mount_root + path.substr(4, 5);
    const auto drives = usb_drives(mount_root);
    if (std::find(drives.begin(), drives.end(), drive) == drives.end())
    {
        result.status = Status::read_failed;
        return result;
    }
    const int directory = open(drive.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (directory < 0)
    {
        result.status = Status::read_failed;
        return result;
    }
    const int file = openat(directory, path.c_str() + 10, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    struct stat st
    {
    }, mount{};
    if (file < 0 || fstat(file, &st) != 0 || fstat(directory, &mount) != 0 ||
        !S_ISREG(st.st_mode) || st.st_dev != mount.st_dev || st.st_size <= 0 ||
        static_cast<std::uint64_t>(st.st_size) > iptv::http::kMaxListBytes)
    {
        if (file >= 0)
            close(file);
        close(directory);
        result.status = Status::read_failed;
        return result;
    }
    result.status = Status::ok;
    std::array<char, 65536> buffer{};
    for (;;)
    {
        if (control && control->cancelled && control->cancelled(control->context))
        {
            result.status = Status::cancelled;
            break;
        }
        const auto count = read(file, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
        {
            result.status = Status::read_failed;
            break;
        }
        if (count == 0)
            break;
        if (static_cast<std::size_t>(count) > iptv::http::kMaxListBytes - result.bytes)
        {
            result.status = Status::response_too_large;
            break;
        }
        result.bytes += static_cast<std::size_t>(count);
        if (!sink.write(sink.context, buffer.data(), static_cast<std::size_t>(count)))
        {
            result.status = Status::stopped;
            break;
        }
    }
    struct stat after
    {
    }, current{};
    if (result.status == Status::ok &&
        (result.bytes != static_cast<std::size_t>(st.st_size) || fstat(file, &after) != 0 ||
         after.st_size != st.st_size || after.st_mtime != st.st_mtime ||
         lstat(drive.c_str(), &current) != 0 || current.st_dev != mount.st_dev ||
         current.st_ino != mount.st_ino))
        result.status = Status::read_failed;
    close(file);
    close(directory);
    return result;
}
} // namespace ptv
