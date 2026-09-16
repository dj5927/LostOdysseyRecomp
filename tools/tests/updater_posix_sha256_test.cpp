#include "updater/update.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    updater::Release release;
    release.tag = "v1.2.3";
    release.assets.push_back({"LostOdysseyRecomp-linux-x64-v1.2.3.AppImage", "https://example.invalid/appimage", "", 1});
    std::string assetError;
    const auto asset = updater::SelectAsset(release, "linux", "x64", assetError);
    if (!asset || asset->name != "LostOdysseyRecomp-linux-x64-v1.2.3.AppImage" || !assetError.empty())
    {
        std::cerr << "Linux AppImage asset selection failed\n";
        return 1;
    }
    const auto path = std::filesystem::temp_directory_path() / "lo-updater-sha256-test.bin";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "abc";
    }
    std::string error;
    const auto digest = updater::Sha256File(path, error);
    std::filesystem::remove(path);
    if (digest != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" || !error.empty())
    {
        std::cerr << "unexpected SHA-256 result: " << digest << "\n";
        return 1;
    }
    error.clear();
    if (!updater::Sha256File(path, error).empty() || error != "could not open file for SHA256")
    {
        std::cerr << "missing-file error contract failed\n";
        return 1;
    }
    return 0;
}
