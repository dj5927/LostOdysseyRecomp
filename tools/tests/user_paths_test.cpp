#include "os/user_paths.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static void Check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
#ifdef _WIN32
    const fs::path executable = fs::path("C:/Games/LostOdyssey");
    Check(os::user_paths::ConfigDir(executable) == executable, "Windows config path is not beside executable");
    Check(os::user_paths::DataDir(executable) == executable, "Windows data path is not beside executable");
    Check(os::user_paths::StateDir(executable) == executable, "Windows state path is not beside executable");
#else
    const fs::path root = fs::temp_directory_path() / "lo-user-paths-test";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root / "xdg" / "config");
    fs::create_directories(root / "xdg" / "data");
    fs::create_directories(root / "xdg" / "state");
    setenv("HOME", (root / "home").c_str(), 1);
    setenv("XDG_CONFIG_HOME", (root / "xdg/config").c_str(), 1);
    setenv("XDG_DATA_HOME", (root / "xdg/data").c_str(), 1);
    setenv("XDG_STATE_HOME", (root / "xdg/state").c_str(), 1);
    unsetenv("FLATPAK_ID");
    Check(os::user_paths::ConfigDir() == root / "xdg/config/lost-odyssey-recomp", "XDG config path mismatch");
    Check(os::user_paths::DataDir() == root / "xdg/data/lost-odyssey-recomp", "XDG data path mismatch");
    Check(os::user_paths::StateDir() == root / "xdg/state/lost-odyssey-recomp", "XDG state path mismatch");
    setenv("FLATPAK_ID", "com.example.LostOdyssey", 1);
    Check(os::user_paths::DataDir() == "/var/data", "Flatpak data path mismatch");
    Check(os::user_paths::IsExecutableDirWritable(root), "existing temporary directory is not writable");
    Check(!os::user_paths::IsExecutableDirWritable(root / "missing"), "missing directory reported writable");
    fs::remove_all(root, error);
#endif
    std::cout << "PASS: user paths fixture\n";
}
