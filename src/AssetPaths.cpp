#include "AssetPaths.h"

#include <vector>

namespace archi
{
    namespace
    {
        std::vector<std::filesystem::path> CandidateRoots()
        {
            return {
                std::filesystem::current_path(),
                std::filesystem::current_path() / "..",
                std::filesystem::current_path() / ".." / ".."
            };
        }
    }

    std::filesystem::path FindAssetsRoot()
    {
        for (const auto& root : CandidateRoots())
        {
            const auto candidate = root / "assets";
            if (std::filesystem::exists(candidate))
                return std::filesystem::weakly_canonical(candidate);
        }

        return std::filesystem::current_path() / "assets";
    }

    std::filesystem::path ResolveAssetPath(const std::filesystem::path& relativeOrAbsolutePath)
    {
        if (relativeOrAbsolutePath.is_absolute())
            return relativeOrAbsolutePath;

        const auto assetsRoot = FindAssetsRoot();
        return assetsRoot / relativeOrAbsolutePath;
    }

    std::filesystem::path GetWritableAssetPath(const std::filesystem::path& relativePath)
    {
        const auto fullPath = ResolveAssetPath(relativePath);
        if (const auto parent = fullPath.parent_path(); !parent.empty())
            std::filesystem::create_directories(parent);
        return fullPath;
    }
}
