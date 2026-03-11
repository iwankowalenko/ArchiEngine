#pragma once

#include <filesystem>

namespace archi
{
    std::filesystem::path FindAssetsRoot();
    std::filesystem::path ResolveAssetPath(const std::filesystem::path& relativeOrAbsolutePath);
    std::filesystem::path GetWritableAssetPath(const std::filesystem::path& relativePath);
}
