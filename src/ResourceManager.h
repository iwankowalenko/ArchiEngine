#pragma once

#include <future>
#include <string>
#include <type_traits>
#include <unordered_map>

#include "Resources.h"

namespace archi
{
    class IRenderAdapter;

    class ResourceManager final
    {
    public:
        struct PendingMeshLoad
        {
            std::string key{};
            std::filesystem::path sourcePath{};
            std::filesystem::path binaryCachePath{};
            std::filesystem::file_time_type sourceWriteTime{};
            MeshData mesh{};
            MaterialAsset importedMaterial{};
            bool hasImportedMaterial = false;
            bool loadedFromBinaryCache = false;
            bool success = false;
            std::string error{};
        };

        struct PendingTextureLoad
        {
            std::string key{};
            std::filesystem::path sourcePath{};
            std::filesystem::path binaryCachePath{};
            std::filesystem::file_time_type sourceWriteTime{};
            TextureData texture{};
            bool loadedFromBinaryCache = false;
            bool success = false;
            std::string error{};
        };

        ResourceManager() = default;

        void SetRenderAdapter(IRenderAdapter* renderer);
        IRenderAdapter* Renderer() const { return m_renderer; }

        template <typename T>
        ResourcePtr<T> Load(const std::string& path)
        {
            if constexpr (std::is_same_v<T, MeshAsset>)
                return LoadMeshInternal(path, false);
            else if constexpr (std::is_same_v<T, TextureAsset>)
                return LoadTextureInternal(path, false);
            else if constexpr (std::is_same_v<T, ShaderAsset>)
                return LoadShaderInternal(path, false);
            else if constexpr (std::is_same_v<T, MaterialAsset>)
                return LoadMaterialInternal(path, false);
            else
                static_assert(AlwaysFalse<T>::value, "Unsupported resource type");
        }

        template <typename T>
        ResourcePtr<T> Reload(const std::string& path)
        {
            if constexpr (std::is_same_v<T, MeshAsset>)
                return LoadMeshInternal(path, true);
            else if constexpr (std::is_same_v<T, TextureAsset>)
                return LoadTextureInternal(path, true);
            else if constexpr (std::is_same_v<T, ShaderAsset>)
                return LoadShaderInternal(path, true);
            else if constexpr (std::is_same_v<T, MaterialAsset>)
                return LoadMaterialInternal(path, true);
            else
                static_assert(AlwaysFalse<T>::value, "Unsupported resource type");
        }

        ResourcePtr<MeshAsset> LoadMeshAsync(const std::string& path);
        ResourcePtr<TextureAsset> LoadTextureAsync(const std::string& path);

        ResourcePtr<MeshAsset> PlaceholderMesh() const { return m_placeholderMesh; }
        ResourcePtr<TextureAsset> PlaceholderTexture() const { return m_placeholderTexture; }
        ResourcePtr<MaterialAsset> PlaceholderMaterial() const { return m_placeholderMaterial; }
        ResourcePtr<MaterialAsset> DefaultMaterial() const { return m_defaultMaterial; }

        void ForceReloadAll();
        void UpdateHotReload();

    private:
        template <typename T>
        struct AlwaysFalse : std::false_type
        {
        };

        ResourcePtr<MeshAsset> LoadMeshInternal(const std::string& path, bool forceReload);
        ResourcePtr<TextureAsset> LoadTextureInternal(const std::string& path, bool forceReload);
        ResourcePtr<ShaderAsset> LoadShaderInternal(const std::string& path, bool forceReload);
        ResourcePtr<MaterialAsset> LoadMaterialInternal(const std::string& path, bool forceReload);

        void EnsureBuiltInResources();
        void FinalizeAsyncLoads();
        void UpdateShaderHotReload();
        void UpdateMaterialHotReload();
        void UpdateMeshHotReload();
        void UpdateTextureHotReload();

        std::string NormalizeAssetKey(const std::string& path) const;
        bool IsVirtualKey(const std::string& path) const;

        ResourcePtr<MaterialAsset> UpsertGeneratedMaterial(const std::string& key, const MaterialAsset& material);
        ResourcePtr<MeshAsset> CreatePlaceholderMeshResource();
        ResourcePtr<TextureAsset> CreatePlaceholderTextureResource();
        ResourcePtr<MaterialAsset> CreatePlaceholderMaterialResource();
        ResourcePtr<MaterialAsset> CreateDefaultMaterialResource();

    private:
        IRenderAdapter* m_renderer = nullptr;
        bool m_forceReloadAll = false;

        ResourcePtr<MeshAsset> m_placeholderMesh{};
        ResourcePtr<TextureAsset> m_placeholderTexture{};
        ResourcePtr<MaterialAsset> m_placeholderMaterial{};
        ResourcePtr<MaterialAsset> m_defaultMaterial{};

        std::unordered_map<std::string, ResourcePtr<MeshAsset>> m_meshCache{};
        std::unordered_map<std::string, ResourcePtr<TextureAsset>> m_textureCache{};
        std::unordered_map<std::string, ResourcePtr<ShaderAsset>> m_shaderCache{};
        std::unordered_map<std::string, ResourcePtr<MaterialAsset>> m_materialCache{};

        std::unordered_map<std::string, std::future<PendingMeshLoad>> m_pendingMeshLoads{};
        std::unordered_map<std::string, std::future<PendingTextureLoad>> m_pendingTextureLoads{};
    };
}
