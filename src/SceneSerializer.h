#pragma once

#include <filesystem>

namespace archi
{
    class World;

    class SceneSerializer final
    {
    public:
        static bool SaveWorld(const World& world, const std::filesystem::path& path);
        static bool LoadWorld(World& world, const std::filesystem::path& path);
    };
}
