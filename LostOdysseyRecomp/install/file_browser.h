#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace install::ui
{
struct DirectoryItem
{
    std::string name;
    std::filesystem::path path;
    bool isDirectory = false;
    bool isDriveOrRoot = false;
};

// Returns system roots / drives (e.g. C:\, D:\ on Windows, / on Unix)
inline std::vector<DirectoryItem> GetSystemRoots()
{
    std::vector<DirectoryItem> roots;
#ifdef _WIN32
    DWORD drives = GetLogicalDrives();
    for (char c = 'A'; c <= 'Z'; ++c)
    {
        if (drives & (1 << (c - 'A')))
        {
            std::string rootStr = std::string(1, c) + ":\\";
            roots.push_back({ rootStr, std::filesystem::path(rootStr), true, true });
        }
    }
#else
    roots.push_back({ "/", std::filesystem::path("/"), true, true });
#endif
    return roots;
}

// Lists entries inside directory, sorted alphabetically, directories first
inline std::vector<DirectoryItem> ListDirectory(const std::filesystem::path& dir)
{
    std::vector<DirectoryItem> items;
    std::error_code ec;

    if (!dir.empty() && dir != dir.root_path())
    {
        items.push_back({ ".. (Parent Directory)", dir.parent_path(), true, false });
    }

    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
        return items;

    std::vector<DirectoryItem> dirs;
    std::vector<DirectoryItem> files;

    for (const auto& entry : std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec))
    {
        if (ec) break;
        bool isDir = entry.is_directory(ec);
        std::u8string u8fn = entry.path().filename().u8string();
        std::string filename(reinterpret_cast<const char*>(u8fn.data()), u8fn.size());
        if (filename.empty()) continue;

        if (isDir)
        {
            dirs.push_back({ filename, entry.path(), true, false });
        }
        else
        {
            files.push_back({ filename, entry.path(), false, false });
        }
    }

    auto comp = [](const DirectoryItem& a, const DirectoryItem& b) {
        return a.name < b.name;
    };
    std::sort(dirs.begin(), dirs.end(), comp);
    std::sort(files.begin(), files.end(), comp);

    items.insert(items.end(), dirs.begin(), dirs.end());
    items.insert(items.end(), files.begin(), files.end());

    return items;
}
}
